# Development Plan: Porting ScratchPad Code

This plan ports useful parts of an old Windows C++ scratch project (`/home/yago/Projects/ScrachPad/ScratchPad`) to this C11 codebase, and makes the codebase platform-agnostic along the way. The original files are copied to `CodeToPort/` for reference. They're Windows C++, are not compiled, and the folder should be deleted once the port is done.

All ported code must follow `CODING_GUIDE.md`. In particular:
- C11, not C++. No references, overloads, default arguments, `nullptr` or `bool`. Overloads get distinct names.
- Arenas instead of `malloc`. `String` instead of `StringLit` and null-terminated strings.
- `snake_case` with module prefixes. Files with function bodies are `.c` and are `#include`d from `main.c`.
- Client input never reaches `InvalidCodePath`. Bounds checks are real checks, not asserts.
- **New:** the [platform layer](#platform-layer) rules below.

## Scope

| Phase | What | Source (`CodeToPort/`) | Target |
|---|---|---|---|
| 1 | Platform layer + arena helpers + `ArrayCount` | `MemoryArena.h/.cpp`, `Shared.h` | `os.h`, `os_linux.c`, `arena.c`, `base_types.h` |
| 2 | Read entire file | `FileUtilities.h` | `os_linux.c` |
| 3 | JSON parser | `JsonParser.h/.cpp` | `json.c` |
| 4 | Work queue / thread pool | `WorkQueue.h/.cpp` | `work_queue.c` + platform primitives |
| 5 | Move existing platform code behind the platform layer | none (existing `main.c`) | `os_linux.c`, `http_server.c` |

The phases are ordered by dependency. Phase 1 sets up the platform layer that everything else builds on. Phase 3 needs phase 1, and phase 2 provides the test data loading for phase 3. Phase 4 is independent but changes the server's structure. Phase 5 then brings the code that predates this plan under the same rules. Ported code is platform-agnostic from the start; existing code is converted afterwards.

**Out of scope:** `Dictionary.cpp`, `StringBuilder.*`, `JsonWrapper.*` (C++ classes), `Sort.hpp`, `SortTiming.cpp`, `TurnTrig.cpp`, D3D11/D3D12, `nlohmann/` and a Windows implementation of the platform layer. For profiling and Windows support, see [Future work](#future-work).

---

## Platform layer

### Rules

- **Application code only calls functions defined in this codebase.** Calls to the operating system *and to libc* (`printf`, `strtod`, `memcpy`, sockets, `mmap`, pthreads, ...) only happen inside the platform layer.
- **Platform code is as small as possible.** The platform layer provides **primitives**, and the logic built on them stays in shared code. For example, the arena's reserve/commit/decommit logic stays in one `arena.c`, which calls `os_reserve_memory` and friends. There won't be an `arena_linux.c` and an `arena_win32.c`.
- **Use a higher-level platform function only when the platforms differ in the whole operation.** `file_read_entire` is the model here. Opening, sizing and reading a file is entirely OS work, so the whole function is the abstraction.
- Freestanding compiler headers are allowed everywhere: `<stdint.h>`, `<stddef.h>`, `<stdarg.h>`, `<limits.h>`, `<float.h>`. Anything else (`<stdio.h>`, `<stdlib.h>`, `<unistd.h>`, `<sys/...>`, `<stdatomic.h>`, ...) is included **only** by `os_*.c`. You can check this with `grep -n '#include <' *.c *.h`.
- **Compiler differences are not platform differences.** Things like `__builtin_trap` vs MSVC's `__fastfail`/`__debugbreak`, and `_Thread_local` vs `__declspec(thread)`, are handled by a small `#if defined(_MSC_VER)` block in `base_types.h`, not in the platform layer.

### Structure

- **Naming:**
  - Platform functions use the `os_` prefix, and handle types use `Os` (`OsSocket`).
  - The files are `os.h` and `os_<platform>.c`.
  - Atomics are CPU and compiler features rather than OS features, so they use their own `atomic_` prefix. They're still implemented in `os_<platform>.c`.
  - `<stdatomic.h>` is part of the same unity build on Linux. Our atomic names must therefore not clash with its names (`atomic_load`, `atomic_store`, `atomic_exchange`, `atomic_fetch_add`, `atomic_compare_exchange_strong`, ...). Always add a type suffix (`_u32`, `_u64`) and, where it applies, an ordering in the name.
- `os.h` holds the platform API: function declarations plus opaque handle types (`OsSocket`, `OsThread`, `OsSemaphore`).
  - Handles that wrap an OS object which differs in size (`sem_t` vs `HANDLE`) use fixed-size opaque storage, for example `typedef struct OsSemaphore { _Alignas(8) u8 storage[32]; } OsSemaphore;`.
  - Each implementation `static_assert`s that its native type fits.
- `os_linux.c` is the only implementation for now. `main.c` picks the implementation:
  ```c
  #if defined(__linux__)
  #include "os_linux.c"
  #else
  #error "Unsupported platform"
  #endif
  ```
- `assert` goes through `base_types.h`, which keeps `<assert.h>`. `assert` is available on every target and is compiled out in release builds, so it's an accepted exception. If it causes trouble later, replace it with an own `Assert` built on the compiler trap.

### Platform API (grows phase by phase)

| Area | Functions | Added in |
|---|---|---|
| Virtual memory | `os_get_page_size`, `os_reserve_memory`, `os_commit_memory`, `os_decommit_memory`, `os_release_memory` | Phase 1 |
| Files | `os_read_entire_file` | Phase 2 |
| Number parsing | `os_parse_f64` (wraps `strtod`) | Phase 3 |
| Threads and sync | `os_get_processor_count`, `os_thread_create`, `os_semaphore_init/post/wait`, atomics (`atomic_load_acquire_u32`, `atomic_store_release_u32`, `atomic_compare_exchange_u32`, `atomic_increment_u32`) | Phase 4 |
| Console output | `os_print` (wraps `vprintf`) | Phase 5 |
| Sockets | `os_socket_listen`, `os_socket_accept`, `os_socket_read`, `os_socket_send`, `os_socket_close` | Phase 5 |
| Entry point | `main` lives in `os_linux.c` and calls `app_run()` after platform setup | Phase 5 |

Each implementation handles OS details inside the function: `EINTR` retries, `MSG_NOSIGNAL`, `errno`. Failures are reported as return values in our own types, never as `errno`.

---

## Phase 1: Platform layer + arena helpers

- Create `os.h` and `os_linux.c` with the virtual-memory primitives. `arena.c` calls these instead of `mmap`/`mprotect`/`munmap` directly. The arena logic itself (reserve, commit on demand, decommit threshold, temp memory) stays shared.
- `arena_push_aligned(Arena *arena, u64 size, u32 alignment)` pushes with a one-off alignment without changing `arena->alignment`. Our `arena_push` aligns the *end* offset, so the start is only aligned as a side effect of earlier pushes. `arena_push_aligned` must align the *start* explicitly.
- `PushType(arena, type)`, `PushArray(arena, type, count)` and `PushArrayAligned(arena, type, count, alignment)` macros.
- A scratch arena per thread: `Arena *arena_get_scratch(void)` backed by a thread-local `Arena` (a zeroed arena is valid; use the thread-local macro from `base_types.h`). Callers wrap uses in `temp_memory_begin`/`temp_memory_end`. Scratch memory must never be the same arena as a function's output arena.
- `ArrayCount(array)` in `base_types.h`, plus the compiler-difference block (trap, thread-local).
- `memory_copy` and `memory_set` in shared code (simple loops), so ported code doesn't need `<string.h>`.
- `min`, `max` and `clamp` are **not** copied. The originals are broken: `max` is missing a closing parenthesis and `clamp` swaps its minimum and maximum. Add correct versions only when something needs them.
- Add a "Platform layer" section to `CODING_GUIDE.md` with the rules above. List the existing direct OS and libc calls in `main.c` and `string_utils.c` under "Known deviations" until phase 5.

**Done when:** the build is warning-free, `arena.c` has no system includes, and existing behaviour is unchanged (GET/POST with `curl`).

## Phase 2: Read entire file

- `b32 os_read_entire_file(Arena *arena, String path, String *result)` in `os_linux.c`, using `open`/`fstat`/`read` into the arena.
- `open` needs a null-terminated path. Copying `path` with a terminator into the scratch arena is a platform detail, so it stays inside the function.
- Loop on `read` until the whole file is read, handling partial reads and `EINTR`. The original used Windows text-mode `fopen` and ignored short reads.
- Report failures through the return value (missing file, permissions). These are environment errors, not `InvalidCodePath`.

**Done when:** `Test.json` (see phase 3) loads with the correct byte count.

## Phase 3: JSON parser

The structure stays the same: a tokenizer, then a recursive descent parser that builds the node tree in an arena, with a name hash on each object member to speed up lookups. The original was written for trusted files. It has to be hardened, because it will parse request bodies. `json.c` is fully platform-agnostic: no system includes, `memory_copy` instead of `memcpy_s`, and `os_parse_f64` for numbers.

### API

- `JsonDocument json_parse(Arena *arena, String text)`. The document holds the root node plus an error status and position. Any JSON value is accepted at the root; the original required an object.
- `json_get_node_by_name` and `json_get_node_by_index` replace the overloaded `JsonGetNode`. The getters (`json_get_number`, `json_get_string`, `json_get_bool`, typed array getters, `json_is_null`) follow the original's behaviour.
- `json_get_number` on a non-number currently returns the odd sentinel `(f64)0xFFFF...`. Replace this with an explicit success flag or `0` plus a type check.

### Required fixes (untrusted input)

- **Bounds:** every read goes through the tokenizer, which checks against `length`. This removes:
  - the unchecked `*tokenizer.at` reads
  - `case EOF`, which never matches a `u8`
  - the 4-byte `ByteSignaturesMatch` reads past the end
  - the unbounded loop that scans a string
- **Errors:** malformed JSON sets an error on the parser and unwinds. It never hits `InvalidCodePath`/`assert`. The HTTP layer turns a parse error into **400 Bad Request**.
- **Nesting depth limit** (for example 64). Deeply nested input like `[[[[...` would otherwise overflow the stack through recursion.
- **Strings:**
  - Handle escapes correctly. The closing-quote check (`prev != '\\'`) gets `"\\"` wrong.
  - Decode `\" \\ \/ \b \f \n \r \t \uXXXX` (to UTF-8, including surrogate pairs).
  - Zero-copy: a string without escapes is a `String` that points into the source text. Only strings with escapes are decoded into the arena. Drop the null terminator.
- **Numbers:** validate the JSON number grammar in shared code (reject `01`, `1.`, `.5`, a lone `-`). The original's `pow(0.1, n)` fraction maths loses precision. Convert the validated token with `os_parse_f64`. The Linux version copies it into a bounded stack buffer and calls `strtod`; we never call `setlocale`, so `.` is the decimal point. Correct float conversion is hard to write ourselves, so it's delegated to libc behind the platform layer.
- **Arrays:** the original required all elements to have the type of the first, and crashed on `[null]` or mixed arrays. Collect elements as `JsonNode`s in the scratch arena. At the end, store a homogeneous array as a compact typed array (keeping the fast typed getters) and anything else as a `JsonNode` array (a new array type).
- **Hashing:** port `HashString` (Jenkins one-at-a-time) to take a `String` (length-based) instead of a null-terminated string.

### HTTP integration

- If a POST has `Content-Type: application/json`, parse the body. Invalid JSON gets a 400. Valid JSON keeps the current echo behaviour, so it can be tested with `curl`.

### Testing

- This becomes the first automated test. Add a test program as a separate unity build, for example `tests/test_main.c`, built by `build.sh` or a `build_tests.sh`. It uses the platform layer like the server does. It covers:
  - valid documents, checking types, values, escapes and nesting
  - malformed input: truncated text, bad literals, bad numbers, unterminated strings, over-deep nesting
- `Test.json` (9 MB, **not** copied into this repo) is at `/home/yago/Projects/ScrachPad/ScratchPad/Test.json`. Use it as a large parse and regression check: it must parse with no error.

**Done when:** the tests pass in debug and `-DNDEBUG` builds, and malformed JSON POSTed with `curl` gets a 400 without crashing a release build.

## Phase 4: Work queue / thread pool

Port the ring buffer queue and run the server's connection handling on worker threads. The accept loop is the single producer.

### Platform split

The queue's logic is application architecture, so it lives in shared code (`work_queue.c`). This covers the ring buffer, how entries are claimed, completion counting and the worker loop. The platform layer only provides threads, a semaphore and a few atomic operations.

- This keeps the ordering-sensitive code (which store is published when) in one place for every platform. On Windows, the atomics would map to `Interlocked*`, the semaphore to `CreateSemaphore` and threads to `CreateThread`.
- The alternative of putting the whole work queue in the platform layer (`os_work_queue_*`) was rejected. Each platform would duplicate the ring buffer logic and its subtle memory ordering. The platform-specific parts are all simple primitives.
- Atomics are platform functions rather than direct `<stdatomic.h>` use. MSVC's C11 atomics support is still limited, so routing them through the platform layer keeps `work_queue.c` portable.

### Port

- Win32 to platform primitives: `CreateThread` → `os_thread_create`, the semaphore → `os_semaphore_*`, and `Interlocked*`/`volatile` → `atomic_*`. On Linux these are implemented with pthreads, `sem_t` and `<stdatomic.h>`.
- **Fix the memory ordering bug** (the original's `// Why no work :(` comment):
  - the entry must be written before `next_entry_to_write` is published, with a release store
  - readers use acquire loads
  - claiming an entry uses compare-exchange
- `work_queue_add_entry` is **single-producer only**. Document this at the function.
- A full queue currently only asserts. `work_queue_add_entry` returns `false` instead, and the accept loop answers that connection with **503 Service Unavailable**.
- Keep `work_queue_complete_all` for general use. Its reset of the goal/count counters is racy if new work is added during the reset, so document that.
- Worker count: `os_get_processor_count()`. Add `-pthread` to `build.sh`.

### Server changes

- `request_arena` is a single global today. Each worker needs its own: a thread-local arena, or one passed in through the thread startup struct. The scratch arena from phase 1 is already per thread.
- The accept loop pushes the connection as the work item's data. The worker reads, handles and responds, then closes the connection and resets its arena.
- `DEBUG_LOG` output from several threads will interleave. Each print call is thread-safe, but the parts of one response can mix with another's. That's acceptable for debug output, but note it in the code.

**Done when:** several concurrent `curl` requests are served in parallel (for example, a slow client doesn't block others), and a full queue gives a 503.

## Phase 5: Move existing code behind the platform layer

After this phase, no file except `os_linux.c` includes a system header or calls libc.

- **Sockets:** move `create_server`, `accept`, the `read` calls in `read_http_request`, `send_string`'s `send` loop and `close` into `os_socket_*`. HTTP logic (`read_http_request`'s framing, the parser, building responses) stays shared and works on `OsSocket`.
- **Console output:** `printf` → `os_print`. This includes the `DEBUG_LOG` echo and the error messages.
- **Entry point:** `main`, signal setup (`crash_handler`) and closing the server socket on a crash move to `os_linux.c`. `main` sets up the platform, then calls `app_run()`, which holds the accept loop.
- **File layout:** with `main` in the platform file, the HTTP code moves out of `main.c` into `http_server.c`. `main.c` becomes the unity-build root that only includes files, so it's the same pattern for the test build.
- **Base types:** check that `base_types.h` only uses freestanding headers plus `<assert.h>`. Make sure `string_utils.c` has no libc calls left.
- Update `CODING_GUIDE.md` and `CLAUDE.md`, and clear the platform entries from "Known deviations".

**Done when:** `grep -n '#include <' *.c *.h` shows system headers only in `os_linux.c` (plus the allowed freestanding headers and `<assert.h>`), and the server behaves exactly as before.

---

## Future work

Not part of this plan:

- **Profiling (from `DebugUtils.h/.cpp`).** The original uses `__rdtsc`-timed blocks collected in a global table and printed per frame. Porting it means:
  - replacing the C++ class that times a block by its lifetime with explicit begin/end macros
  - a `os_read_cpu_timer()` primitive, or a compiler-level intrinsic in `base_types.h`, since `__rdtsc` is the same instruction on both OSes
  - a table that is safe across threads (per-thread tables or atomics) after phase 4

  The source isn't copied to `CodeToPort/`; it's in the ScratchPad folder.
- **Windows backend.** `os_win32.c` would implement the same API with `VirtualAlloc`, Winsock (`WSAStartup` in `main`), `CreateThread`, `Interlocked*` and so on. The build would also need checking under MSVC's C11 mode (`_Thread_local`, `static_assert`, the compiler block in `base_types.h`).
- **Static file serving** on top of phase 2. Request paths must be normalised and `..` traversal rejected before touching the file system.
