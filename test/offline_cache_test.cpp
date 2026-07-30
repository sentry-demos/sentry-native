// offline_cache_test.cpp
//
// Purpose
// -------
// Several focused cases that prove "Go Offline" (consent revoke + cache_keep)
// leaves envelopes on disk under <database_path>/cache/:
//
//   1) soft message event while Offline
//   2) hard crash while Offline, no external crash reporter
//   3) hard crash while Offline, WITH an external crash reporter path set
//      (uses a stub reporter so CI/dev doesn't open a GUI)
//
// Case 3 matters for the Fleet GUI: it enables the desktop crash reporter.
// Upstream still launches that reporter while consent is revoked; with
// SENTRY_CACHE_KEEP_ALWAYS the SDK still writes a local cache copy before
// hand-off. This case asserts that copy exists (retry-queue and/or bare
// ALWAYS <uuid>.envelope).
//
// Run:  ./build/empower-test-offline
//   or: make test-offline

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <sentry.h>

#include "core/sentry_manager.h"
#include "core/platform.h"

namespace fs = std::filesystem;

namespace {

struct CrashChildArgs {
    std::string db;
    bool with_reporter = false;
    std::string reporter_path;
};

bool is_retry_queue_file(const std::string& name) {
    // <ts>-<count>-<uuid>.envelope — drainable outbox
    return name.size() > 45 && name[0] >= '0' && name[0] <= '9'
        && name.find(".envelope") != std::string::npos;
}

bool is_envelope_file(const std::string& name) {
    return name.size() > 9 && name.find(".envelope") != std::string::npos;
}

std::vector<fs::path> list_envelopes(const fs::path& cache_dir, bool retry_only) {
    std::vector<fs::path> out;
    std::error_code ec;
    if (!fs::is_directory(cache_dir, ec)) return out;
    for (const auto& e : fs::directory_iterator(cache_dir, ec)) {
        if (ec || !e.is_regular_file(ec)) continue;
        const std::string name = e.path().filename().string();
        if (retry_only) {
            if (is_retry_queue_file(name)) out.push_back(e.path());
        } else if (is_envelope_file(name)) {
            out.push_back(e.path());
        }
    }
    return out;
}

bool file_contains(const fs::path& path, const char* needle) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::string buf((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());
    return buf.find(needle) != std::string::npos;
}

bool any_contains(const std::vector<fs::path>& files, const char* needle) {
    for (const auto& p : files) {
        if (file_contains(p, needle)) return true;
    }
    return false;
}

void dump_cache(const fs::path& cache_dir) {
    std::error_code ec;
    for (const auto& p : list_envelopes(cache_dir, false)) {
        std::fprintf(stderr, "  - %s (%llu bytes)\n", p.filename().c_str(),
                     static_cast<unsigned long long>(fs::file_size(p, ec)));
    }
}

empower::SentryConfig make_cfg(const std::string& db, bool with_reporter,
                               const std::string& reporter_path) {
    empower::SentryConfig cfg;
    cfg.dsn = "http://publickey@127.0.0.1:9/1"; // unreachable; Offline blocks anyway
    cfg.database_path = db;
    cfg.environment = "test";
    cfg.component = "offline-cache-test";
    cfg.debug = false;
    cfg.crash_upload_sync = true;
    cfg.use_external_crash_reporter = with_reporter;
    cfg.crash_reporter_path = reporter_path;
    cfg.handler_path = empower::path_join(empower::executable_dir(),
#ifdef _WIN32
                                          "sentry-crash.exe"
#else
                                          "sentry-crash"
#endif
    );
    return cfg;
}

// Tiny stand-in for the desktop crash reporter so we don't open a GUI in tests.
// The SDK only needs a spawnable path; we assert caching, not reporter UX.
std::string write_stub_reporter(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
#if defined(_WIN32)
    const fs::path path = dir / "stub-crash-reporter.bat";
    std::ofstream out(path);
    out << "@echo off\r\nexit /b 0\r\n";
#else
    const fs::path path = dir / "stub-crash-reporter";
    std::ofstream out(path);
    out << "#!/bin/sh\nexit 0\n";
    out.close();
    fs::permissions(path,
                    fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec
                        | fs::perms::others_read | fs::perms::others_exec,
                    ec);
#endif
    return path.string();
}

fs::path fresh_db(const char* suffix) {
    std::error_code ec;
    const fs::path db =
        fs::temp_directory_path(ec) / (std::string("empower-offline-") + suffix);
    fs::remove_all(db, ec);
    fs::create_directories(db, ec);
    return db;
}

bool wait_for_fatal_in_cache(const fs::path& cache, std::size_t min_count,
                             bool retry_only, int attempts = 50) {
    for (int i = 0; i < attempts; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        auto files = list_envelopes(cache, retry_only);
        if (files.size() >= min_count && any_contains(files, "\"level\":\"fatal\"")) {
            return true;
        }
        // With an external reporter + ALWAYS, the copy may be a bare uuid file.
        if (!retry_only && any_contains(list_envelopes(cache, false), "\"level\":\"fatal\"")) {
            return true;
        }
    }
    return false;
}

int run_crash_child(const CrashChildArgs& args) {
    auto cfg = make_cfg(args.db, args.with_reporter, args.reporter_path);
    if (!empower::SentryManager::init(cfg)) {
        std::fprintf(stderr, "offline-cache-test: crash-child init failed\n");
        return 2;
    }
    empower::SentryManager::set_offline(true);
    sentry_set_tag("offline_cache_test",
                   args.with_reporter ? "crash-with-reporter" : "crash-no-reporter");

    volatile int* p = nullptr;
    *p = 42; // intentional SIGSEGV
    return 0;
}

int spawn_crash_child(const char* self_path, const CrashChildArgs& args) {
    std::string cmd = std::string("\"") + self_path + "\" --crash-child \"" + args.db + "\"";
    if (args.with_reporter) {
        cmd += " --reporter \"";
        cmd += args.reporter_path;
        cmd += "\"";
    }
    std::printf("offline-cache-test: spawning: %s\n", cmd.c_str());
    return std::system(cmd.c_str());
}

// --- Case 1: soft message while Offline -----------------------------------
int case_soft_message_offline() {
    std::printf("\n== case: soft message while Offline ==\n");
    const fs::path db = fresh_db("soft");
    const fs::path cache = db / "cache";

    if (!empower::SentryManager::init(make_cfg(db.string(), false, ""))) {
        std::fprintf(stderr, "FAIL: init\n");
        return 1;
    }
    empower::SentryManager::set_offline(true);
    if (!empower::SentryManager::is_offline()) {
        std::fprintf(stderr, "FAIL: set_offline did not stick\n");
        return 1;
    }

    const char* kMessage = "offline cache test: soft message queued";
    sentry_capture_event(sentry_value_new_message_event(
        SENTRY_LEVEL_ERROR, "offline-cache-test", kMessage));
    sentry_flush(2000);

    auto files = list_envelopes(cache, true);
    empower::SentryManager::shutdown();

    if (files.empty() || !any_contains(files, kMessage)) {
        std::fprintf(stderr, "FAIL: soft message not in retry-queue cache\n");
        dump_cache(cache);
        return 1;
    }
    std::printf("OK: soft message cached (%zu retry file(s))\n", files.size());
    return 0;
}

// --- Case 2: hard crash while Offline, no external reporter ---------------
int case_crash_offline_no_reporter(const char* self_path) {
    std::printf("\n== case: crash while Offline (no external reporter) ==\n");
    const fs::path db = fresh_db("crash-no-reporter");
    const fs::path cache = db / "cache";

    CrashChildArgs args;
    args.db = db.string();
    args.with_reporter = false;
    const std::size_t before = list_envelopes(cache, true).size();
    (void)spawn_crash_child(self_path, args);

    // Consent-gated capture_envelope path → retry-queue fatal envelope.
    if (!wait_for_fatal_in_cache(cache, before + 1, true)) {
        std::fprintf(stderr, "FAIL: no fatal retry-queue envelope after crash\n");
        dump_cache(cache);
        return 1;
    }
    std::printf("OK: crash cached in retry queue without external reporter\n");
    return 0;
}

// --- Case 3: hard crash while Offline, WITH external reporter path --------
int case_crash_offline_with_reporter(const char* self_path) {
    std::printf("\n== case: crash while Offline (with external reporter) ==\n");
    const fs::path db = fresh_db("crash-with-reporter");
    const fs::path cache = db / "cache";
    const std::string stub = write_stub_reporter(db / "stub-reporter");

    CrashChildArgs args;
    args.db = db.string();
    args.with_reporter = true;
    args.reporter_path = stub;
    (void)spawn_crash_child(self_path, args);

    // Stock native SDK launches the reporter even when Offline; ALWAYS still
    // writes a local copy (often bare <uuid>.envelope). Accept any .envelope
    // that contains a fatal event — retry-queue or ALWAYS archive.
    if (!wait_for_fatal_in_cache(cache, 1, false)) {
        std::fprintf(stderr,
                     "FAIL: no fatal envelope in cache with external reporter set\n");
        dump_cache(cache);
        return 1;
    }
    std::printf("OK: crash still present in cache/ with external reporter path set\n");
    return 0;
}

int run_all(const char* self_path) {
    int failed = 0;
    failed += case_soft_message_offline();
    failed += case_crash_offline_no_reporter(self_path);
    failed += case_crash_offline_with_reporter(self_path);

    std::printf("\n");
    if (failed == 0) {
        std::printf("offline-cache-test: all cases passed\n");
        return 0;
    }
    std::fprintf(stderr, "offline-cache-test: %d case(s) failed\n", failed);
    return 1;
}

} // namespace

int main(int argc, char** argv) {
    // Child:  --crash-child <db> [--reporter <path>]
    if (argc >= 3 && std::strcmp(argv[1], "--crash-child") == 0) {
        CrashChildArgs args;
        args.db = argv[2];
        for (int i = 3; i + 1 < argc; ++i) {
            if (std::strcmp(argv[i], "--reporter") == 0) {
                args.with_reporter = true;
                args.reporter_path = argv[++i];
            }
        }
        return run_crash_child(args);
    }
    return run_all(argv[0]);
}
