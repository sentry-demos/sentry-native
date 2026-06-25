// Headless runner for the Empower Plant demo: no window, same Sentry wiring.
//
// Modes:
//   --autopilot [--duration N]   self-drive for N seconds emitting transactions,
//                                metrics and logs, then crash (non-zero exit).
//   --crash <scenario>           run one scenario and exit (or crash).
//   --listen <port>              accept remote POST /trigger/<scenario> commands.
//   --no-final-crash             autopilot keeps running without the final crash.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <thread>
#include <vector>

#include <sentry.h>

#include "app/console_log.h"
#include "app/fleet_model.h"
#include "chaos/chaos.h"
#include "core/backend_client.h"
#include "core/command_server.h"
#include "core/platform.h"
#include "core/sentry_manager.h"

namespace {

const char* env_or(const char* name, const char* fallback) {
    const char* v = std::getenv(name);
    return (v && *v) ? v : fallback;
}

void sleep_ms(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// Waits while keeping the app-hang watchdog fed, so the autopilot's own pacing
// is never mistaken for a hang (only the deliberate app-hang scenario blocks
// without a heartbeat).
void idle(int ms) {
    const int step = 150;
    for (int elapsed = 0; elapsed < ms; elapsed += step) {
        empower::SentryManager::app_hang_heartbeat();
        sleep_ms(ms - elapsed < step ? ms - elapsed : step);
    }
    empower::SentryManager::app_hang_heartbeat();
}

// One simulated pipeline run as a performance transaction with child spans,
// plus a metric and a structured log - the steady-state demo data.
void run_pipeline(const char* name, const char* op) {
    sentry_transaction_context_t* ctx = sentry_transaction_context_new(name, op);
    sentry_transaction_t* tx = sentry_transaction_start(ctx, sentry_value_new_null());

    sentry_span_t* load = sentry_transaction_start_child(tx, "task", "load device data");
    sleep_ms(40 + std::rand() % 80);
    sentry_span_finish(load);

    sentry_span_t* process = sentry_transaction_start_child(tx, "task", "process samples");
    sleep_ms(30 + std::rand() % 120);
    sentry_span_finish(process);

    sentry_transaction_finish(tx);

    sentry_value_t metric_attrs = sentry_value_new_object();
    sentry_value_set_by_key(metric_attrs, "pipeline",
                            sentry_value_new_attribute(sentry_value_new_string(name), nullptr));
    sentry_metrics_distribution(name, 1.0 + (std::rand() % 100) / 100.0,
                                "second", metric_attrs);

    sentry_value_t log_attrs = sentry_value_new_object();
    sentry_value_set_by_key(log_attrs, "pipeline",
                            sentry_value_new_attribute(sentry_value_new_string(name), nullptr));
    sentry_log_info("pipeline '%s' completed", log_attrs, name);
}

} // namespace

int main(int argc, char** argv) {
    bool autopilot = false;
    bool final_crash = true;
    int duration = 60;
    int listen_port = 0;
    std::string crash_id;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--autopilot") == 0) autopilot = true;
        else if (std::strcmp(argv[i], "--no-final-crash") == 0) final_crash = false;
        else if (std::strcmp(argv[i], "--duration") == 0 && i + 1 < argc) duration = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--listen") == 0 && i + 1 < argc) listen_port = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--crash") == 0 && i + 1 < argc) crash_id = argv[++i];
    }

    std::srand(static_cast<unsigned>(std::time(nullptr)));

    empower::SentryConfig cfg;
    cfg.environment = env_or("SENTRY_ENVIRONMENT", "ci");
    cfg.component = "headless";
    cfg.crash_upload_sync = true; // one-shot run: upload the crash before exit
    // Headroom over normal pacing (the backend call is bounded to 3s) so only
    // the deliberate 8s app-hang scenario trips the watchdog, not the autopilot.
    cfg.app_hang_timeout_ms = 6000;
    cfg.debug = env_or("EMPOWER_DEBUG", "")[0] != '\0';
    if (!empower::SentryManager::init(cfg)) {
        std::fprintf(stderr, "headless: sentry init failed (continuing)\n");
    }

    // The headless runner renders nothing, so attach a representative snapshot
    // of the dashboard as the event screenshot (mirrors what the GUI captures
    // live). Named screenshot.png so Sentry surfaces it in its Screenshots view.
    const char* shot_env = std::getenv("EMPOWER_SCREENSHOT");
    std::string shot = shot_env && *shot_env
        ? shot_env
        : empower::path_join(empower::executable_dir(), "screenshot.png");
    if (FILE* f = std::fopen(shot.c_str(), "rb")) {
        std::fclose(f);
        sentry_attach_file(shot.c_str());
    }

    empower::FleetModel fleet;
    fleet.init();
    empower::ConsoleLog console;

    empower::CommandServer server;
    if (listen_port > 0) {
        bool ok = server.start(listen_port, [&console](const std::string& cmd) {
            std::fprintf(stderr, "remote command: %s\n", cmd.c_str());
            empower::trigger(cmd, &console);
        });
        std::fprintf(stderr, "command server on 127.0.0.1:%d %s\n", listen_port,
                     ok ? "(listening)" : "(failed to bind)");
    }

    if (!crash_id.empty()) {
        std::fprintf(stderr, "running scenario: %s\n", crash_id.c_str());
        empower::trigger(crash_id, &console);
        empower::SentryManager::shutdown();
        return 0;
    }

    if (autopilot) {
        std::fprintf(stderr, "autopilot: running for %ds\n", duration);
        // A CI run fires exactly three notable issue events, in order:
        //   1. a backend error captured on a real distributed trace to Flask,
        //   2. an app hang carrying a lot of attached data,
        //   3. the headline "Convoluted Chain" hard crash at the end.
        // Everything else here - transactions, metrics, logs and the periodic
        // checkout traces - is steady-state telemetry, not issue events. The
        // remaining Chaos Lab scenarios stay available for manual/local use
        // (GUI buttons, --crash <id>, or the --listen remote trigger).
        const auto start = std::chrono::steady_clock::now();
        const auto end = start + std::chrono::seconds(duration);
        bool fired_backend = false;
        bool fired_hang = false;
        int iter = 0;
        while (std::chrono::steady_clock::now() < end) {
            empower::SentryManager::app_hang_heartbeat();
            run_pipeline("sensor.pipeline", "device.ingest");
            empower::SentryManager::app_hang_heartbeat();
            if (iter % 3 == 0) {
                empower::checkout("", &console);                    // distributed trace (no error event)
                empower::SentryManager::app_hang_heartbeat();
            }
            if (iter % 4 == 0) {
                run_pipeline("image.processing", "image.classify");
                empower::SentryManager::app_hang_heartbeat();
            }
            sentry_value_t online_attrs = sentry_value_new_object();
            sentry_value_set_by_key(online_attrs, "fleet_size",
                sentry_value_new_attribute(
                    sentry_value_new_int32(static_cast<int>(fleet.devices().size())), nullptr));
            sentry_metrics_gauge("fleet.devices_online", fleet.online_count(),
                                 "none", online_attrs);

            const double elapsed = std::chrono::duration<double>(
                                       std::chrono::steady_clock::now() - start)
                                       .count();
            // Event 1 (~40% in): the single backend error on a trace.
            if (!fired_backend && elapsed > duration * 0.4) {
                std::fprintf(stderr, "autopilot: event 1 - backend error on a trace\n");
                empower::trigger("backend-500", &console);
                fired_backend = true;
            }
            // Event 2 (~70% in): the single app hang, with a lot of data.
            if (!fired_hang && elapsed > duration * 0.7) {
                std::fprintf(stderr, "autopilot: event 2 - app hang (rich data)\n");
                empower::trigger("app-hang", &console);
                fired_hang = true;
            }
            ++iter;
            idle(1500);
        }
        if (final_crash) {
            // Event 3: the deterministic headline crash, so every CI run yields
            // the same Seer-friendly cross-thread/cross-subsystem crash.
            std::fprintf(stderr, "autopilot: event 3 - final crash 'convoluted'\n");
            empower::trigger("convoluted", &console); // crashes -> non-zero exit for CI
        }
    } else if (listen_port > 0) {
        std::fprintf(stderr, "idle: waiting for remote commands (Ctrl-C to exit)\n");
        while (true) {
            empower::SentryManager::app_hang_heartbeat();
            sleep_ms(1000);
        }
    }

    server.stop();
    empower::SentryManager::shutdown();
    return 0;
}
