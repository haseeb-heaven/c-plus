# Deep stdlib test inventory - Agent 2 Task 1

Agent 2 Task 1 - deep stdlib test inventory
Generated: 2026-11-23T03:13:44.909874
ROOT: /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
Compiler: /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/c+
Build: c+ build <test>.cp --strict --sanitize=none -o <exe>  (timeout 60s)
Run:   timeout 30s <exe> < <stdin|/dev/null>

TEST                                 STATUS               BUILD  RUN    STDERR  CONTRACT
deep_collections_collisions          PASS                 0      0            0 docs/stdlib/DEEP_COLLECTIONS_CONTRACT.md
deep_collections_growth              PASS                 0      0            0 docs/stdlib/DEEP_COLLECTIONS_CONTRACT.md
deep_error_vector                    PASS                 0      0            0 docs/stdlib/DEEP_ERROR_VECTOR_CONTRACT.md
deep_file_lifecycle                  MISSING EXPECTATION  0      0            0 docs/stdlib/DEEP_FILE_CONTRACT.md
deep_file_lines                      MISSING EXPECTATION  0      0            0 docs/stdlib/DEEP_FILE_CONTRACT.md
deep_fs_time                         PASS                 0      0            0 docs/stdlib/DEEP_FS_TIME_CONTRACT.md
deep_io_1byte                        PASS                 0      0            0 docs/stdlib/DEEP_IO_CONTRACT.md
deep_io_token                        PASS                 0      0            0 docs/stdlib/DEEP_IO_CONTRACT.md
deep_json_stress                     PASS                 0      0            0 docs/stdlib/DEEP_JSON_CONTRACT.md
deep_json_types                      PASS                 0      0            0 docs/stdlib/DEEP_JSON_CONTRACT.md
deep_net                             PASS                 0      0            0 docs/stdlib/DEEP_NET_CONTRACT.md
deep_string_append                   PASS                 0      0          139 docs/stdlib/DEEP_STRING_CONTRACT.md
deep_string_view                     PASS                 0      0           78 docs/stdlib/DEEP_STRING_CONTRACT.md
deep_thread_guard                    PASS                 0      0           61 docs/stdlib/DEEP_THREAD_CONTRACT.md
deep_thread_nested                   PASS                 0      0            0 docs/stdlib/DEEP_THREAD_CONTRACT.md

SUMMARY: MISSING EXPECTATION=2, PASS=13; total=15

==============================================================================
FAILURE DETAIL: deep_file_lifecycle -> MISSING EXPECTATION
build command: /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/c+ build /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/tests/stdlib/deep_file_lifecycle.cp --strict --sanitize=none -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/agent2_task1/artifacts/deep_file_lifecycle.exe
build rc=0 timeout=False
run command: /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/agent2_task1/artifacts/deep_file_lifecycle.exe < /dev/null
run rc=0 timeout=False
run stdout (last 20 lines):
ok write-file 1
ok read-file 0 roundtrip
ok remove-temp 1
ok null-constructors 1
ok null-handle-ops 1
ok file-temp 1
ok invalid-args 1
ok partial-eof 1
ok seek-failure 1
ok close-double-drop 1
ok borrow-close 1
ok borrow-drop 1
ok move-double-drop 1
ok fd-during 4 7
ok fd-leak 4 4
ok alloc-injection 1
deep-file-lifecycle PASS 0
run stderr (last 20 lines):

detail: contract documents assertion counts but no byte-exact stdout; exit=0, last_line='deep-file-lifecycle PASS 0'
reproduce: cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib && /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/c+ build /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/tests/stdlib/deep_file_lifecycle.cp --strict --sanitize=none -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/agent2_task1/artifacts/deep_file_lifecycle.exe && /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/agent2_task1/artifacts/deep_file_lifecycle.exe < /dev/null
==============================================================================
FAILURE DETAIL: deep_file_lines -> MISSING EXPECTATION
build command: /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/c+ build /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/tests/stdlib/deep_file_lines.cp --strict --sanitize=none -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/agent2_task1/artifacts/deep_file_lines.exe
build rc=0 timeout=False
run command: /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/agent2_task1/artifacts/deep_file_lines.exe < tests/stdlib/deep_file_lines.stdin
run rc=0 timeout=False
run stdout (last 20 lines):
ok normal-line 0 alpha
ok overlong -1 5 
ok aligned 0 beta
ok eof 1
ok one-byte-progress 2 2 1
ok borrowed-close 1
ok overlong-eof -1 5 
ok after-overlong-eof 1
ok exact-fit 0 123456
ok crlf 0 crlf
ok null-handle -1 3
ok null-buffer -1 108
ok zero-length -1
ok oversized-length -1 5
deep-file-lines PASS 0
run stderr (last 20 lines):

detail: contract documents assertion counts but no byte-exact stdout; exit=0, last_line='deep-file-lines PASS 0'
reproduce: cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib && /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/c+ build /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/tests/stdlib/deep_file_lines.cp --strict --sanitize=none -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/agent2_task1/artifacts/deep_file_lines.exe && /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/agent2_task1/artifacts/deep_file_lines.exe < tests/stdlib/deep_file_lines.stdin

ARTIFACTS: /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/agent2_task1/artifacts

## Notes on classifications

- PASS means: strict/no-sanitizer build exit 0, run exit 0 within 30s, and stdout byte-identical to the output pinned in the contract document (or the documented final line where the contract pins only that line).
- MISSING EXPECTATION means: the test built and ran with exit 0 and printed its own self-check success line, but its contract pins assertion counts only, not byte-exact stdout. It is NOT counted as a pass and needs an `.expected` fixture.
- No BUILD FAILURE, TIMEOUT, FAIL or unexecuted test occurred in this run.
- The stderr bytes on `deep_string_append`, `deep_string_view` and `deep_thread_guard` are expected: `DEEP_STRING_CONTRACT.md:61-62` and `DEEP_THREAD_CONTRACT.md:67-68` document that deliberate fatal probes run in forked children emit those lines while the parent exits 0.
