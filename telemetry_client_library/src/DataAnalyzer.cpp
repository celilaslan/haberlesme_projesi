/**
 * @file DataAnalyzer.cpp
 * @brief Implementation of the DataAnalyzer class
 *
 * Advanced data analysis and quality monitoring for telemetry data.
 */

#include "TelemetryClient.h"
#include <map>
#include <mutex>
#include <chrono>
#include <deque>
#include <algorithm>
#include <numeric>

namespace TelemetryAPI {

/**
 * @struct DataStats
 * @brief Internal data statistics tracking
 */
struct DataStats {
    std::deque<uint64_t> timestamps;
    std::deque<size_t> packet_sizes;
    std::deque<TelemetryData> historical_data;
    int total_packets = 0;
    int lost_packets = 0;
    int last_sequence = -1;
    uint64_t last_update = 0;
    double total_latency = 0.0;
    int latency_samples = 0;

    // Circular buffer size limit
    static constexpr size_t MAX_HISTORY = 1000;

    void addPacket(const TelemetryData& data) {
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        timestamps.push_back(now);
        packet_sizes.push_back(data.raw_data.size());
        historical_data.push_back(data);
        total_packets++;
        last_update = now;

        // Maintain circular buffer
        if (timestamps.size() > MAX_HISTORY) {
            timestamps.pop_front();
            packet_sizes.pop_front();
            historical_data.pop_front();
        }

        // Calculate latency if timestamp is available
        if (data.timestamp_ms > 0) {
            double latency = now - data.timestamp_ms;
            if (latency >= 0 && latency < 60000) { // Reasonable latency (< 1 minute)
                total_latency += latency;
                latency_samples++;
            }
        }
    }
};

/**
 * @class DataAnalyzer::Impl
 * @brief Private implementation for DataAnalyzer
 */
class DataAnalyzer::Impl {
public:
    Impl() : validation_enabled_(false), total_bytes_in_(0), total_bytes_out_(0) {
        start_time_ = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    DataQuality getDataQuality(const std::string& uav_name, DataType type) {
        std::lock_guard<std::mutex> lock(stats_mutex_);

        std::string key = uav_name;
        if (type != DataType::UNKNOWN) {
            key += "_";
            key += (type == DataType::MAPPING ? "mapping" : "camera");
        }

        auto it = uav_stats_.find(key);
        if (it == uav_stats_.end()) {
            return DataQuality{0.0, 0.0, 0, 0.0, 0};
        }

        const auto& stats = it->second;
        DataQuality quality;

        // Calculate packet loss rate
        if (stats.total_packets > 0) {
            quality.packet_loss_rate = static_cast<double>(stats.lost_packets) /
                                     (stats.total_packets + stats.lost_packets);
        } else {
            quality.packet_loss_rate = 0.0;
        }

        // Calculate average latency
        if (stats.latency_samples > 0) {
            quality.average_latency_ms = stats.total_latency / stats.latency_samples;
        } else {
            quality.average_latency_ms = 0.0;
        }

        quality.missing_sequences = stats.lost_packets;
        quality.last_update_time = stats.last_update;

        // Calculate data freshness score (0.0 = very old, 1.0 = very fresh)
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        uint64_t age_ms = now - stats.last_update;

        if (age_ms < 1000) {          // < 1 second = perfect
            quality.data_freshness_score = 1.0;
        } else if (age_ms < 5000) {   // < 5 seconds = good
            quality.data_freshness_score = 1.0 - (age_ms - 1000) / 4000.0 * 0.3;
        } else if (age_ms < 30000) {  // < 30 seconds = acceptable
            quality.data_freshness_score = 0.7 - (age_ms - 5000) / 25000.0 * 0.4;
        } else {                      // > 30 seconds = poor
            quality.data_freshness_score = std::max(0.0, 0.3 - (age_ms - 30000) / 60000.0 * 0.3);
        }

        return quality;
    }

    bool enableDataValidation(bool enable) {
        validation_enabled_ = enable;
        return true;
    }

    std::vector<TelemetryData> getHistoricalData(const std::string& uav_name,
                                                uint64_t start_time,
                                                uint64_t end_time) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        std::vector<TelemetryData> result;

        // Search through all related stats for this UAV
        for (const auto& [key, stats] : uav_stats_) {
            if (key.find(uav_name) == 0) { // Starts with UAV name
                for (const auto& data : stats.historical_data) {
                    if (data.timestamp_ms >= start_time && data.timestamp_ms <= end_time) {
                        result.push_back(data);
                    }
                }
            }
        }

        // Sort by timestamp
        std::sort(result.begin(), result.end(),
                 [](const TelemetryData& a, const TelemetryData& b) {
                     return a.timestamp_ms < b.timestamp_ms;
                 });

        return result;
    }

    bool setDataRateLimit(const std::string& uav_name, int max_messages_per_second) {
        std::lock_guard<std::mutex> lock(rate_limits_mutex_);
        rate_limits_[uav_name] = max_messages_per_second;
        return true;
    }

