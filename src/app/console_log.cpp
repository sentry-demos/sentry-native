#include "app/console_log.h"

#include <ctime>

namespace empower {

void ConsoleLog::push(Level level, const std::string& source, const std::string& text) {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char stamp[16];
    std::strftime(stamp, sizeof(stamp), "%H:%M:%S", &tm);

    lines_.push_back(Line{level, stamp, source, text});
    while (lines_.size() > kMax) {
        lines_.pop_front();
    }
}

} // namespace empower
