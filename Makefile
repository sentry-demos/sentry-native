SENTRY_ORG=testorg-az
SENTRY_PROJECT=sentry-native
PREFIX=static/js
VERSION ?= $(shell sentry-cli releases propose-version)

all: bin/example
.PHONY: all prereqs sentry-makefile sentry-makefile setup_release create_release associate_commits upload_debug_files run_crash run_message clean clean_db

bin/example: prereqs src/example.c
	$(CC) -g -o $@ -DSENTRY_RELEASE=\"$(VERSION)\" -Isentry-native/include src/example.c -Lbin -lsentry_crashpad -Wl,-rpath,"@executable_path"

prereqs: bin/libsentry_crashpad.dylib bin/crashpad_handler

bin/libsentry_crashpad.dylib: sentry-makefile
	$(MAKE) -C sentry-native/premake sentry_crashpad
	cp sentry-native/premake/bin/Release/libsentry_crashpad.dylib bin
	cp -R sentry-native/premake/bin/Release/libsentry_crashpad.dylib.dSYM bin

bin/crashpad_handler: sentry-makefile
	$(MAKE) -C sentry-native/premake crashpad_handler
	cp sentry-native/premake/bin/Release/crashpad_handler bin
	cp -R sentry-native/premake/bin/Release/crashpad_handler.dSYM bin

sentry-makefile: sentry-native/premake/Makefile

sentry-native/premake/Makefile:
	$(MAKE) -C sentry-native fetch configure

# SENTRY
setup_release: create_release associate_commits
create_release:
	sentry-cli releases -o $(SENTRY_ORG) new -p $(SENTRY_PROJECT) $(VERSION)

associate_commits:
	sentry-cli releases -o $(SENTRY_ORG) -p $(SENTRY_PROJECT) set-commits $(VERSION) --auto

upload_debug_files:
	sentry-cli upload-dif --org testorg-az --project $(SENTRY_PROJECT) --wait --include-sources bin/

run_crash: clean_db
	SENTRY_DSN=https://b5ceabee4e4a4cd6b21afe3bd2cbbed4@sentry.io/1720457 bin/example --crash

run_message: clean_db
	SENTRY_DSN=https://b5ceabee4e4a4cd6b21afe3bd2cbbed4@sentry.io/1720457 bin/example --message

clean:
	rm -rf ./bin/exampl*
	rm -rf ./bin/crash*
	rm -rf ./bin/libsent*
	rm -rf ./sentry-native/premake/bin/Release
	
clean_db:
	rm -rf ./sentry-db/*
