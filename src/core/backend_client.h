#pragma once

#include <string>

namespace empower {

class ConsoleLog;

struct BackendResult {
    long status = 0;
    bool ok = false;
    std::string body;
};

// Performs a checkout against the Empower Plant backend as a Sentry performance
// transaction, propagating the trace (sentry-trace / baggage headers) so the
// request continues into the backend's own Sentry instrumentation - producing a
// distributed trace that spans the native client and the Flask service.
//
// base_url defaults to the shared Empower Plant Flask backend when empty.
BackendResult checkout(const std::string& base_url, ConsoleLog* console);

} // namespace empower
