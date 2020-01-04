## Overview
The goal of this is to produce a native crash that gets captured / sent as event to Sentry.

This project is a demo implementation of **sentry-native**, the Sentry SDK for Native Crash Reporting which you can download here https://github.com/getsentry/sentry-native as a distribution zip for use in Production. This project uses it in its pakcaged release form - it is referenced as a submodule.

**Official Sentry Documentation**
Use https://github.com/getsentry/sentry-native when ready to implement this in your real code.

## Setup
This is for running on Mac. See windows.txt for Windows.
1. `git clone --recurse-submodules git@github.com:sentry-demos/sentry-native.git`
2. `yarn global add @sentry/cli` to install *sentry-cli 1.49.0z*. You can also get it from https://github.com/getsentry/sentry-cli/releases/ or https://docs.sentry.io/cli/installation/

## Mac
1. `make bin/example`
2. `make setup_release`
3. `make upload_debug_files`
4. `make run_crash` or `make run_message`

`make clean` if you need to re-run `make bin/example` and upload new debug files.

You can also run all of them at once sequentially: `make clean bin/example setup_release upload_debug_files run_crash`

## Technical Notes
### What's Happening
`make bin/example` creates debug symbols and executables

`make setup_release` creates a Sentry Release and associates git commits

`make upload_debug_files` uploads your symbols to https://sentry.io/settings/${YOUR_ORG}/projects/${PROJECT}/debug-symbols/ which is Project Settings > Debug Files

`make run_crash` causes a native crash in *src/example.c*. It sends one event to Sentry

`make run_message` causes a Sentry Message to get sent as an event to Sentry.

`make clean` is for re-generating debug symbols and executables


## Troubleshooting
If your events are not symbolicated then run `make clean` and re-run commands from step 1

You need to always run `bin/example` before `setup_release`

If the standalone distribution package doesn't fit your needs, then go to https://github.com/getsentry/sentry-native#development

sentry-native in the news https://blog.sentry.io/2019/09/26/fixing-native-apps-with-sentry

## Gif
![gif](screenshots/sentry-native-2-events-150.gif)
