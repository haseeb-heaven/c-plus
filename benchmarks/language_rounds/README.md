# Language rounds benchmark

`bench.cp`, `bench.rs`, and `bench.zig` run the *identical* workload: 2,000,000
iterations of an integer mix plus bounded updates over a two-field struct.

```
p.x = (p.x * 3 + p.y) & 0xffffff
p.y = (p.y + i * 7) & 0xffffff
acc += p.x ^ p.y          // u64, wrapping add
```

Every implementation must print `LANG <elapsed_ns> <checksum>` and exit nonzero
if the checksum is zero. The expected checksum is **16776346274400**.

`run.sh` drives the C+ versus Rust rounds and writes `proof/benchmarks/language_rounds.txt`.

## Running the Zig benchmark

Compile with Zig 0.13.0 in release-fast mode and run the produced binary:

```sh
mkdir -p build/lang_rounds
zig build-exe -O ReleaseFast benchmarks/language_rounds/bench.zig \
  -femit-bin=build/lang_rounds/bench_zig
./build/lang_rounds/bench_zig
```

Example output (timing varies by machine; the checksum must be identical):

```
ZIG 2588000 16776346274400
```

The convenience runner `run_zig.sh` performs both steps and asserts the checksum:

```sh
sh benchmarks/language_rounds/run_zig.sh
```

It prints `ZIG_OK <checksum>` on success and `ZIG_FAIL` otherwise. The
`ZIG <ns> <checksum>` timing line is written to stdout by the benchmark itself.
