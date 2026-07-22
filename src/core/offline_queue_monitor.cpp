#include "core/offline_queue_monitor.h"

#include <chrono>
#include <filesystem>

namespace empower {

std::string OfflineQueueMonitor::s_database_path = ".sentry-native";
std::size_t OfflineQueueMonitor::s_queued_count = 0;
double OfflineQueueMonitor::s_scanned_at = 0;

void OfflineQueueMonitor::configure(std::string database_path) {
    if (!database_path.empty()) {
        s_database_path = std::move(database_path);
    }
    s_scanned_at = 0;
}

void OfflineQueueMonitor::refresh() {
    using clock = std::chrono::steady_clock;
    const double now =
        std::chrono::duration<double>(clock::now().time_since_epoch()).count();
    // Don't rescan every UI frame — a few times per second is enough for the badge.
    constexpr double kInterval = 0.4;
    if (now - s_scanned_at < kInterval) {
        return;
    }
    s_scanned_at = now;
    s_queued_count = 0;

    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path cache_dir = fs::path(s_database_path) / "cache";
    if (!fs::is_directory(cache_dir, ec)) {
        return;
    }
    for (const auto& entry : fs::directory_iterator(cache_dir, ec)) {
        if (ec || !entry.is_regular_file(ec)) continue;
        const std::string name = entry.path().filename().string();
        // Retry outbox only — ignore ALWAYS bare-<uuid>.envelope archives.
        if (name.size() > 45 && name[0] >= '0' && name[0] <= '9'
            && name.find(".envelope") != std::string::npos) {
            ++s_queued_count;
        }
    }
}

std::size_t OfflineQueueMonitor::queued_count() {
    refresh();
    return s_queued_count;
}

} // namespace empower
