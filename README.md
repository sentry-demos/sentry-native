## Setup
1. `git clone --recurse-submodules git@github.com:sentry-demos/sentry-native.git`
2. install `sentry-cli 1.49.0z` from https://github.com/getsentry/sentry-cli/releases/ and https://docs.sentry.io/cli/installation/. You can run `yarn global add @sentry/cli`
3. [mac](#mac)

## Dev Tips

This project makes use of **sentry-native**, the Sentry SDK for Native Crash Reporting https://github.com/getsentry/sentry-native

This project makes use of **sentry-native** in its packaged release form. It is referenced as a submodule in this project but you will download it as a distribution .zip from getsentry/sentry-native when you're ready to use it

## Mac
1. `make bin/example`
2. `make setup_release`
3. `make run`
4. `make clean`

#### What's Happening
`make bin/example` creates debug symbols and executables  
`make setup_release` creates a Sentry Release and associates git commits  
`make run` executes `src/example.c` which sends 2 Events to Sentry. 1 is for a native crash and the other is a Sentry Capture Message. First it removes any outstanding .dmp files from ./sentry-db  
`make clean` only needs to be run if you're trying to re-run `make bin/example`

## Windows
[*These instructions are still under development*](./windows.txt)

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

## Minidumps
If you want to perform a `curl` request directly against our Sentry API and pass the .dmp in the payload...

https://docs.sentry.io/platforms/native/minidump/#minidump-additional

-F 'sentry[tags][mytag]=value'

Minidumps Endpoint for a `sentry-native` project:
```
curl -X POST \
  'https://sentry.io/api/1720457/minidump/?sentry_key=b5ceabee4e4a4cd6b21afe3bd2cbbed4' \
  -F upload_file_minidump=@b066e1b0-68ef-4d7a-8f59-e59ad0e63d8d.dmp
```

-j4