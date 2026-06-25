#include "core/sentry_manager.h"
#include "core/platform.h"

#include <cstdlib>
#include <cstdio>

#include <sentry.h>

#if defined(__aarch64__) || defined(_M_ARM64)
#  define EMPOWER_ARCH "arm64"
#elif defined(__x86_64__) || defined(_M_X64)
#  define EMPOWER_ARCH "x86_64"
#elif defined(__i386__) || defined(_M_IX86)
#  define EMPOWER_ARCH "x86"
#else
#  define EMPOWER_ARCH "unknown"
#endif

#if defined(_WIN32)
#  define EMPOWER_PLATFORM "windows"
#elif defined(__APPLE__)
#  define EMPOWER_PLATFORM "macos"
#else
#  define EMPOWER_PLATFORM "linux"
#endif

namespace empower {

bool SentryManager::s_initialized = false;
std::string SentryManager::s_release;

namespace {

const char* env_or(const char* name, const char* fallback) {
    const char* v = std::getenv(name);
    return (v && *v) ? v : fallback;
}

bool file_exists(const std::string& path) {
    if (path.empty()) return false;
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        std::fclose(f);
        return true;
    }
    return false;
}

// Locates a bundled external crash reporter (the official
// sentry-desktop-crash-reporter), if one sits next to the executable or is
// pointed at by EMPOWER_CRASH_REPORTER. Returns "" when none is found.
std::string find_crash_reporter() {
    const char* env = std::getenv("EMPOWER_CRASH_REPORTER");
    if (env && *env) return env;

    const std::string dir = executable_dir();
    const char* candidates[] = {
#if defined(_WIN32)
        "Sentry.CrashReporter.exe", "sentry-desktop-crash-reporter.exe",
#elif defined(__APPLE__)
        "Sentry Crash Reporter.app", "Sentry.CrashReporter",
#else
        "Sentry.CrashReporter", "sentry-desktop-crash-reporter",
#endif
    };
    for (const char* c : candidates) {
        std::string p = path_join(dir, c);
        if (file_exists(p)) return p;
    }
    return "";
}

// before_send runs for every event prior to transmission. Here it is a light
// enrichment hook: it stamps a tag identifying the demo so events are easy to
// find, and demonstrates where PII scrubbing would live in a real integration.
sentry_value_t before_send(sentry_value_t event, void* /*hint*/, void* /*closure*/) {
    sentry_value_t tags = sentry_value_get_by_key(event, "tags");
    if (sentry_value_is_null(tags)) {
        tags = sentry_value_new_object();
        sentry_value_set_by_key(event, "tags", tags);
    }
    sentry_value_set_by_key(tags, "demo", sentry_value_new_string("empower-plant-native"));
    return event;
}

sentry_value_t str_attr(const char* s) {
    return sentry_value_new_attribute(sentry_value_new_string(s), nullptr);
}

// Enriches the global scope so every event, log and metric carries consistent,
// queryable context. Must run after sentry_init.
void apply_global_enrichment(const SentryConfig& config, const std::string& release) {
    // Tags on every event (errors, crashes, transactions).
    sentry_set_tag("app.component", config.component.c_str());
    sentry_set_tag("app.backend", "native");
    sentry_set_tag("app.crash_reporting", "native_with_minidump");
    sentry_set_tag("app.platform", EMPOWER_PLATFORM);
    sentry_set_tag("app.arch", EMPOWER_ARCH);
    sentry_set_tag("demo", "empower-plant-native");
    if (const char* se = std::getenv("EMPOWER_SE")) {
        if (*se) sentry_set_tag("se", se);
    }

    // A rich "empower" context card shown on every event.
    const char* backend_url = env_or("EMPOWER_BACKEND_URL", "https://flask.empower-plant.com");
    sentry_value_t app = sentry_value_new_object();
    sentry_value_set_by_key(app, "component", sentry_value_new_string(config.component.c_str()));
    sentry_value_set_by_key(app, "crash_backend", sentry_value_new_string("native"));
    sentry_value_set_by_key(app, "minidump_mode", sentry_value_new_string("smart"));
    sentry_value_set_by_key(app, "crash_reporting_mode",
                            sentry_value_new_string("native_with_minidump"));
    sentry_value_set_by_key(app, "upload_mode", sentry_value_new_string("async"));
    sentry_value_set_by_key(app, "traces_sample_rate",
                            sentry_value_new_double(config.traces_sample_rate));
    sentry_value_set_by_key(app, "app_hang_timeout_ms",
                            sentry_value_new_int32(config.app_hang_timeout_ms));
    sentry_value_set_by_key(app, "backend_url", sentry_value_new_string(backend_url));
    sentry_set_context("empower", app);

    // A default operator user so every event (including headless) is attributed.
    sentry_value_t user = sentry_value_new_object();
    sentry_value_set_by_key(user, "id", sentry_value_new_string("operator-001"));
    sentry_value_set_by_key(user, "username", sentry_value_new_string("John Gardener"));
    sentry_value_set_by_key(user, "email",
                            sentry_value_new_string("john.gardener@empower-plant.com"));
    // Let Sentry infer the client IP server-side (the desktop SDK has no
    // send_default_pii option - that one is NX-only).
    sentry_value_set_by_key(user, "ip_address", sentry_value_new_string("{{auto}}"));
    sentry_set_user(user);

    // Attributes applied to ALL logs and metrics (queryable dimensions).
    sentry_set_attribute("app.component", str_attr(config.component.c_str()));
    sentry_set_attribute("app.platform", str_attr(EMPOWER_PLATFORM));
    sentry_set_attribute("environment", str_attr(config.environment.c_str()));
    sentry_set_attribute("release", str_attr(release.c_str()));
}

} // namespace

