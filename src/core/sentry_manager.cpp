#include "core/sentry_manager.h"
#include "core/offline_queue_monitor.h"
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
bool SentryManager::s_crashed_last_run = false;
bool SentryManager::s_consent_given = true;
bool SentryManager::s_offline = false;
std::string SentryManager::s_release;
std::unordered_set<std::string> SentryManager::s_blocked_telemetry;

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

// Pre-built event.hook values — allocated at init so on_crash never calls
// sentry_value_new_* (not async-signal-safe).
static sentry_value_t g_before_send_hook = sentry_value_new_null();
static sentry_value_t g_on_crash_hook = sentry_value_new_null();
static sentry_value_t g_on_crash_tags = sentry_value_new_null();

void init_event_hook_tags() {
    g_before_send_hook = sentry_value_new_string("before_send");
    g_on_crash_hook = sentry_value_new_string("on_crash");
    g_on_crash_tags = sentry_value_new_object();
    sentry_value_set_by_key(g_on_crash_tags, "event.hook", g_on_crash_hook);
}

// before_send runs for every event prior to transmission. Here it is a light
// enrichment hook: it stamps a tag identifying the demo so events are easy to
// find, and demonstrates where PII scrubbing would live in a real integration.
static sentry_value_t tag_event(
    sentry_value_t event, sentry_value_t hook, sentry_value_t fallback_tags) {
    sentry_value_t tags = sentry_value_get_by_key(event, "tags");
    // Scope tags (demo, app.*) come from sentry_set_tag in apply_global_enrichment.
    // before_send: SDK merges scope onto the event first, so tags usually exist here
    //   → else branch sets event.hook on them.
    // on_crash: this hook runs before scope merge on crash backends, so tags are often
    //   still missing → if branch attaches fallback_tags (event.hook pre-set at init);
    //   the SDK then merges scope tags into that object afterward.
    if (sentry_value_is_null(tags)) {
        if (!sentry_value_is_null(fallback_tags)) {
            sentry_value_set_by_key(event, "tags", fallback_tags);
            sentry_value_incref(fallback_tags);
        }
    } else {
        sentry_value_set_by_key(tags, "event.hook", hook);
    }
    return event;
}

sentry_value_t before_send(sentry_value_t event, void* /*hint*/, void* /*closure*/) {
    return tag_event(event, g_before_send_hook, sentry_value_new_null());
}

// on_crash runs only for fatal crashes; the SDK calls it instead of
// before_send on that path. Same demo tagging via tag_event, with a distinct
// event.hook value so crash events are easy to filter in Sentry.
sentry_value_t on_crash(
    const sentry_ucontext_t* /*uctx*/, sentry_value_t event, void* /*data*/) {
    return tag_event(event, g_on_crash_hook, g_on_crash_tags);
}

// Signal-safety note:
// before_transaction (and before_send / on_crash) may run
// while the SDK is handling a crash — inside a signal handler or Windows
// exception filter. In that context only async-signal-safe code is allowed: no
// malloc, mutexes, or C++ containers like std::unordered_set (they can deadlock
// if the crashing thread held the heap lock). This helper uses
// std::unordered_set, which is fine for the demo's log/metric filters on normal
// threads, but not for production crash paths. A real integration would look up
// blocked keys with fixed C strings and sentry_value_* APIs only.
//
// Drop payload when payload[field] is on the block list (user_data).
sentry_value_t filter_telemetry(
    sentry_value_t payload, const char* field, void* user_data) {
    auto* blocked = static_cast<std::unordered_set<std::string>*>(user_data);
    const char* value =
        sentry_value_as_string(sentry_value_get_by_key(payload, field));
    if (value && blocked && blocked->count(value) != 0) {
        sentry_value_decref(payload);
        return sentry_value_new_null();
    }
    return payload;
}

SentryManager::TelemetryTapFn g_tap_metric;
SentryManager::TelemetryTapFn g_tap_log;

sentry_value_t before_send_log(sentry_value_t log, void* user_data) {
    // Tap first so the UI sees blocked payloads too; then maybe drop upload.
    if (g_tap_log) {
        const char* level =
            sentry_value_as_string(sentry_value_get_by_key(log, "level"));
        g_tap_log(log, SentryManager::is_telemetry_blocked(level));
    }
    return filter_telemetry(log, "level", user_data);
}

sentry_value_t before_send_metric(sentry_value_t metric, void* user_data) {
    // Same tap-then-filter order as before_send_log.
    if (g_tap_metric) {
        const char* name =
            sentry_value_as_string(sentry_value_get_by_key(metric, "name"));
        g_tap_metric(metric, SentryManager::is_telemetry_blocked(name));
    }
    return filter_telemetry(metric, "name", user_data);
}

