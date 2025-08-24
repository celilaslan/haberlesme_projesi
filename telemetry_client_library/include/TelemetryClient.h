/**
 * @file TelemetryClient.h
 * @brief Simple C++ API for communicating with the telemetry service
 *
 * This header defines a clean, easy-to-use interface for developers who want
 * to communicate with the telemetry service without dealing with TCP or
 * UDP networking details directly.
 */

#ifndef TELEMETRY_CLIENT_H
#define TELEMETRY_CLIENT_H


// Export symbols for shared library
#ifdef _WIN32
#ifdef TELEMETRY_CLIENT_EXPORTS
#define TELEMETRY_API __declspec(dllexport)
#else
#define TELEMETRY_API __declspec(dllimport)
#endif
#else
#ifdef TELEMETRY_CLIENT_EXPORTS
#define TELEMETRY_API __attribute__((visibility("default")))
#else
#define TELEMETRY_API
#endif
#endif

namespace TelemetryAPI {



}  // namespace TelemetryAPI

#endif  // TELEMETRY_CLIENT_H
