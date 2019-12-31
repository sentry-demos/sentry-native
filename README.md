## Overview
The goal of this is to produce a native crash that gets captured / sent as event to Sentry. Also captures a Message...

This project makes use of **sentry-native**, the Sentry SDK for Native Crash Reporting https://github.com/getsentry/sentry-native

Note - This project makes use of **sentry-native** in its packaged release form. It is referenced as a submodule in this project but you will download it as a distribution .zip from getsentry/sentry-native when you're ready to use it

## Setup
This is for running on Mac. For Windows see windows.txt which is still under development.
1. `git clone --recurse-submodules git@github.com:thinkocapo/sentry-native.git`
2. `git clone --recurse-submodules git@github.com:sentry-demos/sentry-native.git`
3. install `sentry-cli 1.49.0z` from https://github.com/getsentry/sentry-cli/releases/ and https://docs.sentry.io/cli/installation/. You can run `yarn global add @sentry/cli`

## Mac
1. `make bin/example`
2. `make setup_release`
3. `make upload_debug_files`
4. `make run_crash`
5. `make run_message`
6. `make clean`

## Technical Notes
### What's Happening
`make bin/example` creates debug symbols and executables  

`make setup_release` creates a Sentry Release and associates git commits

`make upload_debug_files` uploads your symbols to Sentry.io where you can find them in Project Settings > Debug Files which is at https://sentry.io/settings/${ORG}/projects/${PROJECT}/debug-symbols/

`make run_crash` causes a native crash in `src/example.c`. It sends one event to Sentry

`make run_message` causes a Sentry Message to get sent as an event to Sentry.

`make clean` is for re-generating debug symbols and executables

Use https://github.com/getsentry/sentry-native when ready to implement this in your real code. this `sentry-demos/sentry-native` is an example implementation (demo) of `getsentry/sentry-native`

### Dev Tips

The [memset](http://www.cplusplus.com/reference/cstring/memset/) invocation in `src/example.c` is what causes a native crash

sentry-native in the news https://blog.sentry.io/2019/09/26/fixing-native-apps-with-sentry

## Troubleshooting
#### If your events are not symbolicated
`make clean` and re-run commands from step 1

#### Other
This project is not for developing or testing locally, so if the standalone distribution package doesn't fit your needs, then go to https://github.com/getsentry/sentry-native#development

You need to always run `bin/example` before `setup_release`

## Gif
![gif](screenshots/sentry-native-2-events-150.gif)
