#pragma once

#include <string>

// SentryManager centralizes initialization, configuration and shutdown of the
// Sentry Native SDK for the Empower Plant Fleet Control Center demo.
//
// It is intentionally renderer-agnostic so the same wiring is shared by the
// GUI app (empower-fleet), the headless autopilot (empower-headless) and the
// Phase-0 smoke test.
namespace empower {

struct SentryConfig {
    // DSN is read from the SENTRY_DSN environment variable when empty.
    std::string dsn;
    // Where the SDK keeps its run database / unsent envelopes / minidumps.
    std::string database_path = ".sentry-native";
    // Logical environment ("production", "demo", "ci", "development").
    std::string environment = "production";
    // Which binary this is ("fleet", "headless", "smoke"); tagged on all data.
    std::string component = "fleet";
    // Release identifier; falls back to a built-in version when empty.
    std::string release;
    // Absolute path to the external crash reporter binary (optional).
    std::string crash_reporter_path;
    // Path to the native backend's out-of-process crash daemon (sentry-crash).
    // When empty, defaults to the daemon sitting beside the executable.
    std::string handler_path;
    // Verbose SDK logging to stderr.
    bool debug = false;
    // Performance tracing sample rate (1.0 == capture everything, for a demo).
    double traces_sample_rate = 1.0;
    // App-hang/ANR threshold in milliseconds (kept short for a snappy demo).
    int app_hang_timeout_ms = 2000;
};

class SentryManager {
public:
    // Initializes the SDK with the new out-of-process "native" crash backend,
    // performance tracing, structured logs, metrics and session tracking.
    // Returns true on success. Safe to call once.
    static bool init(const SentryConfig& config);

    // Flushes and shuts down the SDK. Call before process exit.
    static void shutdown();

    // Notifies the app-hang watchdog that the main thread is responsive.
    // Call once per frame from the UI loop.
    static void app_hang_heartbeat();

    // The release string actually used (resolved from config/env/built-in).
    static const std::string& release();

    static bool initialized();

private:
    static bool s_initialized;
    static std::string s_release;
};

} // namespace empower
