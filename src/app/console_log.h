#pragma once

#include <deque>
#include <string>

namespace empower {

// A small in-memory ring buffer that backs the Console panel. Each line mirrors
// a breadcrumb-worthy event in the app, so the UI console reads like the trail
// Sentry will receive.
class ConsoleLog {
public:
    enum class Level { Debug, Info, Warn, Error };

    struct Line {
        Level level;
        std::string time;    // HH:MM:SS
        std::string source;  // subsystem
        std::string text;
    };

    void push(Level level, const std::string& source, const std::string& text);

    const std::deque<Line>& lines() const { return lines_; }
    void clear() { lines_.clear(); }

private:
    std::deque<Line> lines_;
    static constexpr size_t kMax = 400;
};

} // namespace empower
