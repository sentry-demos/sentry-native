#pragma once

#include <deque>
#include <string>
#include <vector>

namespace empower {

class TelemetryFeed {
public:
    struct Metric {
        std::string name;
        std::string value;
        std::string type;
        bool blocked = false;
    };

    struct Log {
        std::string time;
        std::string level;
        std::string body;
        bool blocked = false;
    };

    void upsert_metric(const char* name, const char* value, const char* type, bool blocked);
    void push_log(const char* level, const char* body, bool blocked);

    const std::vector<Metric>& metrics() const { return metrics_; }
    const std::deque<Log>& logs() const { return logs_; }

private:
    std::vector<Metric> metrics_;
    std::deque<Log> logs_;
    static constexpr size_t kMaxLogs = 400;
};

void wire_telemetry_feed(TelemetryFeed& feed);
void unwire_telemetry_feed();

} // namespace empower
