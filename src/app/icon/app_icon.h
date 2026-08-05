#pragma once

struct GLFWwindow;

namespace empower {

// Sets the OS-visible app icon (Dock on macOS, taskbar on Windows).
void set_app_icon(GLFWwindow* window);

} // namespace empower
