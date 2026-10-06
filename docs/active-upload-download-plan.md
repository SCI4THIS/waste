# Upload / Download Commands

Browser file transfer commands for the WASTE bash terminal.

## Overview

Two `/bin` commands that bridge the browser VFS and host OS via native file dialogs:

- **`upload DEST`** — opens a file picker, copies the selected file into the VFS at DEST
- **`download FILE`** — reads FILE from the VFS, triggers a browser download with a pre-populated filename

These names follow the convention used by Google Cloud Shell and AWS CloudShell.

## Architecture

Guest wasm executables call host functions that yield the engine. The worker detects the yield kind, messages the main thread to show a file dialog, and resumes the engine once the user completes or cancels.

```
Guest wasm          C engine           Worker JS           Main thread JS
───────────         ────────           ─────────           ──────────────
host_upload() ──→  yield(HOST_IO) ──→ wait_kind() ──→    <input type=file>
                                      read io params       user picks file
                                      postMessage ─────→  reads FileReader
                                      ←───────────────── postMessage(bytes)
                                      provide_upload()
              ←── resume() ←───────── waste_wast_resume()
returns size
```

## Implementation Steps

### 1. Engine yield reason

Add `EXEC_YIELD_HOST_IO` to the `exec_yield_reason` enum in `src/engine/engine_internal.h`.

### 2. Host IO state in browser_api.c

Add a static `g_host_io` struct tracking: kind (none/upload/download), path string, data buffer, and result (pending/completed/cancelled).

Export functions the worker JS calls to inspect and resolve the request:
- `waste_wast_host_io_kind()` — which operation is pending
- `waste_wast_host_io_path_ptr()` / `_len()` — the path/filename string
- `waste_wast_host_io_data_ptr()` / `_len()` — download data
- `waste_wast_host_io_provide_upload(ptr, len)` — worker provides file bytes
- `waste_wast_host_io_cancel()` — user cancelled the dialog

### 3. Host functions in posix_stubs.c

Two functions registered under the `waste_kernel` module:

**`host_upload_v1(path_ptr, path_len)`**
- First call: stores dest path, yields with `EXEC_YIELD_HOST_IO`
- On resume: writes uploaded bytes to VFS kernel, returns file size (or -1 if cancelled)

**`host_download_v1(name_ptr, name_len, data_ptr, data_len)`**
- First call: copies name and data from guest memory, yields with `EXEC_YIELD_HOST_IO`
- On resume: returns 0 (success) or -1 (cancel)

Both follow the idempotent re-call pattern used by `native_posix_read` (check state, yield if pending, complete if resolved).

### 4. Worker JS message protocol

Update the resume loop in `worker.js` to check `waste_wast_wait_kind()` for `HOST_IO`. On detection:
- **Download**: read data from engine memory, `postMessage({type: "host-download", name, bytes})`, mark complete
- **Upload**: `postMessage({type: "host-upload-request", destPath})`, await response

Add `host-upload-response` handler in `self.onmessage` to receive file bytes from the main thread and call `waste_wast_host_io_provide_upload()`.

### 5. Main thread file dialogs

Handle new worker messages in `app.js`:
- **`host-download`**: create Blob, set up `<a download="name">`, trigger click
- **`host-upload-request`**: create `<input type="file">`, handle `change` and `cancel` events, read with FileReader, send bytes back via `host-upload-response`

### 6. Guest executables

New build script `src/html-rt/tools/build-upload-download.py` with inline C for both commands. Pattern follows `build-ldd.py`: compile with `waste-wasm-clang`, link with `wasm-ld` using PIC flags. Guest `upload.c` calls `host_upload_v1` and prints result. Guest `download.c` reads the file via open/read/close, extracts basename, calls `host_download_v1`.

### 7. Build system

Add `upload` and `download` to `AUX_UTILITIES` in `start.sh` with a dispatch case in `build_single_aux`. Stage outputs to `src/html-rt/src/bash/{upload,download}.wasm`. Auto-discovery in the generator and app.js places them at `/bin/upload` and `/bin/download`.

### 8. Generated HTML

Mirror worker.js and app.js message handling in the inline JS templates within `generate-c-engine-bash-html.py`.

## Files

| File | Change |
|------|--------|
| `src/engine/engine_internal.h` | Add `EXEC_YIELD_HOST_IO` |
| `src/html-rt/browser_api.c` | Host IO state struct + 7 exported functions |
| `src/html-rt/posix_stubs.c` | 2 host functions + dispatch entries |
| `src/html-rt/src/worker.js` | Yield-kind check + upload response handler |
| `src/html-rt/src/app.js` | File picker + download trigger |
| `src/html-rt/tools/build-upload-download.py` | New build script |
| `start.sh` | AUX_UTILITIES + dispatch case |
| `src/html-rt/tools/generate-c-engine-bash-html.py` | Mirror JS changes |

## Verification

1. `./start.sh --build-aux` builds all utilities including upload/download
2. `./start.sh --html-bash` passes existing smoke tests
3. Browser: `upload /tmp/test.txt` → pick file → `cat /tmp/test.txt` shows contents
4. Browser: `echo hello > /tmp/out.txt` → `download /tmp/out.txt` → file downloads
5. Cancel: trigger upload, cancel picker, command returns non-zero
