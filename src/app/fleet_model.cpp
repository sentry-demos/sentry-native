#include "app/fleet_model.h"

#include <cmath>

namespace empower {

namespace {
struct Seed {
    const char* name;
    const char* location;
    Device::Status status;
};

// A believable starting roster. Most are healthy; a couple show wear so the
// fleet grid has color and the "alerts" counter is non-zero.
const Seed kSeeds[] = {
    {"Lobby Monstera",     "HQ / Floor 1",   Device::Status::Ok},
    {"Atrium Fern",        "HQ / Floor 2",   Device::Status::Ok},
    {"Standup Pothos",     "HQ / Floor 3",   Device::Status::Warning},
    {"Cafeteria Palm",     "HQ / Floor 1",   Device::Status::Ok},
    {"Server Room Cactus", "HQ / Basement",  Device::Status::Error},
    {"Reception Orchid",   "HQ / Floor 1",   Device::Status::Ok},
    {"Lab Succulent",      "R&D / Floor 4",  Device::Status::Ok},
    {"Rooftop Olive",      "HQ / Rooftop",   Device::Status::Offline},
    {"Sales Bamboo",       "HQ / Floor 2",   Device::Status::Ok},
    {"Design Ficus",       "HQ / Floor 3",   Device::Status::Ok},
    {"Warehouse Aloe",     "Logistics",      Device::Status::Warning},
    {"Boardroom Calathea", "HQ / Floor 5",   Device::Status::Ok},
};
} // namespace

void FleetModel::init() {
    devices_.clear();
    int i = 0;
    for (const Seed& s : kSeeds) {
        Device d;
        char id[16];
        std::snprintf(id, sizeof(id), "plant-%02d", i + 1);
        d.id = id;
        d.name = s.name;
        d.location = s.location;
        d.firmware = "2.4.1";
        d.status = s.status;
        d.phase = static_cast<float>(i) * 0.7f;
        d.soil_moisture = 0.45f + 0.1f * std::sin(d.phase);
        d.light = 0.5f;
        d.battery = (s.status == Device::Status::Offline) ? 0.0f : 0.6f + 0.03f * i;
        if (d.battery > 1.0f) d.battery = 1.0f;
        d.cpu = 0.15f;
        devices_.push_back(d);
        ++i;
    }

    // Pre-warm the telemetry histories so the charts read as live immediately
    // instead of ramping up from an empty (flat) buffer.
    for (int k = 0; k < Series::kLen; ++k) {
        float t = static_cast<float>(k) * 0.06f;
        frame_time_.push(15.0f + 2.5f * std::sin(t) + 1.5f * std::sin(t * 3.1f));
        net_latency_.push(45.0f + 16.0f * std::sin(t * 0.7f) + 5.0f * std::sin(t * 2.6f));
        cpu_load_.push(0.32f + 0.10f * std::sin(t) + 0.04f * std::sin(t * 4.0f));
        soil_avg_.push(0.50f + 0.14f * std::sin(t * 0.5f));
    }
}

void FleetModel::update(float dt, float frame_ms) {
    clock_ += dt;

    float soil_sum = 0.0f;
    float cpu_sum = 0.0f;
    for (Device& d : devices_) {
        if (d.status == Device::Status::Offline) {
            d.cpu = 0.0f;
            continue;
        }
        d.soil_moisture = 0.45f + 0.18f * std::sin(clock_ * 0.25f + d.phase);
        d.light = 0.55f + 0.30f * std::sin(clock_ * 0.15f + d.phase * 1.3f);
        d.cpu = 0.20f + 0.15f * std::fabs(std::sin(clock_ * 0.9f + d.phase));
        if (d.status == Device::Status::Error) d.cpu += 0.4f;
        if (d.cpu > 1.0f) d.cpu = 1.0f;
        soil_sum += d.soil_moisture;
        cpu_sum += d.cpu;
    }

    const float n = devices_.empty() ? 1.0f : static_cast<float>(devices_.size());
    frame_time_.push(frame_ms);
    cpu_load_.push(cpu_sum / n);
    soil_avg_.push(soil_sum / n);
    net_latency_.push(38.0f + 22.0f * std::fabs(std::sin(clock_ * 0.7f)) +
                      6.0f * std::sin(clock_ * 4.3f));

    // A gently breathing job queue.
    queue_depth_ = static_cast<int>(3.0f + 3.0f * std::sin(clock_ * 0.5f) +
                                    2.0f * std::sin(clock_ * 1.7f));
    if (queue_depth_ < 0) queue_depth_ = 0;
}

int FleetModel::online_count() const {
    int n = 0;
    for (const Device& d : devices_) {
        if (d.status != Device::Status::Offline) ++n;
    }
    return n;
}

int FleetModel::alert_count() const {
    int n = 0;
    for (const Device& d : devices_) {
        if (d.status == Device::Status::Warning || d.status == Device::Status::Error) ++n;
    }
    return n;
}

const char* FleetModel::status_label(Device::Status s) {
    switch (s) {
        case Device::Status::Ok: return "online";
        case Device::Status::Warning: return "warning";
        case Device::Status::Error: return "error";
        case Device::Status::Offline: return "offline";
    }
    return "unknown";
}

} // namespace empower
