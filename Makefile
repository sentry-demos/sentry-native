SENTRY_ORG=testorg-az
SENTRY_PROJECT=sentry-native
VERSION ?= $(shell sentry-cli releases propose-version)
CMAKE=cmake

all: bin/example
.PHONY: all prereqs sentry-makefile sentry-makefile setup_release create_release associate_commits upload_debug_files run_crash run_message clean clean_db

# add the following flags if you want to enable the real api call: -lcurl -mmacosx-version-min=10.6
# note that including `-lcurl` flag will require you to download libcurl locally and set that up.
bin/example: prereqs src/example.c
	$(CC) -g -o $@ -DSENTRY_RELEASE=\"$(VERSION)\" -DSENTRY_PERFORMANCE_MONITORING="ON" -Isentry-native/include src/example.c -Lbin -lsentry -Wl,-rpath,"@executable_path"

prereqs: bin/libsentry.dylib bin/crashpad_handler

bin/libsentry.dylib: sentry-makefile
	$(CMAKE) --build build --parallel --target sentry
	cp build/libsentry.dylib bin
	cp -R build/libsentry.dylib.dSYM bin

bin/crashpad_handler: sentry-makefile
	$(CMAKE) --build build --parallel --target crashpad_handler
	cp build/crashpad_build/handler/crashpad_handler bin
	cp -R build/crashpad_build/handler/crashpad_handler.dSYM bin

sentry-makefile: build/Makefile

build/Makefile:
	$(CMAKE) -B build sentry-native


# SENTRY
setup_release: create_release associate_commits
create_release:
	sentry-cli releases -o $(SENTRY_ORG) new -p $(SENTRY_PROJECT) $(VERSION)

associate_commits:
	sentry-cli releases -o $(SENTRY_ORG) -p $(SENTRY_PROJECT) set-commits $(VERSION) --auto

upload_debug_files:
	sentry-cli upload-dif --org testorg-az --project $(SENTRY_PROJECT) --wait --include-sources bin/

run_crash:
	SENTRY_DSN=https://35a178f3e42445ada24add8bca35a226@o87286.ingest.sentry.io/6297711 bin/example --crash

run_message:
	SENTRY_DSN=https://35a178f3e42445ada24add8bca35a226@o87286.ingest.sentry.io/6297711 bin/example --message

run_clean:
	SENTRY_DSN=https://35a178f3e42445ada24add8bca35a226@o87286.ingest.sentry.io/6297711 bin/example

clean:
	rm -rf ./bin/exampl*
	rm -rf ./bin/crash*
	rm -rf ./bin/libsent*
	rm -rf ./build
	rm -rf ./sentry-db
