# Overview
1. `git clone --recurse-submodules git@github.com:sentry-demos/sentry-native.git`

### Must Read

you need version `sentry-cli 1.49.0z` https://github.com/getsentry/sentry-cli/releases/ and https://docs.sentry.io/cli/installation/  
`yarn global add @sentry/cli`  

use https://github.com/getsentry/sentry-native when ready to implement this in your real code. this `sentry-demos/sentry-native` is an example implementation (demo) of `getsentry/sentry-native`

`example.c` generates **two types of events** and sends them simultaneously. One is defined by sentry-native as a Event Message. The second is a Native Crash, which still gets sent as an event.

This project makes use of **sentry-native**, the Sentry SDK for Native Crash Reporting https://github.com/getsentry/sentry-native

This project makes use of **sentry-native** in its packaged release form. It is referenced as a submodule in this project but you will download it as a distribution .zip from getsentry/sentry-native when you're ready to use it

This project is not for developing or testing locally, so if the standalone distribution package doesn't fit your needs, then go to https://github.com/getsentry/sentry-native#development

`sentry-native` in the news https://blog.sentry.io/2019/09/26/fixing-native-apps-with-sentry

## Mac
1. `make bin/example`
2. `make setup_release`
3. `make run` (this runs make clean_db as well)
4. `make clean` if you want to re-run `make bin/example` which re-creates debug symbols and executables

TODO:
`make clean` for re-running the demo
- Don't Do - anything from inside the submodule (e.g. bin) that should be 'cleaned/removed' as well?
- `rm *` from parent ./bin (except for the .gitignore)

```
sentry-cli upload-dif --org testorg-az --project sentry-native bin/{sentry_crashpad.dylib,sentry_crashpad.dSYM,example,example.dSYM}
...became
sentry-cli upload-dif --org testorg-az --project sentry-native bin/
...or else it would miss some files(3)
...but now 'example' won't upload, only the other 2. and still no .dSYM files
```
## Windows
TODO `set` DSN

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



### Results/Output
TODO - update these screenshots again
[Screenshot 1 of Message Event](screenshots/message-event-1.png)

[Screenshot 2 of Message Event](screenshots/message-event-2.png)

[Screenshot 1 of Native Crash Event](screenshots/native-crash-1.png)

[Screenshot 2 of Native Crash Event](screenshots/native-crash-2.png)

## FYI

The code used in `example.c` for causing a native crash is
```
memset((char *)0x0, 1, 100);
```
Why memset causes native crashes, http://www.cplusplus.com/reference/cstring/memset/

## TODO Remove
```
#  add COMMENTS LIKE
1. `make` builds the source files, application, uploads symbols
2. `make run` runs the bin/example
or...
```

```
where to put the -j4
```

Minidumps Endpoint for a `sentry-native` project:
```
curl -X POST \
  'https://sentry.io/api/1720457/minidump/?sentry_key=b5ceabee4e4a4cd6b21afe3bd2cbbed4' \
  -F upload_file_minidump=@b066e1b0-68ef-4d7a-8f59-e59ad0e63d8d.dmp
```



.PHONY's all on 1 line.