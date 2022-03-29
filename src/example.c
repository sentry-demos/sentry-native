#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sentry.h"
// /bin has crashpad binary executables

#ifdef _WIN32
const char *handler_path = "bin/crashpad_handler.exe";
#else
const char *handler_path = "bin/crashpad_handler";
#endif

void initialize_memory(char *mem)
{
    sentry_add_breadcrumb(sentry_value_new_breadcrumb(0, "Initializing memory"));
    memset(mem, 1, 100);
}

void startup(void)
{
    sentry_set_transaction("startup");
    sentry_set_level(SENTRY_LEVEL_ERROR);

    sentry_add_breadcrumb(sentry_value_new_breadcrumb(0, "Setting user to John Doe"));

    sentry_value_t user = sentry_value_new_object();
    sentry_value_set_by_key(user, "id", sentry_value_new_int32(42));
    sentry_value_set_by_key(user, "email", sentry_value_new_string("john.doe@example.org"));
    sentry_value_set_by_key(user, "username", sentry_value_new_string("John Doe"));
    sentry_set_user(user);

    sentry_set_tag("sessionState", "crashed");

    initialize_memory((char *)0x0);

    sentry_add_breadcrumb(sentry_value_new_breadcrumb(0, "Finished setup"));
}

void send_event(void)
{
    sentry_set_transaction("send_event");

    sentry_add_breadcrumb(sentry_value_new_breadcrumb(0, "Configuring GPU Context"));

    sentry_value_t gpu = sentry_value_new_object();
    sentry_value_set_by_key(gpu, "name", sentry_value_new_string("AMD Radeon Pro 560"));
    sentry_value_set_by_key(gpu, "vendor_name", sentry_value_new_string("Apple"));
    sentry_value_set_by_key(gpu, "memory_size", sentry_value_new_int32(4096));
    sentry_value_set_by_key(gpu, "api_type", sentry_value_new_string("Metal"));
    sentry_value_set_by_key(gpu, "multi_threaded_rendering", sentry_value_new_bool(1));
    sentry_value_set_by_key(gpu, "version", sentry_value_new_string("Metal"));

    sentry_value_t os = sentry_value_new_object();
    sentry_value_set_by_key(os, "name", sentry_value_new_string("macOS"));
    sentry_value_set_by_key(os, "version", sentry_value_new_string("10.14.6 (18G95)"));

    sentry_value_t contexts = sentry_value_new_object();
    sentry_value_set_by_key(contexts, "gpu", gpu);
    sentry_value_set_by_key(contexts, "os", os);

    sentry_value_t event = sentry_value_new_event();
    sentry_value_set_by_key(event, "message", sentry_value_new_string("Sentry Message Capture"));
    sentry_value_set_by_key(event, "contexts", contexts);

    sentry_set_tag("key_name", "value");

    sentry_capture_event(event);
}

// use `make run message` or `make run crash`
int main(int argc, char *argv[])
{
    sentry_options_t *options = sentry_options_new();

    sentry_options_set_handler_path(options, handler_path);
    sentry_options_set_environment(options, "Production");

#ifdef SENTRY_RELEASE
    sentry_options_set_release(options, SENTRY_RELEASE);
#endif

    sentry_options_set_database_path(options, "sentry-db");
    sentry_options_set_debug(options, 1);

    // sentry_options_add_attachment(options, "application.log", "application.log");

    printf("CHRIS0");
    sentry_options_set_traces_sample_rate(options, 1.0); // Set sample rate to capture performance data
    printf("CHRIS1");

    sentry_init(options);

    // Trigger transactions
    sentry_transaction_context_t *tx_ctx
        = sentry_transaction_context_new("little.teapot",
            "Short and stout here is my handle and here is my spout");

    sentry_transaction_context_set_sampled(tx_ctx, 0);

    sentry_transaction_t *tx
        = sentry_transaction_start(tx_ctx, sentry_value_new_null());

    printf("CHRIS2");
    sentry_transaction_set_status(
        tx, SENTRY_SPAN_STATUS_INTERNAL_ERROR);

    sentry_span_t *child
        = sentry_transaction_start_child(tx, "littler.teapot", NULL);
    sentry_span_t *grandchild
        = sentry_span_start_child(child, "littlest.teapot", NULL);
    printf("CHRIS3");

    sentry_span_set_status(child, SENTRY_SPAN_STATUS_NOT_FOUND);
    sentry_span_set_status(
        grandchild, SENTRY_SPAN_STATUS_ALREADY_EXISTS);

    sentry_span_finish(grandchild);
    sentry_span_finish(child);

    sentry_transaction_finish(tx);
    printf("CHRIS4");

    // End instrument performance

    // Native Crash else do a Sentry Message
    if (argc != 2) {
        printf("Usage: %s [--crash|--message]\n", argv[0]);
    } else if(strcmp(argv[1], "--crash") == 0) {
        printf("SentryEventType: %s\n", argv[1]);
        startup();
    } else if( strcmp(argv[1], "--message") == 0 ) {
        printf("SentryEventType: %s\n", argv[1]);
        send_event();
    } else {
        printf("WrongArguments: run \'make run_crash\' or \'make run_message\' \n");
    }

    sentry_close();
    return 0;
}
