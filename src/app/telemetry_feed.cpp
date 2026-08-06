#include "app/telemetry_feed.h"

#include "core/sentry_manager.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

#include <sentry.h>

namespace empower {

namespace {

std::string now_hms() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char stamp[16];
    std::strftime(stamp, sizeof(stamp), "%H:%M:%S", &tm);
    return stamp;
}

std::string format_metric_value(sentry_value_t value) {
    switch (sentry_value_get_type(value)) {
    case SENTRY_VALUE_TYPE_DOUBLE: {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.4g", sentry_value_as_double(value));
        return buf;
    }
    case SENTRY_VALUE_TYPE_INT32: {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d", sentry_value_as_int32(value));
        return buf;
    }
    case SENTRY_VALUE_TYPE_INT64: {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lld",
                      static_cast<long long>(sentry_value_as_int64(value)));
        return buf;
    }
    case SENTRY_VALUE_TYPE_UINT64: {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%llu",
                      static_cast<unsigned long long>(sentry_value_as_uint64(value)));
        return buf;
    }
    default:
        return sentry_value_as_string(value);
    }
}

TelemetryFeed* s_feed = nullptr;

} // namespace

void TelemetryFeed::upsert_metric(const char* name, const char* value, const char* type,
                                  bool blocked) {
    if (!name || !*name) {
        return;
    }
    for (Metric& m : metrics_) {
        if (m.name == name) {
            m.value = value ? value : "";
            m.type = type ? type : "";
            m.blocked = blocked;
            return;
        }
    }
    Metric m;
    m.name = name;
    m.value = value ? value : "";
    m.type = type ? type : "";
    m.blocked = blocked;
    metrics_.push_back(std::move(m));
    std::sort(metrics_.begin(), metrics_.end(),
              [](const Metric& a, const Metric& b) { return a.name < b.name; });
}

void TelemetryFeed::push_log(const char* level, const char* body, bool blocked) {
    Log ln;
    ln.time = now_hms();
    ln.level = level ? level : "";
    ln.body = body ? body : "";
    ln.blocked = blocked;
    logs_.push_back(std::move(ln));
    while (logs_.size() > kMaxLogs) {
        logs_.pop_front();
    }
}

void wire_telemetry_feed(TelemetryFeed& feed) {
    s_feed = &feed;
    SentryManager::set_telemetry_tap(
        [](sentry_value_t metric, bool blocked) {
            if (!s_feed) {
                return;
            }
            const char* name =
                sentry_value_as_string(sentry_value_get_by_key(metric, "name"));
            const char* type =
                sentry_value_as_string(sentry_value_get_by_key(metric, "type"));
            const char* unit =
                sentry_value_as_string(sentry_value_get_by_key(metric, "unit"));
            std::string value =
                format_metric_value(sentry_value_get_by_key(metric, "value"));
            if (unit && *unit) {
                value += " ";
                value += unit;
            }
            s_feed->upsert_metric(name, value.c_str(), type, blocked);
        },
        [](sentry_value_t log, bool blocked) {
            if (!s_feed) {
                return;
            }
            const char* level =
                sentry_value_as_string(sentry_value_get_by_key(log, "level"));
            const char* body =
                sentry_value_as_string(sentry_value_get_by_key(log, "body"));
            s_feed->push_log(level, body, blocked);
        });
}

void unwire_telemetry_feed() {
    SentryManager::clear_telemetry_tap();
    s_feed = nullptr;
}

} // namespace empower
