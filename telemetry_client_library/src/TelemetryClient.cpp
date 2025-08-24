/**
 * @file TelemetryClient.cpp
 * @brief Implementation of the TelemetryClient class
 *
 * This file contains the implementation of the simplified telemetry API,
 * wrapping the complex ZeroMQ and Boost.Asio networking code.
 */

#include "TelemetryClient.h"

#include <algorithm>
#include <atomic>
#include <boost/asio.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <sstream>
#include <thread>
#include <unordered_set>
#include <zmq.hpp>

using boost::asio::ip::udp;
using json = nlohmann::json;

namespace TelemetryAPI {


}  // namespace TelemetryAPI
