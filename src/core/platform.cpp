#include "core/platform.h"

#if defined(_WIN32)
#  include <windows.h>
#elif defined(__APPLE__)
#  include <mach-o/dyld.h>
#  include <climits>
#  include <cstdlib>
#else
#  include <unistd.h>
#  include <climits>
#endif

namespace empower {

namespace {
std::string dir_of(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}
} // namespace

std::string executable_dir() {
#if defined(_WIN32)
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return dir_of(std::string(buf, len));
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string raw(size, '\0');
    if (_NSGetExecutablePath(raw.data(), &size) != 0) {
        return ".";
    }
    char resolved[PATH_MAX];
    if (realpath(raw.c_str(), resolved)) {
        return dir_of(resolved);
    }
    return dir_of(raw.c_str());
#else
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) {
        return ".";
    }
    buf[len] = '\0';
    return dir_of(buf);
#endif
}

std::string path_join(const std::string& dir, const std::string& name) {
#if defined(_WIN32)
    const char sep = '\\';
#else
    const char sep = '/';
#endif
    if (dir.empty()) return name;
    return dir + sep + name;
}

} // namespace empower
