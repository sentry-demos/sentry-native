all: bin/example
.PHONY: all

bin/example: prereqs src/example.c
	$(CC) -o $@ -Isentry-native/include src/example.c -Lbin -lsentry_crashpad -Wl,-rpath,"@executable_path"

prereqs: bin/libsentry_crashpad.dylib bin/crashpad_handler
.PHONY: prereqs

bin/libsentry_crashpad.dylib: sentry-makefile
	$(MAKE) -C sentry-native/premake sentry_crashpad
	cp sentry-native/premake/bin/Release/libsentry_crashpad.dylib bin

bin/crashpad_handler: sentry-makefile
	$(MAKE) -C sentry-native/premake crashpad_handler
	cp sentry-native/premake/bin/Release/crashpad_handler bin

# not needed for bundled sentry-native download
sentry-makefile: sentry-native/premake/Makefile
.PHONY: sentry-makefile

sentry-native/premake/Makefile:
	$(MAKE) -C sentry-native fetch configure

run:
	SENTRY_DSN=https://b5ceabee4e4a4cd6b21afe3bd2cbbed4@sentry.io/1720457 bin/example
.PHONY: run