// Connectivity smoke test for the Empower Plant native demo.
//
// It proves the full modern pipeline end-to-end against a real DSN:
//   - SDK init with the new out-of-process "native" crash backend
//   - user / tags / context / breadcrumbs
//   - a captured message event (symbolicated, non-crash)
//   - a performance transaction with a child span
//   - a structured log line and a metric
//
// Run:  SENTRY_DSN=... ./empower-smoke
#include <cstdio>

#include <sentry.h>

#include "core/sentry_manager.h"

int main() {
    empower::SentryConfig cfg;
    cfg.environment = "development";
    cfg.component = "smoke";
    cfg.debug = true; // verbose SDK logging so the smoke test is observable

    if (!empower::SentryManager::init(cfg)) {
        std::fprintf(stderr, "smoke: init failed\n");
        return 1;
    }
    std::printf("smoke: initialized, release=%s\n",
                empower::SentryManager::release().c_str());

    // Identify the operator running the fleet console.
    sentry_value_t user = sentry_value_new_object();
    sentry_value_set_by_key(user, "id", sentry_value_new_string("operator-001"));
    sentry_value_set_by_key(user, "username", sentry_value_new_string("John Gardener"));
    sentry_value_set_by_key(user, "email", sentry_value_new_string("john.gardener@empower-plant.com"));
    sentry_set_user(user);

    sentry_set_tag("subsystem", "smoke");

    // Device context, like the GUI will attach per selected plant device.
    sentry_value_t device = sentry_value_new_object();
    sentry_value_set_by_key(device, "model", sentry_value_new_string("EmpowerPlant Mini"));
    sentry_value_set_by_key(device, "firmware", sentry_value_new_string("2.4.1"));
    sentry_set_context("device", device);

    sentry_add_breadcrumb(sentry_value_new_breadcrumb("default", "smoke test starting"));

    // A performance transaction with one child span.
    sentry_transaction_context_t* tx_ctx =
        sentry_transaction_context_new("smoke.run", "demo.task");
    sentry_transaction_t* tx = sentry_transaction_start(tx_ctx, sentry_value_new_null());
    sentry_span_t* span = sentry_transaction_start_child(tx, "db.query", "load fleet roster");
    sentry_span_finish(span);
    sentry_transaction_finish(tx);

    // Structured log + metric.
    sentry_value_t log_attrs = sentry_value_new_object();
    sentry_value_set_by_key(log_attrs, "phase",
                            sentry_value_new_attribute(sentry_value_new_string("smoke"), nullptr));
    sentry_log_info("smoke test reached the logging path", log_attrs);
    sentry_metrics_count("smoke.runs", 1, sentry_value_new_null());

    // A non-crash message event (server-side symbolicated stacktrace attached).
    sentry_capture_event(sentry_value_new_message_event(
        SENTRY_LEVEL_INFO, "smoke", "Empower Plant native smoke test event"));

    std::printf("smoke: events queued, flushing...\n");
    empower::SentryManager::shutdown();
    std::printf("smoke: done\n");
    return 0;
}
