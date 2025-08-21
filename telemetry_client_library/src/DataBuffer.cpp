/**
 * @file DataBuffer.cpp
 * @brief Implementation of the DataBuffer class
 *
 * Data buffering, recording, and replay functionality.
 */

#include <atomic>
#include <chrono>
#include <deque>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <thread>

#include "TelemetryClient.h"

using json = nlohmann::json;

namespace TelemetryAPI {

    /**
     * @class DataBuffer::Impl
     * @brief Private implementation for DataBuffer
     */
    class DataBuffer::Impl {
       public:
        Impl()
            : replaying_(false),
              max_buffer_size_(static_cast<size_t>(100) * 1024 * 1024) {}  // 100MB default

        // Disable copy and move operations for complex state management
        Impl(const Impl&) = delete;
        Impl& operator=(const Impl&) = delete;
        Impl(Impl&&) = delete;
        Impl& operator=(Impl&&) = delete;

        bool enableBuffering(size_t max_buffer_size_mb) {
            std::lock_guard<std::mutex> lock(buffer_mutex_);

            max_buffer_size_ = max_buffer_size_mb * 1024 * 1024;
            buffer_enabled_ = true;

            return true;
        }

        bool startRecording(const std::string& filename) {
            std::lock_guard<std::mutex> lock(recording_mutex_);

            if (recording_) {
                return false;  // Already recording
            }

            recording_file_.open(filename, std::ios::out | std::ios::trunc);
            if (!recording_file_.is_open()) {
                return false;
            }

            recording_filename_ = filename;
            recording_ = true;

            // Write file header
            json header;
            header["format_version"] = "1.0";
            header["start_time"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count();
            header["source"] = "TelemetryClient Library";

            recording_file_ << header.dump() << "\n---TELEMETRY_DATA---\n";
            recording_file_.flush();

            return true;
        }

        bool stopRecording() {
            std::lock_guard<std::mutex> lock(recording_mutex_);

            if (!recording_) {
                return false;
            }

            // Write file footer
            json footer;
            footer["end_time"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count();
            footer["total_records"] = recorded_count_;

            recording_file_ << "---END_TELEMETRY_DATA---\n" << footer.dump() << "\n";
            recording_file_.close();
            recording_ = false;

            return true;
        }

        bool replayData(const std::string& filename, double speed_multiplier) {
            if (replaying_) {
                return false;  // Already replaying
            }

            std::ifstream replay_file(filename);
            if (!replay_file.is_open()) {
                return false;
            }

            replaying_ = true;

            // Start replay thread
            replay_thread_ =
                std::thread([this, filename, speed_multiplier]() { performReplay(filename, speed_multiplier); });

            return true;
        }

        bool isRecording() const { return recording_; }

        bool isReplaying() const { return replaying_; }

        double getBufferUsage() const {
            std::lock_guard<std::mutex> lock(buffer_mutex_);

            if (max_buffer_size_ == 0) {
                return 0.0;
            }

            return static_cast<double>(current_buffer_size_) / static_cast<double>(max_buffer_size_);
        }

        void addTelemetryData(const TelemetryData& data) {
            // Add to buffer if enabled
            if (buffer_enabled_) {
                std::lock_guard<std::mutex> lock(buffer_mutex_);

                size_t data_size = estimateDataSize(data);

                // Make room if necessary
                while (current_buffer_size_ + data_size > max_buffer_size_ && !buffer_.empty()) {
                    current_buffer_size_ -= estimateDataSize(buffer_.front());
                    buffer_.pop_front();
                }

                buffer_.push_back(data);
                current_buffer_size_ += data_size;
            }

            // Record to file if recording
            if (recording_) {
                std::lock_guard<std::mutex> lock(recording_mutex_);

                if (recording_file_.is_open()) {
                    json record;
                    record["timestamp"] = data.timestamp_ms;
                    record["uav_name"] = data.uav_name;
                    record["data_type"] = static_cast<int>(data.data_type);
                    record["raw_data"] = data.raw_data;
                    record["protocol"] = static_cast<int>(data.received_via);

                    recording_file_ << record.dump() << "\n";
                    recorded_count_++;

                    // Flush periodically
                    if (recorded_count_ % 100 == 0) {
                        recording_file_.flush();
                    }
                }
            }
        }

        std::vector<TelemetryData> getBufferedData(size_t max_count) {
            std::lock_guard<std::mutex> lock(buffer_mutex_);

            std::vector<TelemetryData> result;
            size_t count = std::min(max_count, buffer_.size());

            auto buffer_iter = buffer_.rbegin();  // Start from most recent
            for (size_t i = 0; i < count; ++i, ++buffer_iter) {
                result.push_back(*buffer_iter);
            }

            // Reverse to get chronological order
            std::reverse(result.begin(), result.end());

            return result;
        }

        void clearBuffer() {
            std::lock_guard<std::mutex> lock(buffer_mutex_);
            buffer_.clear();
            current_buffer_size_ = 0;
        }

        ~Impl() {
            if (recording_) {
                stopRecording();
            }

            if (replaying_ && replay_thread_.joinable()) {
                replaying_ = false;
                replay_thread_.join();
            }
        }

       private:
        void performReplay(const std::string& filename, double speed_multiplier) {
            std::ifstream replay_file(filename);
            if (!replay_file.is_open()) {
                replaying_ = false;
                return;
            }

            std::string line;
            bool in_data_section = false;
            uint64_t first_timestamp = 0;
            uint64_t start_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::system_clock::now().time_since_epoch())
                                      .count();

            while (std::getline(replay_file, line) && replaying_) {
                if (line == "---TELEMETRY_DATA---") {
                    in_data_section = true;
                    continue;
                }

                if (line == "---END_TELEMETRY_DATA---") {
                    break;
                }

                if (!in_data_section) {
                    continue;
                }

                try {
                    json record = json::parse(line);

                    TelemetryData data;
                    data.timestamp_ms = record["timestamp"];
                    data.uav_name = record["uav_name"];
                    data.data_type = static_cast<DataType>(record["data_type"]);
                    data.raw_data = record["raw_data"];
                    data.received_via = static_cast<Protocol>(record["protocol"]);

                    // Calculate timing for replay
                    if (first_timestamp == 0) {
                        first_timestamp = data.timestamp_ms;
                    }

                    uint64_t relative_time = data.timestamp_ms - first_timestamp;
                    auto adjusted_time = static_cast<uint64_t>(static_cast<double>(relative_time) / speed_multiplier);
                    uint64_t target_time = start_time + adjusted_time;

                    // Wait until it's time to replay this data
                    uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count();

                    if (target_time > now) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(target_time - now));
                    }

                    // Trigger replay callback if registered
                    if (replay_callback_) {
                        replay_callback_(data);
                    }

                } catch (const std::exception&) {
                    // Skip malformed records
                    continue;
                }
            }

