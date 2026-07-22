#pragma once

#include <cstddef>
#include <string>

namespace empower {

// Observes the SDK retry outbox on disk (.sentry-native/cache/) for UI badges.
// Does not cache or upload — that remains the SDK's job. This only counts
// retry-queue envelope files so Chaos Lab can show queue depth.
class OfflineQueueMonitor {
public:
    // Where the SDK stores its database (same path passed to sentry_options).
    static void configure(std::string database_path);

    // Retry-queue depth: <ts>-<n>-<uuid>.envelope files under database/cache/.
    // Rescans disk at most a few times per second.
    static std::size_t queued_count();

private:
    static void refresh();

    static std::string s_database_path;
    static std::size_t s_queued_count;
    static double s_scanned_at;
};

} // namespace empower
