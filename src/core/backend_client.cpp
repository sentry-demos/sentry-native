#include "core/backend_client.h"

#include "app/console_log.h"

#include <chrono>
#include <cstring>
#include <string>
#include <vector>

#if defined(EMPOWER_HAVE_CURL)
#  include <curl/curl.h>
#elif defined(_WIN32)
#  include <windows.h>
#  include <winhttp.h>
#endif

#include <sentry.h>

namespace empower {

namespace {

struct Header {
    std::string key;
    std::string value;
};

// Collects the trace-propagation headers emitted by a span.
void collect_header(const char* key, const char* value, void* userdata) {
    static_cast<std::vector<Header>*>(userdata)->push_back({key, value});
}

#if defined(EMPOWER_HAVE_CURL)
size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    static_cast<std::string*>(userdata)->append(ptr, size * nmemb);
    return size * nmemb;
}

// Returns false when no HTTP transport is available.
bool perform_post(const std::string& url, const char* body,
                  const std::vector<Header>& headers, BackendResult& result) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    curl_slist* hdrs = curl_slist_append(nullptr, "Content-Type: application/json");
    for (const auto& h : headers) {
        hdrs = curl_slist_append(hdrs, (h.key + ": " + h.value).c_str());
    }
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result.body);
    // Keep the call well under the app-hang threshold so a slow backend can't
    // be mistaken for a UI hang (this runs on the main thread).
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "empower-fleet/1.0");
    if (curl_easy_perform(curl) == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status);
        result.ok = result.status >= 200 && result.status < 300;
    }
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);
    return true;
}

#elif defined(_WIN32)
std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

bool perform_post(const std::string& url, const char* body,
                  const std::vector<Header>& headers, BackendResult& result) {
    std::wstring wurl = widen(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {0}, path[2048] = {0};
    uc.lpszHostName = host;  uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path;   uc.dwUrlPathLength = 2047;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) return true;

    HINTERNET session = WinHttpOpen(L"empower-fleet/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return true;
    // Bound the call below the app-hang threshold (runs on the main thread).
    WinHttpSetTimeouts(session, 3000, 3000, 3000, 3000);
    HINTERNET conn = WinHttpConnect(session, host, uc.nPort, 0);
    HINTERNET req = conn
        ? WinHttpOpenRequest(conn, L"POST", path, nullptr, WINHTTP_NO_REFERER,
              WINHTTP_DEFAULT_ACCEPT_TYPES,
              uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
        : nullptr;
    if (req) {
        std::wstring hdrs = L"Content-Type: application/json\r\n";
        for (const auto& h : headers) hdrs += widen(h.key + ": " + h.value + "\r\n");
        WinHttpAddRequestHeaders(req, hdrs.c_str(), static_cast<DWORD>(-1),
                                 WINHTTP_ADDREQ_FLAG_ADD);
        DWORD body_len = static_cast<DWORD>(std::strlen(body));
        if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                               const_cast<char*>(body), body_len, body_len, 0) &&
            WinHttpReceiveResponse(req, nullptr)) {
            DWORD code = 0, len = sizeof(code);
            WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX);
            result.status = code;
            result.ok = code >= 200 && code < 300;
        }
        WinHttpCloseHandle(req);
    }
    if (conn) WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return true;
}

#else
bool perform_post(const std::string&, const char*, const std::vector<Header>&, BackendResult&) {
    return false; // no HTTP transport on this platform
}
#endif

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

    if (console) console->push(ConsoleLog::Level::Info, "checkout", "POST " + url);

    // Propagate sentry-trace / baggage from the span so the backend continues
    // this same trace.
    std::vector<Header> headers;
    sentry_span_iter_headers(span, collect_header, &headers);
    const auto t0 = std::chrono::steady_clock::now();
    perform_post(url, body, headers, result);
    const double elapsed_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();

    sentry_span_set_data(span, "http.response.status_code",
                         sentry_value_new_int32(static_cast<int32_t>(result.status)));
    sentry_span_set_status(span, result.ok ? SENTRY_SPAN_STATUS_OK
                                            : SENTRY_SPAN_STATUS_INTERNAL_ERROR);
    sentry_span_finish(span);
    sentry_transaction_set_status(tx, result.ok ? SENTRY_SPAN_STATUS_OK
                                                : SENTRY_SPAN_STATUS_INTERNAL_ERROR);
    sentry_transaction_finish(tx);

    char status[8];
    std::snprintf(status, sizeof(status), "%ld", result.status);
    sentry_value_t metric_attrs = sentry_value_new_object();
    sentry_value_set_by_key(metric_attrs, "status_code",
        sentry_value_new_attribute(sentry_value_new_string(status), nullptr));
    // METRIC: checkout.requests — HTTP checkout attempts, grouped by status_code.
    sentry_metrics_count("checkout.requests", 1, metric_attrs);
    // METRIC: checkout.duration — end-to-end POST latency in ms, grouped by status_code.
    sentry_metrics_distribution("checkout.duration", elapsed_ms, "millisecond", metric_attrs);

    return result;
}

} // namespace empower
