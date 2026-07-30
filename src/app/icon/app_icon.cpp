#include "app/icon/app_icon.h"

#include "core/platform.h"

#include <cstdio>
#include <string>

#if defined(__APPLE__)
extern "C" void empower_set_dock_icon(const char* png_path);
#endif

namespace empower {
namespace {

std::string icon_png_path() {
    return path_join(path_join(executable_dir(), "assets"), "icon/app-icon.png");
}

bool file_exists(const std::string& path) {
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        std::fclose(f);
        return true;
    }
    return false;
}

} // namespace

void set_app_icon(GLFWwindow* window) {
    (void)window;
#if defined(__APPLE__)
    const std::string path = icon_png_path();
    if (file_exists(path)) {
        empower_set_dock_icon(path.c_str());
    }
#endif
}

} // namespace empower
