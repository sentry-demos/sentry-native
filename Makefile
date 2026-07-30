# Convenience wrapper around CMake for Linux/macOS. Windows users invoke CMake
# directly (see the README). The real build definition lives in CMakeLists.txt.
BUILD ?= build
BUILD_TYPE ?= RelWithDebInfo

.PHONY: all build run headless smoke test-offline crash-reporter clean

all: build

build:
	cmake -B $(BUILD) -S . -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DEMPOWER_BUILD_GUI=ON
	cmake --build $(BUILD) --parallel

# Launch the GUI Fleet Control Center (needs SENTRY_DSN in the environment).
run: build
	./$(BUILD)/empower-fleet

# Run the headless autopilot for 60s (emits telemetry, then crashes).
headless: build
	./$(BUILD)/empower-headless --autopilot --duration 60

# Connectivity smoke test.
smoke: build
	./$(BUILD)/empower-smoke

# Assert Go Offline caches message events and hard crashes under database/cache/.
test-offline: build
	./$(BUILD)/empower-test-offline

# Download + theme the official external crash reporter into the build dir.
crash-reporter: build
	bash scripts/fetch-crash-reporter.sh $(BUILD)

clean:
	rm -rf $(BUILD)
