# Coding Guide

Conventions for this codebase. Where existing code disagrees with this guide, the guide wins; see [Known deviations](#known-deviations).

## Files and the build

- The project is a **unity build**: `main.c` is the only translation unit, and every other file is `#include`d into it. Never add files to the gcc command in `build.sh`.
- Any file containing **function bodies uses `.c`**. **`.h` files only hold types, macros and declarations.**
- Every file starts with `#pragma once`.
- Include order in `main.c`: the platform block (`os_linux.c`, see [Platform layer](#platform-layer)) first, then system headers, then `base_types.h`, then project headers, then project `.c` files.
- The build must stay warning-free under `-Wall -Wextra -pedantic -std=c11`.

## Base types

- Use the typedefs from `base_types.h` (`i8..i64`, `u8..u64`, `f32`, `f64`, `b32`). Don't use raw `int`, `long` and so on, except where a system API requires them (`socklen_t`, `ssize_t`).
- The integer typedefs are based on `<stdint.h>` (`int32_t`, `uint64_t`, ...), with a `static_assert` checking each size.
- Booleans are `b32`, with the `true` and `false` macros. Don't include `<stdbool.h>`. Null pointers are `null`.
- Sizes, lengths and offsets are `i64` (for `String`) or `u64` (for memory). Loop indices use the same type as the bound they're compared with.
- Use `KB()`, `MB()` and `GB()` for byte sizes.

## Strings

- `String` (`{i64 length; char *text}`) is a **non-null-terminated** slice. Pass and return it by value.
- Make literals only with `STR_LIT("...")`. It relies on `sizeof`, so it gives the wrong length for a `char *`.
- Parsed data is a `String` that points into the original buffer. **Don't copy** unless the data has to outlive that buffer.
- Print with `printf("%.*s", (i32)s.length, s.text)`.
- Don't use `<string.h>`. Add `string_` functions in `string_utils.c` instead.
- Print with `os_print`, a `printf`-style function with format checking. To print an `i64` portably, cast it to `i32` when it's known to be small, or format it with `string_from_int` and print the `String`.

## Memory

- **Use arenas, never `malloc`/`free`.** A zero-initialized `Arena` is valid and sets itself up on its first `arena_push`.
- `permanent_arena` holds data that lives for the whole program. `request_arena` holds per-connection data and is wrapped in `temp_memory_begin`/`temp_memory_end` in the accept loop.
- Anything pushed to `request_arena`, and every `String` that points into the request buffer, is **invalid after `temp_memory_end`**. Never store it anywhere that lives longer.
- Every push aligns its **start** (to the arena's alignment, 16, or the one given to `arena_push_aligned`). `arena->alignment` is never changed by an aligned push.
- Consecutive pushes are contiguous only when each size is a multiple of the next push's alignment. The request read loop depends on this (4 KB chunks).
- `arena_push` and the `PushType`/`PushArray`/`PushArrayAligned` macros zero the memory. The `_no_zero`/`NoZero` variants don't. Use them only when the caller overwrites the whole allocation straight away, such as the request read buffer. Decommitted memory can come back holding old data, so never rely on unzeroed memory being zero.
- The array macros check `count * sizeof(type)` for overflow and trap. They don't make an untrusted count safe: bound it before pushing.
- **Scratch arenas:** `arena_get_scratch(conflict)` returns one of two thread-local scratch arenas, the one that isn't `conflict`. A function that writes into an output arena and also uses scratch passes its output arena as `conflict` (it might itself be a scratch arena). Pass `null` when there is no output arena. Wrap each use in `temp_memory_begin`/`temp_memory_end`.
- Scratch memory is a stack: code that recurses can't build one contiguous array in scratch while a nested call pushes to the same arena.
- Use `memory_copy` and `memory_set` (`memory_utils.c`), not `<string.h>`.

## Platform layer

- **Application code only calls functions defined in this codebase.** Calls to the OS *and to libc* (`printf`, `strtod`, `memcpy`, sockets, `mmap`, pthreads, ...) only happen in `os_<platform>.c`.
- `os.h` declares the platform API (`os_` functions, `Os` handle types). `os_linux.c` is the only implementation. `main.c` includes it first, through an `#if defined(__linux__)` block.
- **Keep platform code as small as possible.** The platform layer provides primitives, and logic built on them stays shared. For example, the arena logic is in `arena.c` and only calls `os_reserve_memory`, `os_commit_memory` and so on. Use a higher-level platform function only when the platforms differ in the whole operation (like reading an entire file).
- Platform functions handle OS details internally (`EINTR` retries, `MSG_NOSIGNAL`, `errno`) and report failures through return values in our own types.
- Freestanding headers are allowed everywhere: `<stdint.h>`, `<stddef.h>`, `<stdarg.h>`, `<limits.h>`, `<float.h>`. `base_types.h` also keeps `<assert.h>`. Every other system header is included **only** by `os_*.c`. Check with `grep -n '#include <' *.c *.h`.
- `os_linux.c` defines `_DEFAULT_SOURCE` before its system headers. Under `-std=c11`, glibc hides `madvise` and `MADV_*` without it, and the macro only works before the *first* system header.
- Opaque handles for OS objects that differ in size use fixed-size storage (`typedef struct OsSemaphore { _Alignas(8) u8 storage[32]; } OsSemaphore;`), and each implementation `static_assert`s that its native type fits.
- Atomics use an `atomic_` prefix (not `os_`) with a type suffix and, where it applies, an ordering (`atomic_load_acquire_u32`), so the names never clash with `<stdatomic.h>`.
- **Compiler differences are not platform differences.** The trap and thread-local storage are handled by the `#if defined(_MSC_VER)` block in `base_types.h` (`Trap()`, `ThreadLocal`).

## Errors and assertions

There are three tiers. Choose based on **who could cause the failure**.

| Tool | When | In release builds |
|---|---|---|
| `InvalidCodePath` | A state that is impossible if our logic is correct. If it is reached, the program's state can't be trusted. | **Always crashes** (`__builtin_trap()`) |
| `assert(...)` | Cheap development checks of our own preconditions and invariants (non-null arguments, alignment and so on). | Removed (`NDEBUG`) |
| `UnhandledInput(parser, status)` | Client input that we don't handle yet, or that is invalid. | Records `status`; a response is sent and the server keeps running |

Rules:
- **`InvalidCodePath` must never be reachable from client input**, or any client can bring down the server.
- **An `assert` is never the only protection for memory safety.** Checking that a read stays inside an input buffer is a real `if`.
- `UnhandledInput` also asserts in debug builds, so new edge cases show up during development. Each use is a TODO that can be grepped: either handle the case properly or keep the error response on purpose.
- Parse errors go into an error field on the parser (`HttpStatus error`). The first error wins, and parse functions return early once it is set. The request handler then answers with that status.
- Status codes for client input:
  - **400 Bad Request**: malformed syntax.
  - **501 Not Implemented**: a valid but unknown or unsupported method.
  - **505 HTTP Version Not Supported**: an unsupported protocol version.
  - **413/431**: a body or headers that are too large.
  - **5xx in general**: faults on the server side.
- Check the return value of every system call (`socket`, `read`, `send`, `mprotect`, ...). Recover where you can, for example by dropping the connection and continuing. A failure that leaves our own state corrupt is an `InvalidCodePath`.

## Macros

- Prefer functions. Use a macro only when it needs things functions can't do: `sizeof` on a literal, `#`/`##`, or compile-time tables.
- Multi-statement macros are wrapped in `do { ... } while (0)`.
- For lists that generate several pieces of code, use **X-macros** (see `HTTP_STATUS_LIST` in `http_status.c`). Add an entry by adding one `X(...)` line.
- Naming: constants and tables use `UPPER_SNAKE` (`DEFAULT_PORT`, `STR_LIT`, `HTTP_STATUS_LIST`). Statement-like helper macros use `PascalCase` (`InvalidCodePath`, `AssertAligned`, `KB`).

## Tests

- `tests/test_main.c` is a second unity build that includes the platform layer and the modules it tests. `build_tests.sh` builds it in debug and release (`-DNDEBUG`) and runs both. Both must pass.
- Parsers that take client input get malformed-input tests, including every truncation of a valid input and random mutations, which must never crash or assert.

## Debug output

- Debug-only logging and tracing is gated behind `DEBUG_LOG` (`#ifdef DEBUG_LOG`), which `build.sh` passes as `-DDEBUG_LOG` for development builds. Without the flag, debug macros compile to nothing.
- Without `DEBUG_LOG`, output covers only startup, connections and errors.

## Naming and formatting

- **Two-space indentation and Allman braces**, including for `case` blocks: `case X: { ... break; }`.
- Functions and variables, local variables included, use `snake_case`. Types use `PascalCase`. Enum members use `TypeName_Value` (`HttpMethod_GET`, `HttpStatus_Not_Found`), and the first member is the zero/none value (`HttpMethod_NONE = 0`).
- Functions are prefixed with their module: `arena_`, `temp_memory_`, `string_`, `http_parser_`, `parse_http_`.
- Declare structs as `typedef struct Name { ... } Name;`.
- The pointer star goes with the name: `char *text`.
- Zero-initialize with `= {0}` and set fields with designated initializers (`(Arena){0}`, `{ .buffer = ..., }`).
- Return small structs by value (`HttpRequest parse_http_request(...)`). Pass by pointer when the callee changes the argument (`Arena *`, `HttpMessageParser *`).

## Comments

- Keep comments sparse. Explain *why* something is done or what is not obvious, not *what* the code does.
- Mark known gaps with `// TODO: ...`.

## Known deviations

Existing code that doesn't follow this guide yet. Fix these when you touch the surrounding code:

- `main.c` still calls the OS and libc directly: `<stdio.h>` (`printf`), `<stdlib.h>` (`_Exit`), `<unistd.h>` (`read`, `close`), `<arpa/inet.h>`/`<sys/socket.h>` (`socket`, `setsockopt`, `bind`, `listen`, `accept`, `send`, `htons`), `<errno.h>` and `<signal.h>`. These move behind the platform layer in phase 5 of `DEVELOPMENT_PLAN.md`.
