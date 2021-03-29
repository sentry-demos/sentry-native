## Overview

This project is a demo implementation of **sentry-native**, the Sentry SDK for Native Crash Reporting. The demo produces a native crash that gets captured by sentry-native and sent to Sentry.io This demo uses sentry-native in its packaged release form - it is referenced as a submodule. You can also download it separately here https://github.com/getsentry/sentry-native as a distribution zip.

The **Official Sentry Documentation** for sentry-native is at https://github.com/getsentry/sentry-native

## Setup

| dependency    |    version     |
| ------------- | :------------: |
| sentry-native |     0.4.8      |
| sentry-cli    |     1.4.9      |
| macOS         | Mojave 10.14.4 |

_See windows.txt for Windows_

#### sentry-cli

1. `yarn global add @sentry/cli`. You can also get it from https://github.com/getsentry/sentry-cli/releases/ or https://docs.sentry.io/cli/installation/

#### Mac

1. `git clone --recurse-submodules https://github.com/getsentry/sentry-native.git`
2. `make bin/example`
3. `make setup_release`
4. `make upload_debug_files`
5. `make run_crash` or `make run_message`

You can also run all of them at once sequentially:  
`make clean bin/example setup_release upload_debug_files run_crash`

`make clean` if you need to re-run `make bin/example` and upload new debug files.

You can also run all of them at once sequentially: `make clean bin/example setup_release upload_debug_files run_crash`

## Technical Notes

### What's Happening

`make bin/example` creates debug symbols and executables

`make setup_release` creates a Sentry Release and associates git commits

`make upload_debug_files` uploads your symbols to Project Settings > Debug Files https://sentry.io/settings/${YOUR_ORG}/projects/${PROJECT}/debug-symbols/. You can also access your symbols from a Symbol Server https://docs.sentry.io/workflow/debug-files/#symbol-servers

`make run_crash` causes a native crash in _src/example.c_. It sends one event to Sentry

`make run_message` causes a Sentry Message to get sent as an event to Sentry.

`make clean` is for re-generating debug symbols and executables

### Updating sentry-native

This demo app was tested with sentry-native v0.4.8.

```
git pull upstream master // or git pull origin master, if you're not on a fork
git merge upstream/master
git submodule update --init --recursive
```

## Troubleshooting

If your events are not symbolicated then run `make clean` and re-run commands from step 1

Try running the `make` commands one-by-one because if you run all at once and one of them has a problem, it won't halt execution of the following commands.

You need to always run `bin/example` before `setup_release`

If the standalone distribution package doesn't fit your needs, then go to https://github.com/getsentry/sentry-native#development

sentry-native in the news https://blog.sentry.io/2019/09/26/fixing-native-apps-with-sentry

## Gif

![gif](screenshots/sentry-native-crash-final.gif)
