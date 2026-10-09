# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

A from-scratch HTTP/1.x server in C11. No external dependencies, and all OS and libc calls sit behind a small platform layer (`os.h`, implemented for Linux in `os_linux.c`). The project uses its own types and utilities.

## Build & run

```sh
./build.sh          # debug build with -DDEBUG_LOG (echoes responses to stdout) -> build/Http-C
./build/Http-C      # listens on port 8888 (DEFAULT_PORT in http_server.c)
curl -v http://localhost:8888/
curl -v -X POST -H 'Content-Type: text/plain' -d 'hello' http://localhost:8888/   # POST echoes the body
curl -v -X POST -H 'Content-Type: application/json' -d '{"a":1}' http://localhost:8888/   # invalid JSON gets a 400
./build_tests.sh    # builds tests/test_main.c in debug and release (-DNDEBUG) and runs both
```

There is no linter or Makefile. The automated tests (`tests/test_main.c`, a separate unity build) cover the JSON parser and the work queue. They also parse a 9 MB `Test.json` outside the repo (`TEST_JSON_PATH`) and skip it if it's missing. Check server changes by building cleanly (the warnings are strict) and testing manually with `curl`. The debug build intentionally crashes on malformed requests (`UnhandledInput` asserts). To test error responses, build a release binary: `gcc -O2 -DNDEBUG -Wall -Wextra -pedantic -std=c11 -pthread -o build/Http-C-release main.c`. For threading changes, also run a `-fsanitize=thread` build. The VS Code `Build` task runs `build.sh`, and the launch config debugs `build/Http-C`.

## Architecture

**Unity build:** `main.c` is the only translation unit and only includes files: `os_linux.c` first, then `base_types.h`, `string_utils.c`, `memory_utils.c`, `http_status.c`, `arena.c`, `json.c`, `work_queue.c` and `http_server.c`. `.h` files hold only types, macros and declarations. `arena.h` declares the arena API so `os_linux.c`, which is included first, can use it. If you add a new module, include it from `main.c` (and from `tests/test_main.c` if it's tested) rather than adding it to the gcc command.

**Platform layer (`os.h`, `os_linux.c`):** the only place that includes system headers or calls the OS or libc. It provides primitives: virtual memory, `os_read_entire_file`, `os_parse_f64` (`strtod`), `os_print`, threads, semaphores, `atomic_*_u32` operations and TCP sockets (`OsSocket`). `main` lives in `os_linux.c`. It installs the crash handlers, which close the listening socket, and then calls `app_run()`. Logic built on the primitives stays in shared code. `os_linux.c` defines `_DEFAULT_SOURCE` before its first system header.

**Core conventions (`base_types.h`):**
- Fixed-width typedefs on top of `<stdint.h>`: `i8..i64`, `u8..u64`, `f32/f64`, `b32`. It also defines `true`/`false`/`null` macros, so don't include `<stdbool.h>`.
- `String` is a length-prefixed, **non-null-terminated** slice (`{i64 length; char *text}`). Make literals with `STR_LIT("...")` and print with `os_print("%.*s", (i32)s.length, s.text)`. Parsed request fields are `String`s pointing into the request buffer and are never copied.
- Error handling has three tiers. `InvalidCodePath` traps even in release builds, `assert` is for development checks only, and `UnhandledInput(parser, status)` handles bad client input. See `CODING_GUIDE.md`.
- Size helpers: `KB()`, `MB()`, `GB()`, plus `ArrayCount()`. Compiler differences (`Trap()`, `ThreadLocal`, `PrintfFormat`) are in an `#if defined(_MSC_VER)` block.

**Memory (`arena.h`, `arena.c`):** this is a virtual-memory arena. It reserves 2 GB and commits pages lazily in page-size granules through `os_reserve_memory`/`os_commit_memory`. Popping far enough back decommits (`MADV_FREE` + `PROT_NONE` on Linux). `arena_push` initializes the arena on first use, so a zeroed global `Arena` is valid. Pushes align their start and zero the memory (`_no_zero`/`NoZero` variants skip that). Each thread has two scratch arenas through `arena_get_scratch(conflict)`. Use arenas instead of `malloc`. `permanent_arena` is for long-lived allocations on the main thread.

**Request flow (`http_server.c`):** `app_run` → `os_socket_listen` → accept loop, which queues each connection on `connection_queue` (a 503 is sent if the queue is full) → a worker thread runs `handle_connection`: `read_http_request` → `handle_http_request` builds an `HttpResponse` → `send_http_response` → close the connection. `request_arena` is thread-local, and each connection is wrapped in `temp_memory_begin`/`temp_memory_end` on it. `read_http_request` reads the socket into the arena in 4 KB chunks. The chunks stay contiguous because nothing else pushes to the arena during the read. It reads until `\r\n\r\n`, parses the head, then reads `Content-Length` bytes of body. Requests are capped at `MAX_REQUEST_SIZE` (431 or 413 if larger). `HttpMessageParser` is a bounds-checked cursor over the head with a sticky `error` status, and the handler responds with that status if it's set. The `parse_http_*` functions handle the method, path, version and headers in order. Only `Host`, `User-Agent`, `Content-Type` and `Content-Length` are recognized (case-insensitively); other headers are skipped. Methods use an X-macro list (`HTTP_METHOD_LIST`). The response is written with many `send_string` calls, which echo to stdout under `DEBUG_LOG`.

**JSON (`json.c`):** `json_parse(arena, text)` is a tokenizer plus a recursive descent parser that builds a `JsonNode` tree in the arena. It's hardened for untrusted input: errors go into `JsonDocument.error`/`error_position` and never assert, nesting is limited to `JSON_MAX_DEPTH`, and the output arena is reset on error. Strings without escapes point into the source text. Arrays whose elements share a type are stored compactly (`f64[]`, `String[]`, ...), and other arrays as `JsonNode[]` (`JsonArrayType_Mixed`). POST bodies with `Content-Type: application/json` are parsed, and invalid JSON gets a 400.

**Work queue (`work_queue.c`):** a ring buffer with one producer (the accept loop) and `os_get_processor_count()` worker threads. The queue logic and its memory ordering live in shared code, while the platform layer provides only threads, a semaphore and atomics. Workers claim entries with a compare-exchange on `next_entry_to_read`. A slot's `in_use` flag stops the producer from reusing it before the claiming worker has copied it. `work_queue_add_entry` is single-producer only and returns false when the queue is full. `WORK_QUEUE_SIZE` can be overridden with `-D` to test that.

**Status codes (`http_status.c`):** this is an X-macro list (`HTTP_STATUS_LIST`) that generates both the `HttpStatus_<Name>` enum and `http_status_to_string` ("200 OK"). To add a status, add one `X(...)` line. `HttpStatus_None = 0` means "no status/error".

## Coding guide

**Follow `CODING_GUIDE.md` for all code changes.** It covers the file and build model, types, strings, memory, the platform layer, threads, error handling (the `InvalidCodePath`, `assert` and `UnhandledInput` tiers), tests and style. Its "Known deviations" list tracks existing code that doesn't conform yet.

## Planned work

`DEVELOPMENT_PLAN.md` describes the completed port of an old Windows C++ project and the move behind the platform layer. Its "Status" section lists where the implementation differs from the plan, and "Future work" lists what's next: profiling, a Windows backend (`os_win32.c`) and static file serving.
