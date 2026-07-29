#pragma once

#include <functional>
#include <string>

namespace empower {

class FleetModel;
class ConsoleLog;
class TelemetryFeed;

// Mutable UI state shared across frames. Owned by the app entry point.
struct AppState {
    FleetModel* fleet = nullptr;
    ConsoleLog* console = nullptr;
    TelemetryFeed* telemetry = nullptr;

    // Identity shown in the sidebar / header.
    std::string operator_name = "John Gardener";
    std::string environment = "production";
    std::string release;
    std::string dsn_host;       // masked ingest host, for the Settings page
    std::string screenshot_path; // live UI PNG for feedback attachments
    bool dsn_configured = false;

    // Navigation.
    int page = 0;               // index into the sidebar nav
    int selected_device = -1;
    bool dismiss_crash_banner = false;

    // Invoked when a Chaos Lab action is triggered. The scenario id is a stable
    // string key; the handler decides what actually happens.
    std::function<void(const std::string& scenario)> on_chaos;
};

// Renders the entire Fleet Control Center for one frame into the main viewport.
void render_ui(AppState& state);

} // namespace empower
