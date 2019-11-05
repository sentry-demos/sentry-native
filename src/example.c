#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sentry.h" // references sentry-native
// /bin has crashpad binary executables

#ifdef _WIN32
const char *handler_path = "bin/crashpad_handler.exe";
#else
const char *handler_path = "bin/crashpad_handler";
#endif

void initialize_memory(char *mem) {
    sentry_add_breadcrumb(sentry_value_new_breadcrumb(0, "Initializing memory"));
    memset(mem, 1, 100);
}

void startup(void) {
    sentry_set_transaction("startup");
    sentry_set_level(SENTRY_LEVEL_ERROR);

    sentry_add_breadcrumb(sentry_value_new_breadcrumb(0, "Setting user to John Doe"));

    sentry_value_t user = sentry_value_new_object();
    sentry_value_set_by_key(user, "id", sentry_value_new_int32(42));
    sentry_value_set_by_key(user, "email", sentry_value_new_string("john.doe@example.org"));
    sentry_value_set_by_key(user, "username", sentry_value_new_string("John Doe"));
    sentry_set_user(user);

    initialize_memory((char *)0x0);

    sentry_add_breadcrumb(sentry_value_new_breadcrumb(0, "Finished setup"));
}

void send_event(void) {
    sentry_set_transaction("startup");

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
    sentry_value_set_by_key(event, "message", sentry_value_new_string("Invariant Violation - Message"));
    sentry_value_set_by_key(event, "contexts", contexts);
    sentry_capture_event(event);
}

int main(void) {
    sentry_options_t *options = sentry_options_new();

    sentry_options_set_handler_path(options, handler_path);
    sentry_options_set_environment(options, "Production");
    sentry_options_set_release(options, "5fd7a6cd");
    sentry_options_set_database_path(options, "sentry-db");
    sentry_options_set_debug(options, 1);
    // sentry_options_add_attachment(options, "application.log",
    //                               "application.log");

    sentry_init(options);
    send_event();

    sentry_shutdown();

    // normally happens before sentry_shutdown
    startup();
}
