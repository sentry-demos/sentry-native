# Contributing

Guide for local development: symbolicated stacks and the Chaos Lab crash taxonomy.

## Local debug files (symbolication)

CI uploads debug information files on every build ([`.github/workflows/ci.yml`](.github/workflows/ci.yml)). When you build locally and trigger **Chaos Lab**, stacks stay unsymbolicated until you upload debug files for **the binary you actually run**.

### Debug ID vs release

| Term | What it is | When it changes |
| --- | --- | --- |
| **Debug ID** | Fingerprint stamped into the binary (and its `.dSYM` / `.pdb`) at **link** time | Almost every time the **linker** produces a new executable |
| **Release** | `empower.native@<git-sha>`, baked in at configure time | Only when the git commit changes |

Sentry matches crashes to symbols using the **debug ID**. The **release** groups events by commit.

**Relink** = the linker ran again and wrote a new binary, even with no source changes (clean build, reconfigure, etc.). Skip the upload if CMake did not relink — same binary file, same debug ID.

### Setup

Copy [`.env.local.example`](.env.local.example) to `.env.local` and fill in your token, org, and project. Do not commit `.env.local`.

Create a token at [Sentry auth tokens](https://sentry.io/settings/account/api/auth-tokens/) with scopes: `org:read`, `project:read`, `project:releases` (or `project:write`).

Load credentials in the shell before running `sentry-cli`:

```sh
set -a && source .env.local && set +a
```

Install `sentry-cli` if needed: `npm install -g @sentry/cli`.

### Upload by platform

**macOS** (after a rebuild that relinked):

```sh
dsymutil build/empower-fleet -o build/empower-fleet.dSYM
sentry-cli debug-files upload --include-sources --wait \
  build/empower-fleet build/empower-fleet.dSYM
```

**Linux** — debug info is embedded in the ELF binary (`-g` in CMake):

```sh
sentry-cli debug-files upload --include-sources --wait build/empower-fleet
```

**Windows** — upload the `.exe` and matching `.pdb` from the build dir.

Check the debug ID: `sentry-cli debug-files check build/empower-fleet` (macOS/Linux).

## Crash taxonomy

Every **Chaos Lab** scenario lives in [`src/chaos/chaos.cpp`](src/chaos/chaos.cpp). Each run sets `chaos.scenario`, a transaction name, breadcrumbs, device context, and a deterministic fingerprint so repeated runs group into one issue.

Trigger from the GUI (**Chaos Lab** tab), headless (`build/empower-headless --crash <id>`), or remote HTTP (`POST /trigger/<id>` when listening).

### Crash scenarios

These terminate the process (or hang until the watchdog fires). Tag prefix: `crash-<id>`.

| ID | UI label | Fault | What happens |
| --- | --- | --- | --- |
| `null-deref` | Null Dereference | `SIGSEGV` | Reads through a null device register pointer in `read_device_register`. |
| `use-after-free` | Use After Free | `SIGSEGV` | Ingest worker unmaps a 1 MB sample buffer; render thread reads it later on a different thread. |
| `stack-overflow` | Stack Overflow | `SIGSEGV` / stack guard | `resolve_dependencies` recurses with a 256-byte frame until the stack is exhausted. |
| `divide-by-zero` | Divide By Zero | `SIGFPE` or null deref | `compute_yield_per_plant(0)` — integer divide by zero; on arm64 the bogus quotient is dereferenced. |
| `heap-corruption` | Heap Corruption | `abort` | `decode_sensor_frame` writes 4096 bytes into a 32-byte malloc buffer; `free` detects corruption and aborts. |
| `assert-fail` | Assertion Abort | `SIGABRT` | Bad firmware version fails OTA check: captures a **fatal message** (`soft-assert-fail`), then `std::abort()`. Look for the **crash** event (`crash-assert-fail`) for the stack — the message has no stack trace. |
| `gpu-stress` | GPU Device Lost | `SIGSEGV` | Null vertex buffer write on the render path; includes a `gpu` context object. |
| `convoluted` | Convoluted Chain | `SIGSEGV` (indirect) | **Seer showcase:** calibration blob overflows a 16-byte field and corrupts a callback pointer on a worker thread; crash happens later when an unrelated sensor sample invokes the corrupt callback. Attaches `calibration-blob.bin`. |

### Non-crash scenarios

These return to the app. Handled events use tag prefix `soft-<id>` or scenario-specific tags.

| ID | UI label | Outcome | What happens |
| --- | --- | --- | --- |
| `app-hang` | App Hang | App-hang event | Main thread blocks for 8 s during a fake telemetry flush. Rich payload: 256 KB attachment, `telemetry_flush` context, breadcrumb trail, `telemetry.flush.queue_depth` gauge. Tag: `hang-app-hang`. |
| `backend-500` | Backend Error | Handled error message | Real HTTP checkout to the Flask backend (`EMPOWER_BACKEND_URL`). Captures an error event if the request fails; propagates `sentry-trace` / `baggage`. Tag: `soft-backend-500`. |
| `message` | Handled Message | Info message | Non-fatal status report via `sentry_capture_event_with_scope`. Tag: `soft-message`. |

### Headless trigger examples

```sh
build/empower-headless --crash convoluted
build/empower-headless --crash assert-fail
build/empower-headless --listen 8799
curl -X POST http://127.0.0.1:8799/trigger/heap-corruption
```
