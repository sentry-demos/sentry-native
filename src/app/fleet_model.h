#pragma once

#include <array>
#include <string>
#include <vector>

namespace empower {

// A simulated Empower Plant IoT device in the fleet.
struct Device {
    enum class Status { Ok, Warning, Error, Offline };

    std::string id;        // "plant-01"
    std::string name;      // "Lobby Monstera"
    std::string location;  // "HQ / Floor 3"
    std::string firmware;  // "2.4.1"
    Status status = Status::Ok;

    float soil_moisture = 0.5f; // 0..1
    float light = 0.5f;         // 0..1
    float battery = 1.0f;       // 0..1
    float cpu = 0.2f;           // 0..1

    // Per-device phase so the simulated sensors don't move in lockstep.
    float phase = 0.0f;
};

// A fixed-length history used to drive the telemetry sparklines.
struct Series {
    static constexpr int kLen = 120;
    std::array<float, kLen> data{};
    int head = 0;

    void push(float v) {
        data[head] = v;
        head = (head + 1) % kLen;
    }
    float latest() const { return data[(head + kLen - 1) % kLen]; }

    float rolling_avg(int window) const {
        if (window < 1) window = 1;
        if (window > kLen) window = kLen;
        float sum = 0.0f;
        for (int i = 0; i < window; ++i)
            sum += data[(head + kLen - 1 - i) % kLen];
        return sum / static_cast<float>(window);
    }
};

// The simulated state behind the dashboard: a fleet of devices plus rolling
// telemetry. Updated once per frame; rendering reads from it.
class FleetModel {
public:
    void init();
    void update(float dt, float frame_ms);

    const std::vector<Device>& devices() const { return devices_; }
    std::vector<Device>& devices() { return devices_; }

    int online_count() const;
    int alert_count() const;

    const Series& frame_time() const { return frame_time_; }
    const Series& net_latency() const { return net_latency_; }
    const Series& cpu_load() const { return cpu_load_; }
    const Series& soil_avg() const { return soil_avg_; }
    int queue_depth() const { return queue_depth_; }

    static const char* status_label(Device::Status s);

private:
    std::vector<Device> devices_;
    Series frame_time_;
    Series net_latency_;
    Series cpu_load_;
    Series soil_avg_;
    int queue_depth_ = 0;
    float clock_ = 0.0f;
};

} // namespace empower
