<!-- C+ project README -->
<h1 align="center">C+</h1>

<p align="center">
  <img src="docs/assets/c-plus-logo.svg" alt="C+ — The safest next version C++ never shipped" width="620">
</p>

<p align="center">
  <strong>The safest next version C++ never shipped.</strong><br>
  An experimental C-compatible language focused on compiler-driven safety and native performance.
</p>

<p align="center">
  <a href="https://github.com/haseeb-heaven/c-plus/stargazers"><img src="https://img.shields.io/github/stars/haseeb-heaven/c-plus?style=for-the-badge&logo=github&color=gold" alt="GitHub stars"></a>
  <a href="https://github.com/haseeb-heaven/c-plus/blob/develop/LICENSE"><img src="https://img.shields.io/github/license/haseeb-heaven/c-plus?style=for-the-badge&color=blue" alt="MIT license"></a>
  <img src="https://img.shields.io/badge/language-C%2B%2B20-00599C?style=for-the-badge&logo=cplusplus&logoColor=white" alt="C++20">
  <img src="https://img.shields.io/badge/status-experimental-orange?style=for-the-badge" alt="Experimental">
</p>

---

## ✨ What is C+?

C+ aims to retain familiar C syntax while adding compiler-driven memory-safety analysis, optional modern language features, and a bundled hardened C library.

### Core goals

- 🛡️ **Safety analysis** — ownership, lifetime, alias and bounds analysis.
- ⚙️ **Familiar syntax** — C-compatible programming without mandatory borrow annotations.
- 🧩 **Modern features** — optional RAII and struct-backed methods.
- 📚 **Hardened library** — bundled C library and selected checked APIs.
- 🚀 **Native toolchain** — Clang/LLVM output and supported C ABI compatibility.

> [!WARNING]
> **Experimental software.** C+ does not yet guarantee memory safety for arbitrary programs or complete ISO C compatibility. Strict safe mode must reject operations it cannot prove safe or enforce soundly. See [known limitations](docs/LIMITATIONS.md).

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
| Compiler safety analysis | 🟡 In development |
| RAII and methods | 🟡 Partial |
| Standard library | 🟡 Partial; not a complete ISO C replacement |
| Strict safe mode | 🚧 Fail-closed enforcement in progress |

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
