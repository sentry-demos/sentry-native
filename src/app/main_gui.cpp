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
#include "app/telemetry_feed.h"
#include "app/theme.h"
#include "app/ui.h"
#include "chaos/chaos.h"
#include "core/sentry_manager.h"

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
    cfg.use_external_crash_reporter = true; // interactive desktop app
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
    if (sentry_ok) {
        sentry_attachment_t* screenshot =
            sentry_attach_file(screenshot_path.c_str());
        sentry_attachment_set_content_type(screenshot, "image/png");
    }
#endif

    empower::FleetModel fleet;
    fleet.init();
    empower::ConsoleLog console;
    empower::TelemetryFeed telemetry;
    if (sentry_ok) {
        empower::wire_telemetry_feed(telemetry);
    }
    console.push(empower::ConsoleLog::Level::Info, "boot",
                 "Fleet Control Center online");
    console.push(sentry_ok ? empower::ConsoleLog::Level::Info
                           : empower::ConsoleLog::Level::Warn,
                 "sentry",
                 sentry_ok ? "Sentry native backend initialized"
                           : "Sentry init failed (events will not be sent)");
    console.push(empower::ConsoleLog::Level::Info, "session", "session started");

    empower::AppState state;
    state.fleet = &fleet;
    state.console = &console;
    state.telemetry = sentry_ok ? &telemetry : nullptr;
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
                sentry_metrics_distribution("fleet.frame_time", dt * 1000.0, SENTRY_UNIT_MILLISECOND,
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

    empower::unwire_telemetry_feed();
    empower::SentryManager::shutdown();
    return 0;
}
