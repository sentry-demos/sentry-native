#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "sentry.h"
#include <curl/curl.h>
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

void api_call(void)
{
    printf("CHRIS api call");

  CURL *curl;
  char url[80];
  CURLcode res;

  strcpy(url,"https://application-monitoring-flask-dot-sales-engineering-sf.appspot.com/products-join");

  curl = curl_easy_init();
  if(curl) {
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, url);

    res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
  }
}

void startup(void)
{
    // sentry_set_transaction("startup");
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
    // sentry_set_transaction("send_event");

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

    sentry_options_set_traces_sample_rate(options, 1.0); // Set sample rate to capture performance data

    sentry_init(options);

    sentry_transaction_context_t *tx_ctx = sentry_transaction_context_new(
        "renderscreen", 
        "render.screen"
    );
    sentry_transaction_t *tx = sentry_transaction_start(tx_ctx, sentry_value_new_null());

    sentry_span_t *async = sentry_transaction_start_child(
        tx, 
        "http.async", 
        "async operation 1"
    );

    sleep(2);

    sentry_span_finish(async);

    sentry_span_t *child_filewrite = sentry_transaction_start_child(
        tx, 
        "file.write", 
        "file write 1"
    );
    sleep(3);

    sentry_span_t *grandchild_network_request_1 = sentry_transaction_start_child(
        tx, 
        "http.server", 
        "network request 1"
    );
    sleep(2);

    ////////////////////////////////////////////////
    ///////////// API CALL HERE ////////////////////
    ////////////////////////////////////////////////
    printf("Chris before apicall");
    // api_call();
    sentry_span_finish(grandchild_network_request_1);

    sentry_span_t *grandchild_network_request_2 = sentry_transaction_start_child(
        tx, 
        "http.server", 
        "network request 2"
    );
    sleep(1);
    sentry_span_finish(grandchild_network_request_2);

    sentry_span_finish(child_filewrite);

    sentry_span_t *async2 = sentry_transaction_start_child(
        tx, 
        "http.async", 
        "async operation 2"
    );

    sleep(2);

    sentry_span_t *fileread = sentry_transaction_start_child(
        tx, 
        "file.read", 
        "file read 1"
    );
    sleep(1);
    sentry_span_finish(fileread);

    sentry_span_t *filewrite = sentry_transaction_start_child(
        tx, 
        "file.write", 
        "file write 2"
    );
    sleep(3);
    sentry_span_finish(filewrite);

    sentry_transaction_finish(tx); // Mark the transaction as finished and send 

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
