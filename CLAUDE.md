# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

A from-scratch HTTP/1.x server in C11 for Linux (POSIX sockets, `mmap`/`mprotect`). No external dependencies and no libc string helpers. The project uses its own types and utilities.

## Build & run

```sh
./build.sh          # debug build with -DDEBUG_LOG (echoes responses to stdout) -> build/Http-C
./build/Http-C      # listens on port 8888 (DEFAULT_PORT in main.c)
curl -v http://localhost:8888/
curl -v -X POST -H 'Content-Type: text/plain' -d 'hello' http://localhost:8888/   # POST echoes the body
```

There is no test suite, linter or Makefile. Check changes by building cleanly (the warnings are strict) and testing manually with `curl`. The debug build intentionally crashes on malformed requests (`UnhandledInput` asserts). To test error responses, build a release binary: `gcc -O2 -DNDEBUG -Wall -Wextra -pedantic -std=c11 -o build/Http-C-release main.c`. The VS Code `Build` task runs `build.sh`, and the launch config debugs `build/Http-C`.

## Architecture

**Unity build:** `main.c` is the only translation unit. The other `.c` files (`string_utils.c`, `http_status.c`, `arena.c`) are `#include`d into it, and `.h` files hold only types and macros. If you add a new module, include it from `main.c` the same way rather than adding it to the gcc command.

**Core conventions (`base_types.h`):**
- Fixed-width typedefs on top of `<stdint.h>`: `i8..i64`, `u8..u64`, `f32/f64`, `b32`. It also defines `true`/`false`/`null` macros, so don't include `<stdbool.h>`.
- `String` is a length-prefixed, **non-null-terminated** slice (`{i64 length; char *text}`). Make literals with `STR_LIT("...")` and print with `printf("%.*s", (i32)s.length, s.text)`. Parsed request fields are `String`s pointing into the request buffer and are never copied.
- Error handling has three tiers. `InvalidCodePath` traps even in release builds, `assert` is for development checks only, and `UnhandledInput(parser, status)` handles bad client input. See `CODING_GUIDE.md`.
- Size helpers: `KB()`, `MB()`, `GB()`.

**Memory (`arena.c`):** this is a virtual-memory arena. It reserves 2 GB with `PROT_NONE` and commits pages lazily with `mprotect` in 4 KB granules. `arena_push` initializes the arena on first use, so a zeroed global `Arena` is valid. Request handling is scoped with `temp_memory_begin`/`temp_memory_end` on `request_arena`, which resets the arena after each connection. Use arenas instead of `malloc`. `permanent_arena` is for long-lived allocations.

**Request flow (`main.c`):** `create_server` → `accept` loop → `read_http_request` → build an `HttpResponse` → `send_http_response` → close the connection. `read_http_request` reads the socket into the arena in 4 KB chunks. The chunks stay contiguous because nothing else pushes to the arena during the read. It reads until `\r\n\r\n`, parses the head, then reads `Content-Length` bytes of body. Requests are capped at `MAX_REQUEST_SIZE` (431 or 413 if larger). `HttpMessageParser` is a bounds-checked cursor over the head with a sticky `error` status, and the handler responds with that status if it's set. The `parse_http_*` functions handle the method, path, version and headers in order. Only `Host`, `User-Agent`, `Content-Type` and `Content-Length` are recognized; other headers are skipped. Methods use an X-macro list (`HTTP_METHOD_LIST`). The response is written with many `send_string` calls, which echo to stdout under `DEBUG_LOG`.

**Status codes (`http_status.c`):** this is an X-macro list (`HTTP_STATUS_LIST`) that generates both the `HttpStatus_<Name>` enum and `http_status_to_string` ("200 OK"). To add a status, add one `X(...)` line. `HttpStatus_None = 0` means "no status/error".

## Coding guide

**Follow `CODING_GUIDE.md` for all code changes.** It covers the file and build model, types, strings, memory, error handling (the `InvalidCodePath`, `assert` and `UnhandledInput` tiers) and style. Its "Known deviations" list tracks existing code that doesn't conform yet.
