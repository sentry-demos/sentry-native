// Entry point for the Empower Plant Fleet Control Center GUI.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

// GLFW pulls in the platform OpenGL header (gl.h), which declares the handful
// of legacy entry points used here (glViewport / glClear / glClearColor).
#include <GLFW/glfw3.h>

#if defined(EMPOWER_HAVE_STB)
#  define STB_IMAGE_WRITE_IMPLEMENTATION
#  include "stb_image_write.h"
#endif

#include "app/console_log.h"
#include "app/fleet_model.h"
#include "app/theme.h"
#include "app/ui.h"
#include "chaos/chaos.h"
#include "core/sentry_manager.h"

#if defined(EMPOWER_HAVE_REPLAY)
#  include <chrono>
#  include "app/replay_recorder.h"
#endif

#include <sentry.h>

namespace {

const char* env_or(const char* name, const char* fallback) {
    const char* v = std::getenv(name);
    return (v && *v) ? v : fallback;
}

// Extracts the ingest host from a DSN for display (no secret key).
std::string dsn_host(const std::string& dsn) {
    auto at = dsn.find('@');
    if (at == std::string::npos) return "";
    auto slash = dsn.find('/', at);
    return dsn.substr(at + 1, slash == std::string::npos ? std::string::npos : slash - at - 1);
}

void glfw_error(int code, const char* desc) {
    std::fprintf(stderr, "glfw error %d: %s\n", code, desc);
}

#if defined(EMPOWER_HAVE_STB)
void save_screenshot(GLFWwindow* w, const std::string& path) {
    int width = 0, height = 0;
    glfwGetFramebufferSize(w, &width, &height);
    std::vector<unsigned char> px(static_cast<size_t>(width) * height * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    stbi_flip_vertically_on_write(1);
    stbi_write_png(path.c_str(), width, height, 4, px.data(), width * 4);
}
#endif

} // namespace

int main(int argc, char** argv) {
    // Offscreen screenshot mode: render a few frames into a hidden window and
    // write a PNG. Used for visual review and as a non-Windows screenshot path.
    std::string shot_path;
    std::string crash_id;
    int shot_frames = 90;
    int start_page = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--shot") == 0 && i + 1 < argc) shot_path = argv[++i];
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) shot_frames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--page") == 0 && i + 1 < argc) start_page = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--crash") == 0 && i + 1 < argc) crash_id = argv[++i];
    }
    const bool shot_mode = !shot_path.empty();

    // Headless fault trigger: init Sentry, run one scenario, exit (or crash).
    // No window is created. Useful for verifying a scenario and for CI.
    if (!crash_id.empty()) {
        empower::SentryConfig cfg;
        cfg.environment = env_or("SENTRY_ENVIRONMENT", "demo");
        cfg.component = "fleet";
        cfg.debug = env_or("EMPOWER_DEBUG", "")[0] != '\0';
        empower::SentryManager::init(cfg);
        empower::ConsoleLog console;
        std::fprintf(stderr, "triggering scenario: %s\n", crash_id.c_str());
        empower::trigger(crash_id, &console);
        empower::SentryManager::shutdown();
        return 0;
    }

    glfwSetErrorCallback(glfw_error);
    if (!glfwInit()) {
        std::fprintf(stderr, "failed to init GLFW\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    if (shot_mode) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(
        1320, 840, "Empower Plant - Fleet Control Center", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "failed to create window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // a demo: don't persist window layout
    empower::theme::apply();
    float xscale = 1.0f, yscale = 1.0f;
    glfwGetWindowContentScale(window, &xscale, &yscale);
    empower::theme::load_fonts(xscale);
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 150");

    // Initialize Sentry with the same wiring shared by every binary.
    const std::string dsn = env_or("SENTRY_DSN", "");
    empower::SentryConfig cfg;
    cfg.environment = env_or("SENTRY_ENVIRONMENT", "production");
    cfg.component = "fleet";
    cfg.debug = env_or("EMPOWER_DEBUG", "")[0] != '\0';
    cfg.use_external_crash_reporter = false; // daemon submits crashes directly
    bool sentry_ok = empower::SentryManager::init(cfg);

    // Real GPU context from the live OpenGL renderer, so even non-GPU crashes
    // report the adapter in use.
    if (sentry_ok) {
        auto gl_str = [](unsigned int name) -> const char* {
            const GLubyte* s = glGetString(name);
            return s ? reinterpret_cast<const char*>(s) : "unknown";
        };
        sentry_value_t gpu = sentry_value_new_object();
        sentry_value_set_by_key(gpu, "name", sentry_value_new_string(gl_str(GL_RENDERER)));
        sentry_value_set_by_key(gpu, "vendor_name", sentry_value_new_string(gl_str(GL_VENDOR)));
        sentry_value_set_by_key(gpu, "version", sentry_value_new_string(gl_str(GL_VERSION)));
        sentry_value_set_by_key(gpu, "api_type", sentry_value_new_string("OpenGL"));
        sentry_set_context("gpu", gpu);
    }

    // Attach a live screenshot of the UI. The SDK only captures screenshots
    // automatically on Windows; here we keep a fresh PNG of the dashboard on
    // disk and attach it so crashes carry a screenshot on every platform.
    const std::string screenshot_path = cfg.database_path + "/screenshot.png";
#if defined(EMPOWER_HAVE_STB)
    if (sentry_ok) sentry_attach_file(screenshot_path.c_str());
#endif

#if defined(EMPOWER_HAVE_REPLAY)
    // Rolling session replay: keep re-staging the last ~15s of the dashboard
    // as an mp4 in <database>/replays/. On a crash the sentry-crash daemon
    // wraps the staged clip in a replay_video envelope (matched to the crash
    // event via contexts.replay.replay_id) and sends it in the same session.
    empower::ReplayRecorder replay;
    if (sentry_ok) {
        sentry_uuid_t replay_uuid = sentry_uuid_new_v4();
        char uuid_str[37];
        sentry_uuid_as_string(&replay_uuid, uuid_str);
        std::string replay_id;
        for (const char* c = uuid_str; *c; ++c) {
            if (*c != '-') replay_id += *c;
        }
        empower::ReplayRecorder::Config rcfg;
        rcfg.replays_dir = cfg.database_path + "/replays";
        rcfg.replay_id = replay_id;
        // Quality knobs, overridable for experiments (see README).
        rcfg.max_width = std::atoi(env_or("EMPOWER_REPLAY_MAX_WIDTH", "1280"));
        rcfg.qp = std::atoi(env_or("EMPOWER_REPLAY_QP", "23"));
        rcfg.capture_fps =
            static_cast<float>(std::atof(env_or("EMPOWER_REPLAY_FPS", "4")));
        rcfg.window_seconds = static_cast<float>(
            std::atof(env_or("EMPOWER_REPLAY_WINDOW_SEC", "15")));
        if (replay.init(rcfg)) {
            sentry_value_t replay_ctx = sentry_value_new_object();
            sentry_value_set_by_key(replay_ctx, "replay_id",
                                    sentry_value_new_string(replay_id.c_str()));
            sentry_set_context("replay", replay_ctx);
        }
    }
    std::vector<unsigned char> replay_px;
#endif

    empower::FleetModel fleet;
    fleet.init();
    empower::ConsoleLog console;
    console.push(empower::ConsoleLog::Level::Info, "boot",
                 "Fleet Control Center online");
    console.push(sentry_ok ? empower::ConsoleLog::Level::Info
                           : empower::ConsoleLog::Level::Warn,
                 "sentry",
                 sentry_ok ? "Sentry native backend initialized"
                           : "Sentry init failed (events will not be sent)");
#if defined(EMPOWER_HAVE_REPLAY)
    if (replay.active()) {
        console.push(empower::ConsoleLog::Level::Info, "replay",
                     "session replay armed (15s rolling window)");
    }
#endif
    // Seed the Telemetry feed with representative recent activity.
    console.push(empower::ConsoleLog::Level::Info, "session", "session started");
    console.push(empower::ConsoleLog::Level::Info, "metric", "sent fleet.devices_online = 11");
    console.push(empower::ConsoleLog::Level::Info, "log", "fleet heartbeat: 11 online, queue 6");
    console.push(empower::ConsoleLog::Level::Debug, "sentry", "flushed 4 envelopes");
    console.push(empower::ConsoleLog::Level::Info, "metric", "sent fleet.frame_time = 16.4 ms");

    empower::AppState state;
    state.fleet = &fleet;
    state.console = &console;
    state.environment = cfg.environment;
    state.release = empower::SentryManager::release();
    state.dsn_host = dsn_host(dsn);
    state.dsn_configured = !dsn.empty();
    state.page = start_page;
    state.on_chaos = [&console](const std::string& id) {
        empower::trigger(id, &console);
    };

    double last = glfwGetTime();
    int frame = 0;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ++frame;

        double now = glfwGetTime();
        float dt = shot_mode ? (1.0f / 60.0f) : static_cast<float>(now - last);
        last = now;
        fleet.update(dt, dt * 1000.0f);

        // Keep the app-hang watchdog informed that the UI thread is alive, and
        // periodically emit runtime metrics / a heartbeat log.
        if (sentry_ok && !shot_mode) {
            empower::SentryManager::app_hang_heartbeat();
            if (frame % 180 == 0) {
                // Per-metric attribute dimensions (on top of the global ones).
                auto attr1 = [](const char* k, sentry_value_t v) {
                    sentry_value_t o = sentry_value_new_object();
                    sentry_value_set_by_key(o, k, sentry_value_new_attribute(v, nullptr));
                    return o;
                };
                int fleet_size = static_cast<int>(fleet.devices().size());
                sentry_metrics_distribution("fleet.frame_time", dt * 1000.0, "millisecond",
                                            attr1("renderer", sentry_value_new_string("opengl")));
                sentry_metrics_gauge("fleet.cpu_load", fleet.cpu_load().latest(), "ratio",
                                     attr1("renderer", sentry_value_new_string("opengl")));
                sentry_metrics_gauge("fleet.job_queue_depth", fleet.queue_depth(), "none",
                                     attr1("fleet_size", sentry_value_new_int32(fleet_size)));
                sentry_metrics_gauge("fleet.devices_online", fleet.online_count(), "none",
                                     attr1("fleet_size", sentry_value_new_int32(fleet_size)));

                sentry_value_t hb = sentry_value_new_object();
                sentry_value_set_by_key(hb, "devices_online",
                    sentry_value_new_attribute(sentry_value_new_int32(fleet.online_count()), nullptr));
                sentry_value_set_by_key(hb, "queue_depth",
                    sentry_value_new_attribute(sentry_value_new_int32(fleet.queue_depth()), nullptr));
                sentry_log_info("fleet heartbeat: %d devices online, queue depth %d",
                                hb, fleet.online_count(), fleet.queue_depth());

                char line[96];
                std::snprintf(line, sizeof(line), "sent fleet.frame_time = %.1f ms",
                              dt * 1000.0);
                console.push(empower::ConsoleLog::Level::Debug, "metric", line);
                std::snprintf(line, sizeof(line), "fleet heartbeat: %d online, queue %d",
                              fleet.online_count(), fleet.queue_depth());
                console.push(empower::ConsoleLog::Level::Info, "log", line);
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        empower::render_ui(state);

        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        const ImVec4& bg = empower::theme::color::bg;
        glClearColor(bg.x, bg.y, bg.z, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

#if defined(EMPOWER_HAVE_STB)
        // Refresh the attached UI screenshot a couple of times a second.
        if (sentry_ok && !shot_mode && frame % 120 == 30) {
            save_screenshot(window, screenshot_path);
        }
#endif

#if defined(EMPOWER_HAVE_REPLAY)
        // Feed the rolling replay a few frames per second (frame-count paced
        // in shot mode, where offscreen rendering outruns the wall clock).
        if (replay.active()) {
            const double now_unix
                = std::chrono::duration<double>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
            if (shot_mode ? frame % 30 == 0 : replay.frame_due(now_unix)) {
                glfwGetFramebufferSize(window, &w, &h);
                replay_px.resize(static_cast<size_t>(w) * h * 4);
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE,
                             replay_px.data());
                replay.submit_frame(replay_px.data(), w, h, now_unix);
            }
        }
#endif
        glfwSwapBuffers(window);

        if (shot_mode && frame >= shot_frames) {
#if defined(EMPOWER_HAVE_STB)
            save_screenshot(window, shot_path);
            std::fprintf(stderr, "wrote screenshot %s\n", shot_path.c_str());
#endif
            break;
        }
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    empower::SentryManager::shutdown();
    return 0;
}
