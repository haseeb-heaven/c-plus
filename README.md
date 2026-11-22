<!-- C+ project README -->
<h1 align="center">C+</h1>

<p align="center">
  <img src="docs/assets/c-plus-logo.svg" alt="C+ — The safest next version C++ never shipped" width="620">
</p>

<p align="center">
  <strong>The safest next version C++ never shipped.</strong><br>
  An experimental C-compatible language focused on compiler-assisted safety and native performance.
</p>

<p align="center">
  <a href="https://github.com/haseeb-heaven/c-plus/stargazers"><img src="https://img.shields.io/github/stars/haseeb-heaven/c-plus?style=for-the-badge&logo=github&color=gold" alt="GitHub stars"></a>
  <a href="https://github.com/haseeb-heaven/c-plus/blob/develop/LICENSE"><img src="https://img.shields.io/github/license/haseeb-heaven/c-plus?style=for-the-badge&color=blue" alt="MIT license"></a>
  <img src="https://img.shields.io/badge/language-C%2B%2B20-00599C?style=for-the-badge&logo=cplusplus&logoColor=white" alt="C++20">
  <img src="https://img.shields.io/badge/status-experimental-orange?style=for-the-badge" alt="Experimental">
</p>

---

## ✨ What is C+?

C+ keeps familiar C syntax and adds compiler-assisted safety checks, deterministic resource cleanup, managed allocation, and a growing standard-library layer.

## 🛡️ How C+ safety works

C+ uses multiple layers. No single layer can make arbitrary C code safe.

### 1. Compile-time checks — Clang / LLVM toolchain

Before code generation, C+ lowers its extensions to C and runs static checks using the Clang AST, C+ analysis passes, and Clang diagnostics.

- Rejects provable constant and supported counted-loop out-of-bounds indexes.
- Detects supported provable null dereferences, ownership copies, use-after-move, and borrowed-pointer escapes.
- Promotes selected compiler warnings to errors, including uninitialized reads, returning stack addresses, and unsafe format usage.
- Inserts deterministic cleanup for supported resource scopes.

Clang/LLVM then performs C compilation, optimization, code generation, and linking. LLVM is the native backend; the safety checks come from the C+ analysis and Clang diagnostics, not from a claim that LLVM proves every program safe.

### 2. Runtime checks — C+ runtime and sanitizers

Safe builds enable Clang's UndefinedBehaviorSanitizer checks for supported bounds, null, alignment, and undefined-behaviour cases. Runtime failures stop execution instead of continuing.

The C+ runtime also provides:
- Managed allocation tracking and allocation-size metadata.
- Detection of managed double-free and invalid release.
- Explicit live-allocation checks and checked array-index helpers.
- Reverse-order cleanup for managed allocations and resource destructors.
- Deterministic diagnostics and exit codes for runtime safety failures.

### 3. Standard and safer libraries

The C+ standard-library layer builds on familiar C/POSIX facilities and adds safer interfaces where implemented.

- `String` owns its buffer through the C+ managed allocator and releases it at scope cleanup.
- `IntVec` tracks length and capacity and uses managed storage.
- Format-free `print` / `scan` helpers reduce format-string mistakes; string input is bounded.
- Checked library APIs are intended to validate sizes, arguments, and operation results at library boundaries.

The library work is still partial. The current standard-library headers are a small layer, not a complete ISO C replacement. Additional hardened libc and checked-API integration is tracked in [Issue #22](https://github.com/haseeb-heaven/c-plus/issues/22) and [PR #17](https://github.com/haseeb-heaven/c-plus/pull/17).

> [!WARNING]
> **Experimental software — not a blanket memory-safety guarantee.** Raw C pointers and legacy C operations can still be unsafe. General use-after-free detection, complete pointer-arithmetic checking, full borrow checking, and complete ISO C coverage are not implemented. AddressSanitizer is not enabled in the documented macOS environment because its runtime fails during initialization. See [limitations](docs/LIMITATIONS.md).

## 🚀 Quick start

**Requirements:** CMake, a C++20 compiler, and LLVM/Clang development packages.

```sh
cmake -S . -B build
cmake --build build --target c+ -- -j4
./build/c+ build demos/A_hello.c -o hello
./hello
```

### CLI

| Command | Action |
|:--|:--|
| `./build/c+ build file.c -o app` | Compile |
| `./build/c+ run file.c -o app` | Build and run |
| `./build/c+ check file.c` | Check source |
| `./build/c+ test` | Run tests |
| `./build/c+ demos` | Run demos |
| `./build/c+ bench` | Run benchmarks |

## 📊 Development status

| Component | Status |
|:--|:--|
| Compile-time checks | 🟡 Implemented for a documented subset |
| Runtime checks | 🟡 UBSan + C+ runtime checks |
| RAII and managed memory | 🟡 Implemented with documented limits |
| Standard / safer libraries | 🟡 Partial; expanding |
| Strict safe mode | 🚧 Enforcement and coverage in progress |

### Current work

- [Compiler development · Issue #21](https://github.com/haseeb-heaven/c-plus/issues/21) · [PR #16](https://github.com/haseeb-heaven/c-plus/pull/16)
- [Hardened library · Issue #22](https://github.com/haseeb-heaven/c-plus/issues/22) · [PR #17](https://github.com/haseeb-heaven/c-plus/pull/17)
- [Verification and release gate · Issue #23](https://github.com/haseeb-heaven/c-plus/issues/23)

## 🧪 Verify

```sh
./tests/run_tests.sh
./demos/run_demos.sh
./benchmarks/run_bench.sh
```

Passing tests do not prove general memory safety. Review the [status](docs/STATUS.md), [limitations](docs/LIMITATIONS.md), [architecture](docs/ARCHITECTURE.md), and [roadmap](docs/ROADMAP.md).

## 🤖 AI-assisted development

Codex and Gemini assisted with implementation. Heaven created the language design and vision, directed technical decisions, and manually tested the project. AI-assisted code remains subject to review and verification.

## 🤝 Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) and [LICENSE](LICENSE).