    BandwidthStats getBandwidthUsage() {
        std::lock_guard<std::mutex> lock(bandwidth_mutex_);

        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        BandwidthStats stats;
        stats.total_bytes_received = total_bytes_in_;
        stats.total_bytes_sent = total_bytes_out_;

        // Calculate current rates (bytes per second over last minute)
        uint64_t window_start = now - 60000; // 1 minute window

        size_t recent_bytes_in = 0, recent_bytes_out = 0;
        for (const auto& sample : bandwidth_samples_) {
            if (sample.timestamp >= window_start) {
                recent_bytes_in += sample.bytes_in;
                recent_bytes_out += sample.bytes_out;
            }
        }

        stats.bytes_per_second_in = recent_bytes_in / 60.0;
        stats.bytes_per_second_out = recent_bytes_out / 60.0;

        // Calculate peak bandwidth
        stats.peak_bandwidth_in = peak_bandwidth_in_;
        stats.peak_bandwidth_out = peak_bandwidth_out_;

        return stats;
    }

    void recordDataReceived(const TelemetryData& data) {
        std::lock_guard<std::mutex> lock(stats_mutex_);

        // Create key for this data type and UAV
        std::string key = data.uav_name;
        if (data.data_type != DataType::UNKNOWN) {
            key += "_";
            key += (data.data_type == DataType::MAPPING ? "mapping" : "camera");
        }

        uav_stats_[key].addPacket(data);

        // Update bandwidth statistics
        {
            std::lock_guard<std::mutex> bw_lock(bandwidth_mutex_);
            total_bytes_in_ += data.raw_data.size();

            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            bandwidth_samples_.push_back({static_cast<uint64_t>(now), data.raw_data.size(), 0});

            // Keep only last hour of samples
            while (!bandwidth_samples_.empty() &&
                   bandwidth_samples_.front().timestamp < now - 3600000) {
                bandwidth_samples_.pop_front();
            }

            // Update peak bandwidth (calculate over 1-second windows)
            calculatePeakBandwidth();
        }
    }

    void recordDataSent(size_t bytes) {
        std::lock_guard<std::mutex> lock(bandwidth_mutex_);
        total_bytes_out_ += bytes;

        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        bandwidth_samples_.push_back({static_cast<uint64_t>(now), 0, bytes});

        // Keep only last hour of samples
        while (!bandwidth_samples_.empty() &&
               bandwidth_samples_.front().timestamp < now - 3600000) {
            bandwidth_samples_.pop_front();
        }

        calculatePeakBandwidth();
    }

private:
    struct BandwidthSample {
        uint64_t timestamp;
        size_t bytes_in;
        size_t bytes_out;
    };

    void calculatePeakBandwidth() {
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        // Calculate peak over 1-second windows
        for (uint64_t window_start = now - 60000; window_start < now; window_start += 1000) {
            uint64_t window_end = window_start + 1000;

            size_t window_bytes_in = 0, window_bytes_out = 0;
            for (const auto& sample : bandwidth_samples_) {
                if (sample.timestamp >= window_start && sample.timestamp < window_end) {
                    window_bytes_in += sample.bytes_in;
                    window_bytes_out += sample.bytes_out;
                }
            }

            peak_bandwidth_in_ = std::max(peak_bandwidth_in_, static_cast<double>(window_bytes_in));
            peak_bandwidth_out_ = std::max(peak_bandwidth_out_, static_cast<double>(window_bytes_out));
        }
    }

    mutable std::mutex stats_mutex_;
    mutable std::mutex rate_limits_mutex_;
    mutable std::mutex bandwidth_mutex_;

    std::map<std::string, DataStats> uav_stats_;  // UAV_name -> stats
    std::map<std::string, int> rate_limits_;      // UAV_name -> max_msg_per_sec

    bool validation_enabled_;
    uint64_t start_time_;

    // Bandwidth tracking
    size_t total_bytes_in_;
    size_t total_bytes_out_;
    double peak_bandwidth_in_ = 0.0;
    double peak_bandwidth_out_ = 0.0;
    std::deque<BandwidthSample> bandwidth_samples_;
};

// DataAnalyzer implementation
DataAnalyzer::DataAnalyzer() : pImpl(std::make_unique<Impl>()) {}

DataAnalyzer::~DataAnalyzer() = default;

DataQuality DataAnalyzer::getDataQuality(const std::string& uav_name, DataType type) {
    return pImpl->getDataQuality(uav_name, type);
}

bool DataAnalyzer::enableDataValidation(bool enable) {
    return pImpl->enableDataValidation(enable);
}

std::vector<TelemetryData> DataAnalyzer::getHistoricalData(const std::string& uav_name,
                                                          uint64_t start_time,
                                                          uint64_t end_time) {
    return pImpl->getHistoricalData(uav_name, start_time, end_time);
}

bool DataAnalyzer::setDataRateLimit(const std::string& uav_name, int max_messages_per_second) {
    return pImpl->setDataRateLimit(uav_name, max_messages_per_second);
}

BandwidthStats DataAnalyzer::getBandwidthUsage() {
    return pImpl->getBandwidthUsage();
}

} // namespace TelemetryAPI