            replaying_ = false;
        }

        static size_t estimateDataSize(const TelemetryData& data) {
            return sizeof(TelemetryData) + data.raw_data.size() + data.uav_name.size();
        }

        mutable std::mutex buffer_mutex_;
        mutable std::mutex recording_mutex_;

        // Buffer management
        bool buffer_enabled_{false};
        size_t max_buffer_size_;
        size_t current_buffer_size_{0};
        std::deque<TelemetryData> buffer_;

        // Recording
        bool recording_{false};
        std::ofstream recording_file_;
        std::string recording_filename_;
        size_t recorded_count_ = 0;

        // Replay
        std::atomic<bool> replaying_;
        std::thread replay_thread_;
        std::function<void(const TelemetryData&)> replay_callback_;
    };

    // DataBuffer implementation
    DataBuffer::DataBuffer() : pImpl(std::make_unique<Impl>()) {}

    DataBuffer::~DataBuffer() = default;

    bool DataBuffer::enableBuffering(size_t max_buffer_size_mb) { return pImpl->enableBuffering(max_buffer_size_mb); }

    bool DataBuffer::startRecording(const std::string& filename) { return pImpl->startRecording(filename); }

    bool DataBuffer::stopRecording() { return pImpl->stopRecording(); }

    bool DataBuffer::replayData(const std::string& filename, double speed_multiplier) {
        return pImpl->replayData(filename, speed_multiplier);
    }

    bool DataBuffer::isRecording() const { return pImpl->isRecording(); }

    bool DataBuffer::isReplaying() const { return pImpl->isReplaying(); }

    double DataBuffer::getBufferUsage() const { return pImpl->getBufferUsage(); }

}  // namespace TelemetryAPI
