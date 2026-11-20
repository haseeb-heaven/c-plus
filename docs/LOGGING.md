# Logging and telemetry

C+ logging is **disabled by default**. It uses the MIT-licensed `clog` library
vendored from `0xA1M/clog` (license preserved in `third_party/clog/LICENSE`).

## Compiler logging

```sh
c+ build main.cp --log --log-level info -o main
c+ build main.cp --log --log-level debug -o main   # verbose pipeline logs
```

`--log-level` accepts `trace`, `debug`, `info`, `warn`, `error`, `fatal`.
With no `--log`, no logging code runs and no output is emitted.
`--verbose` controls backend command printing; `--log-level debug` controls
pipeline logging.

## Runtime/stdlib logging

When `--log` is used, the driver passes `-DCPLUS_LOG_RUNTIME=1` and the chosen
level to the generated C. Every standard-library method (`String`, `IntVec`, `File`, `Mutex`/`Guard`,
I/O and sockets) and key runtime entry points emit scoped logs, for example:

```text
[DEBUG] String ctor len=3  (string.hp:15 in String_ctor)
```

With logging disabled, every `cplus_log(...)` call expands to `((void)0)`, so
there is no runtime cost.

## Verification

```sh
./tests/logging_test.sh
```