bool SentryManager::init(const SentryConfig& config) {
    if (s_initialized) {
        return true;
    }

    sentry_options_t* options = sentry_options_new();

    // --- Identity ----------------------------------------------------------
    std::string dsn = !config.dsn.empty() ? config.dsn : env_or("SENTRY_DSN", "");
    if (!dsn.empty()) {
        sentry_options_set_dsn(options, dsn.c_str());
    }

    // Default to the release baked in at build time (git commit), so events
    // match the release CI created with debug files and commit associations.
#ifdef EMPOWER_RELEASE
    const char* built_release = EMPOWER_RELEASE;
#else
    const char* built_release = "empower.native@1.0.0";
#endif
    s_release = !config.release.empty()
        ? config.release
        : env_or("SENTRY_RELEASE", built_release);
    sentry_options_set_release(options, s_release.c_str());
    sentry_options_set_environment(options, config.environment.c_str());
    sentry_options_set_database_path(options, config.database_path.c_str());
    sentry_options_set_debug(options, config.debug ? 1 : 0);

    // The native backend launches an out-of-process crash daemon (sentry-crash),
    // which must be locatable. Default to the daemon copied beside the binary.
    std::string handler = !config.handler_path.empty()
        ? config.handler_path
        : path_join(executable_dir(),
#ifdef _WIN32
                    "sentry-crash.exe"
#else
                    "sentry-crash"
#endif
        );
    sentry_options_set_handler_path(options, handler.c_str());

    // --- New out-of-process "native" crash backend -------------------------
    // Selected at build time via -DSENTRY_BACKEND=native: a client-side native
    // stackwalk plus a smart minidump, with the daemon finishing the upload.
    sentry_options_set_crash_reporting_mode(
        options, SENTRY_CRASH_REPORTING_MODE_NATIVE_WITH_MINIDUMP);
    sentry_options_set_minidump_mode(options, SENTRY_MINIDUMP_MODE_SMART);
    sentry_options_set_crash_upload_mode(options,
        config.crash_upload_sync ? SENTRY_CRASH_UPLOAD_MODE_SYNC
                                 : SENTRY_CRASH_UPLOAD_MODE_ASYNC);
    if (config.crash_upload_sync) {
        // SYNC keeps the crashed process alive until the daemon is done, but the
        // daemon only gets `shutdown_timeout` to flush. The default (2s) is too
        // short for our ~1MB crash envelope (minidump + screenshot), so it would
        // be dumped to disk for "next restart" - which never happens in a
        // one-shot CI run. Give it enough time to finish the upload in-process
        // (kept under the ~10s crash-handler wait cap).
        sentry_options_set_shutdown_timeout(options, 8000);
    }

    // --- Performance, logs, metrics, sessions ------------------------------
    sentry_options_set_traces_sample_rate(options, config.traces_sample_rate);
    sentry_options_set_symbolize_stacktraces(options, 1);
    sentry_options_set_enable_logs(options, 1);
    sentry_options_set_logs_with_attributes(options, 1); // per-line log attributes
    sentry_options_set_enable_metrics(options, 1);
    sentry_options_set_auto_session_tracking(options, 1);

    // --- App-hang / ANR detection ------------------------------------------
    sentry_options_set_enable_app_hang_tracking(options, 1);
    sentry_options_set_app_hang_timeout(options, config.app_hang_timeout_ms);

    // --- Screenshots (Windows only) ----------------------------------------
#ifdef _WIN32
    sentry_options_set_attach_screenshot(options, 1);
#endif

    // --- External crash reporter (official sentry-desktop-crash-reporter) --
    std::string reporter = !config.crash_reporter_path.empty()
        ? config.crash_reporter_path
        : find_crash_reporter();
    if (!reporter.empty()) {
        sentry_options_set_external_crash_reporter_path(options, reporter.c_str());
    }

    sentry_options_set_before_send(options, before_send, nullptr);

    if (sentry_init(options) != 0) {
        std::fprintf(stderr, "[empower] sentry_init failed\n");
        return false;
    }

    s_initialized = true;
    apply_global_enrichment(config, s_release);
    return true;
}

void SentryManager::shutdown() {
    if (!s_initialized) {
        return;
    }
    sentry_close();
    s_initialized = false;
}

void SentryManager::app_hang_heartbeat() {
    if (s_initialized) {
        sentry_app_hang_heartbeat();
    }
}

const std::string& SentryManager::release() { return s_release; }

bool SentryManager::initialized() { return s_initialized; }

} // namespace empower
