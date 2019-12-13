## Setup
1. `git clone --recurse-submodules git@github.com:thinkocapo/sentry-native.git`
2. `git clone --recurse-submodules git@github.com:sentry-demos/sentry-native.git`
3. install `sentry-cli 1.49.0z` from https://github.com/getsentry/sentry-cli/releases/ and https://docs.sentry.io/cli/installation/. You can run `yarn global add @sentry/cli`
4. [mac](#mac)

## Dev Tips

This project makes use of **sentry-native**, the Sentry SDK for Native Crash Reporting https://github.com/getsentry/sentry-native

This project makes use of **sentry-native** in its packaged release form. It is referenced as a submodule in this project but you will download it as a distribution .zip from getsentry/sentry-native when you're ready to use it

## Mac
1. `make bin/example`
2. `make setup_release`
3. `make upload_debug_files`
3. `make run`
4. `make clean`

#### What's Happening
`make bin/example` creates debug symbols and executables  
`make setup_release` creates a Sentry Release and associates git commits, and uploads debug symbols 
`make run` executes `src/example.c` which sends 2 Events to Sentry. 1 is for a native crash and the other is a Sentry Capture Message. First it removes any outstanding .dmp files from ./sentry-db  
`make clean` only needs to be run if you're trying to re-run `make bin/example`

#### If your events are not symbolicated
1. something
```
cd ./bin
➜  bin git:(master) ✗ rm -rf crash*
➜  bin git:(master) ✗ rm -rf exampl*
➜  bin git:(master) ✗ rm -rf libsent*
cd ../

and...

rm ./sentry-native/premake/bin

```


re-run from `make bin/example` again

2. something
3. something

## Windows
Do everything in Visual Studio Code and set the DSN first.
1. download Premake5.exe
2. cd premake5 and
premake5.exe vs2017 <---creates a solution, a wrapper around projects that ref each other
3. open the .sln in VS
4. click 'ok' for accepting the upgrade by VS.
5. rt-click 'example-crashpad' project and click Build
6. can 'Set as Default Project' for when you click Run Button at the top
7. rt-click 'crashpad-handler' project and click Build
8. the builds go into /sentry-native/premake/bin
9. sentry-native/premake, spwan a cmd, bin\Debug\example_crashpad.exe

## Event Examples
[Symbolicated stacktrace of a native crash](screenshots/native-crash-stacktrace.png)

[Sentry capture message](screenshots/message-capture.png)

## Notes
Use https://github.com/getsentry/sentry-native when ready to implement this in your real code. this `sentry-demos/sentry-native` is an example implementation (demo) of `getsentry/sentry-native`

This project is not for developing or testing locally, so if the standalone distribution package doesn't fit your needs, then go to https://github.com/getsentry/sentry-native#development

You need to always run `bin/example` before `setup_release`

The [memset](http://www.cplusplus.com/reference/cstring/memset/) invocation in `src/example.c` is what causes a native crash

sentry-native in the news https://blog.sentry.io/2019/09/26/fixing-native-apps-with-sentry

## Gif
![gif](screenshots/sentry-native-2-events-150.gif)