sentry_value_t before_transaction(sentry_value_t tx, void* user_data) {
    return filter_telemetry(tx, "transaction", user_data);
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

    init_event_hook_tags();
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
    // Same release string can ship on multiple OSes; dist tells Sentry which build.
    sentry_options_set_dist(options, EMPOWER_PLATFORM);
    sentry_options_set_environment(options, config.environment.c_str());
    sentry_options_set_database_path(options, config.database_path.c_str());
    sentry_options_set_debug(options, config.debug ? 1 : 0);
    // Point the UI badge watcher at the same folder the SDK will use.
    OfflineQueueMonitor::configure(config.database_path);

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

    // --- Offline cache / retry ---------------------------------------------
    // Fixed demo defaults (not SentryConfig knobs): keep envelopes on disk,
    // retry failed/blocked sends, and gate uploads via user consent so
    // Settings consent and Chaos Lab demo-offline can block without re-init.
    sentry_options_set_cache_keep(options, SENTRY_CACHE_KEEP_ALWAYS);
    sentry_options_set_cache_max_items(options, 30);
    sentry_options_set_cache_max_size(options, 16 * 1024 * 1024); // 16 MiB
    sentry_options_set_cache_max_age(options, 5 * 24 * 60 * 60);  // 5 days
    sentry_options_set_http_retry(options, 1);
    sentry_options_set_require_user_consent(options, 1);

    // --- External crash reporter (official sentry-desktop-crash-reporter) --
    // Only for the interactive GUI: when set, the SDK hands the crash to this
    // separate app to submit (with a user-feedback dialog). A headless/CI binary
    // can't launch that GUI app, so it must submit crashes itself - otherwise
    // the crash is written out for the reporter and never sent.
    if (config.use_external_crash_reporter) {
        std::string reporter = !config.crash_reporter_path.empty()
            ? config.crash_reporter_path
            : find_crash_reporter();
        if (!reporter.empty()) {
            sentry_options_set_external_crash_reporter_path(options, reporter.c_str());
        }
    }

    sentry_options_set_before_send(options, before_send, nullptr);
    sentry_options_set_before_send_log(
        options, before_send_log, &SentryManager::s_blocked_telemetry);
    sentry_options_set_before_send_metric(
        options, before_send_metric, &SentryManager::s_blocked_telemetry);
    sentry_options_set_before_transaction(
        options, before_transaction, &SentryManager::s_blocked_telemetry);

    // The on_crash callback replaces the before_send callback for crash events.
    sentry_options_set_on_crash(options, on_crash, nullptr);

    if (sentry_init(options) != 0) {
        std::fprintf(stderr, "[empower] sentry_init failed\n");
        return false;
    }

    s_crashed_last_run = sentry_get_crashed_last_run() == 1;
    if (s_crashed_last_run) {
        sentry_clear_crashed_last_run();
    }

    // Start with uploads allowed: consent given, demo offline off.
    sentry_user_consent_give();
    s_consent_given = true;
    s_offline = false;

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
    s_crashed_last_run = false;
    s_consent_given = true;
    s_offline = false;
    s_blocked_telemetry.clear();
    clear_telemetry_tap();
}

void SentryManager::app_hang_heartbeat() {
    if (s_initialized) {
        sentry_app_hang_heartbeat();
    }
}

void SentryManager::sync_upload_gate() {
    if (!s_initialized) {
        return;
    }
    // One SDK consent gate, two app flags.
    if (s_consent_given && !s_offline) {
        sentry_user_consent_give();
    } else {
        sentry_user_consent_revoke();
    }
}

void SentryManager::set_user_consent(bool given) {
    if (!s_initialized || s_consent_given == given) {
        return;
    }
    s_consent_given = given;
    sync_upload_gate();
}

bool SentryManager::has_user_consent() {
    return s_initialized && s_consent_given;
}

void SentryManager::set_offline(bool offline) {
    if (!s_initialized || s_offline == offline) {
        return;
    }
    s_offline = offline;
    sync_upload_gate();
}

bool SentryManager::is_offline() { return s_initialized && s_offline; }

void SentryManager::set_telemetry_blocked(const char* key, bool blocked) {
    if (!key || !*key) {
        return;
    }
    if (blocked) {
        s_blocked_telemetry.insert(key);
    } else {
        s_blocked_telemetry.erase(key);
    }
}

bool SentryManager::is_telemetry_blocked(const char* key) {
    return key && s_blocked_telemetry.count(key) != 0;
}

void SentryManager::set_telemetry_tap(TelemetryTapFn on_metric, TelemetryTapFn on_log) {
    g_tap_metric = std::move(on_metric);
    g_tap_log = std::move(on_log);
}

void SentryManager::clear_telemetry_tap() {
    g_tap_metric = nullptr;
    g_tap_log = nullptr;
}

const std::string& SentryManager::release() { return s_release; }

bool SentryManager::initialized() { return s_initialized; }

bool SentryManager::crashed_last_run() { return s_crashed_last_run; }

bool SentryManager::capture_feedback(
    const char* message, const char* contact_email, const char* name,
    const char* attachment_path) {
    if (!s_initialized || !message || !*message) {
        return false;
    }

    sentry_value_t feedback = sentry_value_new_feedback(
        message,
        (contact_email && *contact_email) ? contact_email : nullptr,
        (name && *name) ? name : nullptr,
        nullptr);

    sentry_hint_t* hint = nullptr;
    if (attachment_path && *attachment_path && file_exists(attachment_path)) {
        hint = sentry_hint_new();
        if (hint) {
            sentry_hint_attach_file(hint, attachment_path);
        }
    }

    sentry_capture_feedback_with_hint(feedback, hint);
    return true;
}

} // namespace empower
