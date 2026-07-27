#include "chaos/chaos.h"

#include "app/console_log.h"
#include "core/backend_client.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include <sentry.h>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <sys/mman.h>
#endif

#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#  define MAP_ANONYMOUS MAP_ANON
#endif

#if defined(_MSC_VER)
#  define EMPOWER_NOINLINE __declspec(noinline)
#else
#  define EMPOWER_NOINLINE __attribute__((noinline))
#endif

namespace empower {

namespace {

void breadcrumb(const char* category, const char* message) {
    sentry_value_t bc = sentry_value_new_breadcrumb("default", message);
    sentry_value_set_by_key(bc, "category", sentry_value_new_string(category));
    sentry_add_breadcrumb(bc);
}

void set_device_context() {
    sentry_value_t dev = sentry_value_new_object();
    sentry_value_set_by_key(dev, "id", sentry_value_new_string("plant-05"));
    sentry_value_set_by_key(dev, "name", sentry_value_new_string("Server Room Cactus"));
    sentry_value_set_by_key(dev, "model", sentry_value_new_string("EmpowerPlant Pro"));
    sentry_value_set_by_key(dev, "firmware", sentry_value_new_string("2.4.1"));
    sentry_value_set_by_key(dev, "uptime_hours", sentry_value_new_int32(372));
    sentry_set_context("device", dev);
}

// Sets up the common scope for a scenario and logs the opening breadcrumb.
// Crash/hang events read global scope at capture time; chaos.scenario uses a
// crash- prefix. Handled events override with soft- on a local scope.
void arm(const char* id, const char* transaction, const char* opening,
         ConsoleLog* console, ConsoleLog::Level level) {
    const std::string scenario_tag = std::string("crash-") + id;
    sentry_set_tag("chaos.scenario", scenario_tag.c_str());
    sentry_set_transaction(transaction);
    // Deterministic grouping: every run of a scenario collapses into one issue
    // (so the convoluted-chain crash, whose crash site varies, stays a single
    // issue that Seer can analyze consistently).
    sentry_set_fingerprint(id, NULL);
    set_device_context();
    breadcrumb("chaos", opening);

    sentry_value_t log_attrs = sentry_value_new_object();
    sentry_value_set_by_key(log_attrs, "scenario",
                            sentry_value_new_attribute(sentry_value_new_string(id), nullptr));
    sentry_log_warn("chaos scenario '%s' triggered: %s", log_attrs, id, opening);
    if (console) console->push(level, "chaos", opening);
}

// Never runs after a crash, but shows capture_event_with_scope cannot label the crash.
void dead_capture_with_local_scope(sentry_scope_t* scope) {
    sentry_value_t ev = sentry_value_new_message_event(
        SENTRY_LEVEL_DEBUG, "chaos",
        "unreachable — local scope is never applied to crash events");
    sentry_capture_event_with_scope(ev, scope);
}

// ----- null dereference ---------------------------------------------------
EMPOWER_NOINLINE void read_device_register(volatile int* reg) {
    breadcrumb("driver", "reading device status register");
    volatile int v = *reg; // reg == nullptr
    (void)v;
}

void scenario_null_deref() {
    sentry_scope_t* scope = sentry_local_scope_new();
    sentry_scope_set_tag(scope, "crash.scenario", "will never show up on the crash issue");
    int* reg = nullptr;
    read_device_register(reg);
    dead_capture_with_local_scope(scope);
}

// ----- use-after-free across threads --------------------------------------
EMPOWER_NOINLINE void sample_freed_buffer(char* buffer) {
    breadcrumb("render", "render thread sampling device buffer");
    volatile char c = buffer[4096]; // buffer was freed (and unmapped)
    (void)c;
}

char* map_buffer(size_t n) {
#if defined(_WIN32)
    return static_cast<char*>(VirtualAlloc(nullptr, n, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
#else
    void* p = mmap(nullptr, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? nullptr : static_cast<char*>(p);
#endif
}

void unmap_buffer(char* p, size_t n) {
#if defined(_WIN32)
    (void)n;
    VirtualFree(p, 0, MEM_RELEASE);
#else
    munmap(p, n);
#endif
}

void scenario_use_after_free() {
    sentry_scope_t* scope = sentry_local_scope_new();
    sentry_scope_set_tag(scope, "crash.scenario", "will never show up on the crash issue");
    // The device sample buffer is mapped directly from the OS, so releasing it
    // unmaps the pages and a later access faults deterministically (a realistic
    // UAF surfaced on a different thread than the one that released it).
    const size_t n = 1 * 1024 * 1024;
    char* buffer = map_buffer(n);
    std::memset(buffer, 0xAB, n);
    breadcrumb("worker", "ingest worker releasing device sample buffer");
    unmap_buffer(buffer, n);
    std::thread reader(sample_freed_buffer, buffer);
    reader.join();
    dead_capture_with_local_scope(scope);
}

// ----- stack overflow -----------------------------------------------------
EMPOWER_NOINLINE int resolve_dependencies(volatile int depth) {
    volatile char frame[256];
    frame[0] = static_cast<char>(depth);
    // The added frame[0] read defeats tail-call optimization.
    return resolve_dependencies(depth + 1) + frame[0];
}

void scenario_stack_overflow() {
    sentry_scope_t* scope = sentry_local_scope_new();
    sentry_scope_set_tag(scope, "crash.scenario", "will never show up on the crash issue");
    breadcrumb("scheduler", "resolving device dependency graph");
    volatile int sink = resolve_dependencies(0);
    (void)sink;
    dead_capture_with_local_scope(scope);
}

// ----- integer divide by zero ---------------------------------------------
EMPOWER_NOINLINE int compute_yield_per_plant(volatile int plants) {
    breadcrumb("analytics", "computing yield per plant");
    volatile int total_yield = 1000;
    int q = total_yield / plants; // SIGFPE on x86 when plants == 0
    // On architectures where integer divide-by-zero does not trap (e.g. arm64
    // returns 0), the bogus quotient is used as an address and faults here.
    volatile int* p = reinterpret_cast<int*>(static_cast<intptr_t>(q));
    return *p;
}

void scenario_divide_by_zero() {
    sentry_scope_t* scope = sentry_local_scope_new();
    sentry_scope_set_tag(scope, "crash.scenario", "will never show up on the crash issue");
    volatile int result = compute_yield_per_plant(0);
    (void)result;
    dead_capture_with_local_scope(scope);
}

// ----- heap corruption ----------------------------------------------------
EMPOWER_NOINLINE void decode_sensor_frame(char* out, size_t out_len) {
    breadcrumb("parser", "decoding sensor frame into buffer");
    // Off-by-a-lot: writes far past the allocation, smashing heap metadata.
    std::memset(out, 0xEE, out_len);
}

void scenario_heap_corruption() {
    sentry_scope_t* scope = sentry_local_scope_new();
    sentry_scope_set_tag(scope, "crash.scenario", "will never show up on the crash issue");
    char* buf = static_cast<char*>(std::malloc(32));
    decode_sensor_frame(buf, 4096);
    breadcrumb("parser", "releasing decoded frame buffer");
    std::free(buf); // allocator detects the corruption and aborts
    dead_capture_with_local_scope(scope);
}

// ----- assertion / abort --------------------------------------------------
EMPOWER_NOINLINE void verify_firmware_signature(const char* version) {
    breadcrumb("ota", "verifying staged firmware signature");
    if (std::strcmp(version, "2.4.1") != 0) {
        sentry_value_t ev = sentry_value_new_message_event(
            SENTRY_LEVEL_FATAL, "ota",
            "firmware signature check failed - aborting");
        sentry_scope_t* scope = sentry_local_scope_new();
        const std::string scenario_tag = std::string("soft-") + "assert-fail";
        sentry_scope_set_tag(scope, "chaos.scenario", scenario_tag.c_str());
        sentry_capture_event_with_scope(ev, scope);
        std::abort();
    }
}

void scenario_assert_fail() {
    sentry_scope_t* scope = sentry_local_scope_new();
    sentry_scope_set_tag(scope, "crash.scenario", "will never show up on the crash issue");
    verify_firmware_signature("0.0.0-tampered");
    dead_capture_with_local_scope(scope);
}

// ----- GPU stress / device lost -------------------------------------------
EMPOWER_NOINLINE void submit_render_commands(volatile float* vertex_buffer) {
    breadcrumb("gpu", "submitting draw call to render queue");
    vertex_buffer[0] = 1.0f; // vertex_buffer == nullptr
}

void scenario_gpu_stress() {
    sentry_scope_t* scope = sentry_local_scope_new();
    sentry_scope_set_tag(scope, "crash.scenario", "will never show up on the crash issue");
    sentry_value_t gpu = sentry_value_new_object();
    sentry_value_set_by_key(gpu, "name", sentry_value_new_string("EmpowerPlant Display Adapter"));
    sentry_value_set_by_key(gpu, "api_type", sentry_value_new_string("OpenGL"));
    sentry_value_set_by_key(gpu, "vendor_name", sentry_value_new_string("Mesa"));
    sentry_set_context("gpu", gpu);
    breadcrumb("gpu", "render thread overloaded - device lost");
    float* vertex_buffer = nullptr;
    submit_render_commands(vertex_buffer);
    dead_capture_with_local_scope(scope);
}

// ----- the convoluted chain (corruption now, crash later, elsewhere) ------
struct DeviceCalibration {
    char profile[16];
    std::function<void(int)> on_sample;
};

EMPOWER_NOINLINE void parse_calibration_profile(DeviceCalibration* cal,
                                                const uint8_t* blob, size_t n) {
    breadcrumb("calibration", "parsing device calibration profile");
    // Bug: copies the whole blob into a fixed 16-byte field, overflowing into
    // the adjacent on_sample callback. No crash here - the damage is silent.
    std::memcpy(cal->profile, blob, n);
}

EMPOWER_NOINLINE void flash_worker_apply_calibration(DeviceCalibration* cal,
                                                     std::vector<uint8_t> blob) {
    breadcrumb("firmware", "flash worker applying staged calibration");
    parse_calibration_profile(cal, blob.data(), blob.size());
    breadcrumb("firmware", "flash worker finished (no error observed)");
}

EMPOWER_NOINLINE void dispatch_sensor_sample(DeviceCalibration* cal, int value) {
    breadcrumb("sensor", "sensor pipeline dispatching sample to callback");
    cal->on_sample(value); // callback pointer was corrupted earlier -> crash
}

void scenario_convoluted() {
    sentry_scope_t* scope = sentry_local_scope_new();
    sentry_scope_set_tag(scope, "crash.scenario", "will never show up on the crash issue");
    auto* cal = new DeviceCalibration();
    cal->on_sample = [](int sample) { (void)sample; };

    std::vector<uint8_t> blob(64, 0xC0);
    sentry_attachment_t* attachment = sentry_attach_bytes(
        reinterpret_cast<const char*>(blob.data()), blob.size(),
        "calibration-blob.bin");
    sentry_attachment_set_type(attachment, SENTRY_ATTACHMENT_TYPE_GENERIC);
    sentry_attachment_set_content_type(attachment, "application/octet-stream");

    breadcrumb("firmware", "operator triggered firmware flash with calibration");
    // The corruption happens off the UI thread, in the flash worker...
    std::thread worker(flash_worker_apply_calibration, cal, blob);
    worker.join();

    // ...and only much later, an unrelated sensor sample invokes the now-corrupt
    // callback and crashes far from the actual bug.
    breadcrumb("sensor", "new sensor sample arrived for device");
    dispatch_sensor_sample(cal, 42);
    dead_capture_with_local_scope(scope);
}

// ----- app hang (bounded, so a live demo recovers) ------------------------
// This is the demo's deliberately data-heavy event: before blocking the main
// thread we load the scope with a large attachment, a detailed context object,
// extra tags and a long breadcrumb trail, so the resulting app-hang event
// carries a rich payload to explore in Sentry.
void scenario_app_hang(ConsoleLog* console) {
    sentry_set_tag("hang.subsystem", "telemetry-flush");
    sentry_set_tag("hang.trigger", "synchronous-disk-flush");

    // A sizable diagnostic dump attached to the event (~256 KB).
    std::vector<char> dump(256 * 1024);
    // For TUS large uploads (>= 100 MiB), enable large attachments in
    // sentry_manager.cpp and attach a file via sentry_attach_file instead.
    for (size_t i = 0; i < dump.size(); ++i)
        dump[i] = static_cast<char>('A' + (i % 26));
    sentry_attach_bytes(dump.data(), dump.size(), "telemetry-flush-dump.txt");

    // A detailed context describing the stuck flush.
    sentry_value_t flush = sentry_value_new_object();
    sentry_value_set_by_key(flush, "queue_depth", sentry_value_new_int32(18432));
    sentry_value_set_by_key(flush, "pending_bytes", sentry_value_new_int32(7 * 1024 * 1024));
    sentry_value_set_by_key(flush, "destination", sentry_value_new_string("/var/empower/telemetry.wal"));
    sentry_value_set_by_key(flush, "fsync_mode", sentry_value_new_string("full"));
    sentry_value_set_by_key(flush, "blocked_thread", sentry_value_new_string("main/ui"));
    sentry_set_context("telemetry_flush", flush);

    // A long breadcrumb trail of the devices the flush is draining.
    static const char* devices[] = {
        "plant-01", "plant-02", "plant-03", "plant-05", "plant-08",
        "plant-13", "plant-21", "plant-34", "plant-55", "plant-89",
    };
    for (const char* d : devices)
        breadcrumb("telemetry", (std::string("flushing telemetry for ") + d).c_str());

    breadcrumb("ui", "main thread entering long synchronous flush");

    sentry_value_t flush_attrs = sentry_value_new_object();
    sentry_value_set_by_key(flush_attrs, "subsystem",
        sentry_value_new_attribute(sentry_value_new_string("telemetry-flush"), nullptr));
    // METRIC: telemetry.flush.queue_depth — one-shot spike matching the app-hang event context.
    sentry_metrics_gauge("telemetry.flush.queue_depth", 18432, "none", flush_attrs);

    for (int i = 0; i < 8; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        breadcrumb("ui", "still flushing... (main thread blocked)");
    }
    if (console)
        console->push(ConsoleLog::Level::Warn, "chaos",
                      "main thread unblocked after 8s hang");
}

// ----- real distributed-trace checkout to the Flask backend ---------------
void scenario_backend_error(ConsoleLog* console) {
    breadcrumb("checkout", "ordering a replacement plant from the backend");
    const char* base = std::getenv("EMPOWER_BACKEND_URL");
    BackendResult r = checkout(base ? base : "", console);

    if (!r.ok) {
        char status[8];
        std::snprintf(status, sizeof(status), "%ld", r.status);
        sentry_value_t ev = sentry_value_new_message_event(
            SENTRY_LEVEL_ERROR, "checkout",
            "replacement-plant checkout failed at the Flask backend");

        sentry_scope_t* scope = sentry_local_scope_new();
        const std::string scenario_tag = std::string("soft-") + "backend-500";
        sentry_scope_set_tag(scope, "chaos.scenario", scenario_tag.c_str());
        sentry_scope_set_tag(scope, "backend", "flask");
        sentry_scope_set_tag(scope, "http.status_code", status);
        sentry_capture_event_with_scope(ev, scope);
        
        if (console)
            console->push(ConsoleLog::Level::Error, "chaos",
                          "checkout failed (HTTP " + std::string(status) +
                              ") - distributed trace + event captured");
    } else if (console) {
        console->push(ConsoleLog::Level::Info, "chaos",
                      "checkout ok (HTTP 200) - distributed trace captured");
    }
}

// ----- handled message -----------------------------------------------------
void scenario_message(ConsoleLog* console) {
    breadcrumb("ops", "operator requested manual status report");
    sentry_value_t ev = sentry_value_new_message_event(
        SENTRY_LEVEL_INFO, "ops",
        "manual fleet status report requested by operator");
    sentry_scope_t* scope = sentry_local_scope_new();
    const std::string scenario_tag = std::string("soft-") + "message";
    sentry_scope_set_tag(scope, "chaos.scenario", scenario_tag.c_str());
    sentry_capture_event_with_scope(ev, scope);
    if (console)
        console->push(ConsoleLog::Level::Info, "chaos",
                      "status report captured as a Sentry message");
}

} // namespace

const std::vector<ChaosScenario>& scenarios() {
    static const std::vector<ChaosScenario> list = {
        {"null-deref", "Null Dereference", "read through a null device handle", Severity::Crash},
        {"use-after-free", "Use After Free", "sample a freed buffer across threads", Severity::Crash},
        {"stack-overflow", "Stack Overflow", "runaway dependency recursion", Severity::Crash},
        {"divide-by-zero", "Divide By Zero", "yield-per-plant with zero plants", Severity::Crash},
        {"heap-corruption", "Heap Corruption", "off-by-one in the frame parser", Severity::Crash},
        {"assert-fail", "Assertion Abort", "invariant on bad firmware signature", Severity::Crash},
        {"gpu-stress", "GPU Device Lost", "pathological render-thread submit", Severity::Crash},
        {"convoluted", "Convoluted Chain", "corruption now, crash later, far away", Severity::Crash},
        {"app-hang", "App Hang", "block the main thread for 8 seconds", Severity::Warning},
        {"backend-500", "Backend Error", "failed checkout to Flask backend", Severity::Backend},
        {"message", "Handled Message", "capture a non-fatal status event", Severity::Message},
    };
    return list;
}

void trigger(const std::string& id, ConsoleLog* console) {
    // Complete the metrics trifecta (gauge/distribution already emit from the UI loop).
    sentry_value_t attrs = sentry_value_new_object();
    sentry_value_set_by_key(attrs, "scenario",
        sentry_value_new_attribute(sentry_value_new_string(id.c_str()), nullptr));
    // METRIC: chaos.scenarios_triggered — count per Chaos Lab button press, tagged by scenario id.
    sentry_metrics_count("chaos.scenarios_triggered", 1, attrs);

    if (id == "null-deref") { arm("null-deref", "device.poll", "polling device status register", console, ConsoleLog::Level::Error); scenario_null_deref(); }
    else if (id == "use-after-free") { arm("use-after-free", "ingest.sample", "sampling device buffer after release", console, ConsoleLog::Level::Error); scenario_use_after_free(); }
    else if (id == "stack-overflow") { arm("stack-overflow", "scheduler.resolve", "resolving device dependency graph", console, ConsoleLog::Level::Error); scenario_stack_overflow(); }
    else if (id == "divide-by-zero") { arm("divide-by-zero", "analytics.yield", "computing yield per plant", console, ConsoleLog::Level::Error); scenario_divide_by_zero(); }
    else if (id == "heap-corruption") { arm("heap-corruption", "parser.decode", "decoding sensor frame", console, ConsoleLog::Level::Error); scenario_heap_corruption(); }
    else if (id == "assert-fail") { arm("assert-fail", "ota.verify", "verifying firmware signature", console, ConsoleLog::Level::Error); scenario_assert_fail(); }
    else if (id == "gpu-stress") { arm("gpu-stress", "render.submit", "submitting render commands", console, ConsoleLog::Level::Error); scenario_gpu_stress(); }
    else if (id == "convoluted") { arm("convoluted", "firmware.flash", "flashing calibration to device", console, ConsoleLog::Level::Error); scenario_convoluted(); }
    else if (id == "app-hang") { arm("app-hang", "ui.flush", "starting long synchronous flush", console, ConsoleLog::Level::Warn); scenario_app_hang(console); }
    else if (id == "backend-500") { arm("backend-500", "checkout", "ordering a replacement plant", console, ConsoleLog::Level::Warn); scenario_backend_error(console); }
    else if (id == "message") { arm("message", "ops.report", "requesting a status report", console, ConsoleLog::Level::Info); scenario_message(console); }
}

} // namespace empower
