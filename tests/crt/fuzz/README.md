# Safe CRT fuzz targets

libFuzzer-compatible targets for #22. Build on a host with libFuzzer+ASan/UBSan (Linux CI expected). This macOS host may not have libFuzzer runtime; compile smoke is attempted when available.

Targets: `fuzz_memcpy_s`, `fuzz_strcpy_s`, `fuzz_alloc_s`, `fuzz_format_s`.
