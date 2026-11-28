# cplus-lsp (experimental skeleton)

A tiny stdio Language Server Protocol server for C+ .cp / .hp sources.
It is a skeleton, not a full IDE backend: it proves the protocol wiring and
routes diagnostics through the real compiler instead of reimplementing the
analyser. Spec: https://microsoft.github.io/language-server-protocol/

## Protocol handled

| Method | Behaviour |
| --- | --- |
| initialize | Advertises full document sync (textDocumentSync: 1), hoverProvider true, completionProvider with no trigger chars. |
| initialized, $/cancelRequest, textDocument/didSave | Ignored (no-op). |
| textDocument/didOpen | Stores full text keyed by URI, publishes diagnostics. |
| textDocument/didChange | Replaces full text (contentChanges[-1].text), publishes diagnostics. |
| textDocument/didClose | Drops the buffer, clears diagnostics. |
| textDocument/completion | Returns a static CompletionList of the tokens below. |
| textDocument/hover | Static markdown for the identifier under the cursor. |
| shutdown / exit | Standard null result / process exit. |

Framing is LSP standard: Content-Length N, CRLF CRLF, then exactly N bytes of
JSON. Only protocol frames are written to stdout; server self-diagnostics go
to stderr.

## Diagnostics entry point

The server runs the same command the repo .cp build scripts use:

    c+ check --default --sanitize=none <temp .cp>

Both stdout and stderr are captured. Lines matching the compiler format
"file:line:col: error|warning|note|fatal error: message" (including backend
clang findings) become LSP Diagnostic objects with source "c+"; compiler
1-based lines/columns are converted to LSP 0-based ranges. Caret and help:
lines are ignored. The temp file lives in a private mktemp -d removed on exit.

## Static completion / hover tokens

| Token | Meaning | Status |
| --- | --- | --- |
| class | Lightweight struct-based class with methods, lowered to C functions. | Real (compiler/LangFeatures.h) |
| drop | Destructor method (void drop(void)), lowered to T_drop. | Real (docs/LANGUAGE.md) |
| Slice(T) | Proposed borrowed contiguous view of T. | Stub; not compiled today |
| Vec(T) | Proposed owned growable array of T. | Stub; not compiled today |
| unsafe | Proposed escape hatch. | Stub; current compiler rejects it |

These tables are hand-written; completion and hover do not consult the
compiler, so they can advertise tokens the current compiler does not accept.
Read the hover text before using the proposed tokens.

## Build and run

No build step and no CMake target. Requirements: bash, jq (>= 1.6), and a
working c+ binary on PATH or at ./build/c+.

    export CPLUS=$PWD/build/c+          # optional if c+ is on PATH
    export CPLUS_LSP_INCLUDE="-I$PWD -I$PWD/third_party"   # optional, for headers
    chmod +x tools/lsp/cplus-lsp.sh
    tools/lsp/cplus-lsp.sh              # speaks LSP on stdin/stdout

If no compiler is found the server still starts, but publishes empty
diagnostics and prints a warning to stderr.

## Editor setup

Neovim (built-in LSP): register the filetype, then start the server.

    vim.filetype.add({ extension = { cp = "cplus", hp = "cplus" } })
    vim.api.nvim_create_autocmd("FileType", {
      pattern = "cplus",
      callback = function()
        vim.lsp.start({ name = "cplus-lsp",
          cmd = { vim.env.HOME .. "/c-plus/tools/lsp/cplus-lsp.sh" } })
      end,
    })

coc.nvim (coc-settings.json):

    { "languageserver": {
        "cplus": { "command": "/abs/path/c-plus/tools/lsp/cplus-lsp.sh",
                   "filetypes": ["cplus"] } } }

VS Code / Sublime: use any generic LSP client that accepts an external
command, point it at the script, and register .cp/.hp with a "cplus" id.

## Honest scope

- No semantic tokens, go-to-definition, references, rename, formatting,
  folding, or workspace symbols.
- Full sync only: the whole buffer is resent and re-checked on every change;
  no incremental ranges and no project/build model.
- Completion and hover are static stubs, not semantic analysis.
- Diagnostics run c+ check per keystroke; latency is the compiler's, and
  project includes must be supplied through CPLUS_LSP_INCLUDE.
- Standalone script: not part of CMake, CI, or the shipped binary. It is an
  experimental editor aid, not a compiler component.
