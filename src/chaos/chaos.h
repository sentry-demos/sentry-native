#pragma once

#include <string>
#include <vector>

namespace empower {

class ConsoleLog;

// How dangerous a scenario is, used purely for UI coloring.
enum class Severity { Crash, Warning, Backend, Message };

// One triggerable fault in the Chaos Lab. The list is the single source of
// truth shared by the UI (button grid) and the dispatcher.
struct ChaosScenario {
    const char* id;
    const char* label;
    const char* desc;
    Severity severity;
};

// The full catalog of scenarios.
const std::vector<ChaosScenario>& scenarios();

// Runs the scenario with the given id: sets up rich Sentry scope (breadcrumbs,
// tags, contexts, attachments, spans) and then triggers the fault. Crashing
// scenarios do not return. `console` may be null.
void trigger(const std::string& id, ConsoleLog* console);

} // namespace empower
