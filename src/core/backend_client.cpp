#include "core/backend_client.h"

#include "app/console_log.h"

#include <string>

#include <curl/curl.h>
#include <sentry.h>

namespace empower {

namespace {

size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

// Collects the trace-propagation headers emitted by a span into a curl list.
void collect_header(const char* key, const char* value, void* userdata) {
    auto** list = static_cast<curl_slist**>(userdata);
    std::string header = std::string(key) + ": " + value;
    *list = curl_slist_append(*list, header.c_str());
}

} // namespace

BackendResult checkout(const std::string& base_url, ConsoleLog* console) {
    const std::string url =
        (base_url.empty() ? "https://flask.empower-plant.com" : base_url) + "/checkout";
    const char* body =
        "{\"cart\":{\"items\":[{\"id\":\"PLANT-MOOD\",\"price\":155,\"qty\":3}],"
        "\"total\":465},\"email\":\"john.gardener@empower-plant.com\","
        "\"reason\":\"fleet replacement order\"}";

    BackendResult result;

    sentry_transaction_context_t* tx_ctx =
        sentry_transaction_context_new("checkout", "http.client");
    sentry_transaction_t* tx = sentry_transaction_start(tx_ctx, sentry_value_new_null());
    sentry_span_t* span = sentry_transaction_start_child(tx, "http.client", "POST /checkout");
    sentry_span_set_data(span, "http.request.method", sentry_value_new_string("POST"));
    sentry_span_set_data(span, "server.address", sentry_value_new_string(url.c_str()));

    if (console)
        console->push(ConsoleLog::Level::Info, "checkout", "POST " + url);

    CURL* curl = curl_easy_init();
    if (curl) {
        curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        // Inject sentry-trace / baggage from the span so the backend continues
        // this same trace.
        sentry_span_iter_headers(span, collect_header, &headers);

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result.body);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "empower-fleet/1.0");

        CURLcode rc = curl_easy_perform(curl);
        if (rc == CURLE_OK) {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status);
            result.ok = result.status >= 200 && result.status < 300;
        }
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }

    sentry_value_t status_obj = sentry_value_new_object();
    sentry_value_set_by_key(status_obj, "http.response.status_code",
                            sentry_value_new_int32(static_cast<int32_t>(result.status)));
    sentry_span_set_data(span, "http.response.status_code",
                         sentry_value_new_int32(static_cast<int32_t>(result.status)));
    sentry_span_set_status(span, result.ok ? SENTRY_SPAN_STATUS_OK
                                            : SENTRY_SPAN_STATUS_INTERNAL_ERROR);
    sentry_span_finish(span);
    sentry_transaction_set_status(tx, result.ok ? SENTRY_SPAN_STATUS_OK
                                                : SENTRY_SPAN_STATUS_INTERNAL_ERROR);
    sentry_transaction_finish(tx);

    return result;
}

} // namespace empower
