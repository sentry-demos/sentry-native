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
    // Block the crashing process until the daemon finishes uploading the crash.
    // Needed for one-shot runs (CI) where nothing relaunches to flush it.
    bool crash_upload_sync = false;
    // Hand crashes to the external crash reporter UI (interactive desktop app).
    // Headless/CI must leave this false so the SDK submits crashes itself.
    bool use_external_crash_reporter = false;
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

    // Demo "Go Offline": block uploads and queue envelopes in the local cache.
    // Toggle back to drain via HTTP retry. Owned here so callers don't need to
    // know about user-consent internals.
    static void set_offline(bool offline);
    static bool is_offline();
    // GDPR-style upload consent (Settings). Independent of Chaos Lab Go Offline.
    // SDK uploads only when consent is given AND demo offline is off.
    static void set_user_consent(bool given);
    static bool has_user_consent();

    // The release string actually used (resolved from config/env/built-in).
    static const std::string& release();

    static bool initialized();

private:
    static void sync_upload_gate();

    static bool s_initialized;
    static bool s_consent_given;
    static bool s_offline;
    static std::string s_release;
};

} // namespace empower
