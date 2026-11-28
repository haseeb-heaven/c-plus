#!/usr/bin/env bash
# cplus-lsp.sh - minimal stdio LSP skeleton for C+ (.cp/.hp) sources.
# Diagnostics come from the real compiler entry point:
#   c+ check --default --sanitize=none <temp .cp>
# No Python, no new CMake target: jq handles JSON framing and messages.
# Honest scope: full-sync only, static completion/hover, no semantic tokens.

set -u

command -v jq >/dev/null 2>&1 || { echo "cplus-lsp: jq is required" >&2; exit 1; }

# stdout is reserved for LSP frames; every log line goes to stderr.
CPLUS=${CPLUS:-}
if [ -z "$CPLUS" ]; then
  if [ -x ./build/c+ ]; then CPLUS=./build/c+
  elif command -v c+ >/dev/null 2>&1; then CPLUS=c+
  fi
fi
[ -n "$CPLUS" ] || echo "cplus-lsp: no c+ found; empty diagnostics (set CPLUS)" >&2

STATE=$(mktemp -d "${TMPDIR:-/tmp}/cplus-lsp.XXXXXX") || exit 1
trap 'rm -rf "$STATE"' EXIT INT TERM
printf '{}\n' > "$STATE/docs.json"

# Advertised tokens. Status is explicit: Slice/Vec/unsafe are proposed
# spellings from the docs, not compiler-validated keywords today.
KEYWORDS='[
  {"label":"class","kind":14,"detail":"C+ class","documentation":"Lightweight struct-based class with methods; lowers to ordinary C functions."},
  {"label":"drop","kind":14,"detail":"C+ destructor","documentation":"void drop(void) on a class/resource struct; lowered to T_drop and run deterministically at scope exit."},
  {"label":"Slice(T)","kind":14,"detail":"proposed generic","documentation":"Proposed borrowed view over a contiguous T sequence; stub only, not compiled today."},
  {"label":"Vec(T)","kind":14,"detail":"proposed generic","documentation":"Proposed owned growable array of T; stub only, not compiled today."},
  {"label":"unsafe","kind":14,"detail":"proposed escape hatch","documentation":"Proposed opt-out marker; the current compiler does not recognize this token."}
]'
HOVER='{
  "class":"**class** - lightweight struct-based class with methods; lowers to C functions (see docs/LANGUAGE.md).",
  "drop":"**drop** - deterministic destructor method on a class/resource struct; lowered to T_drop.",
  "Slice":"**Slice(T)** - proposed borrowed contiguous view of T; completion/hover stub, not compiled today.",
  "Vec":"**Vec(T)** - proposed owned growable array of T; completion/hover stub, not compiled today.",
  "unsafe":"**unsafe** - proposed escape hatch; the current compiler does not recognize it, so diagnostics will reject it."
}'

send() {
  local body=$1 n
  n=$(printf '%s' "$body" | wc -c | tr -d '[:space:]')
  printf 'Content-Length: %s\r\n\r\n%s' "$n" "$body"
}

read_message() {
  local len= line= blank=0
  while IFS= read -r line; do
    line=${line%$'\r'}
    if [ -z "$line" ]; then blank=1; break; fi
    case "$line" in
      [Cc]ontent-[Ll]ength:*) len=${line#*:}; len=$(printf '%s' "$len" | tr -cd '0-9');;
    esac
  done
  [ "$blank" = 1 ] && [ -n "$len" ] || return 1
  # head -c over-reads pipes; dd with fullblock consumes exactly len bytes.
  dd bs="$len" count=1 iflag=fullblock status=none 2>/dev/null
}

send_notification() {
  send "$(jq -n --arg m "$1" --argjson p "$2" '{jsonrpc:"2.0",method:$m,params:$p}')"
}

send_response() {
  send "$(jq -n --argjson id "$1" --argjson r "$2" '{jsonrpc:"2.0",id:$id,result:$r}')"
}

store_doc() {
  jq --arg u "$1" --arg t "$2" --argjson v "$3" \
    '.[$u] = {text:$t,version:$v}' "$STATE/docs.json" > "$STATE/docs.tmp" &&
    mv "$STATE/docs.tmp" "$STATE/docs.json"
}

publish_diagnostics() {
  local uri=$1 text out diags='[]' params
  text=$(jq -r --arg u "$uri" '.[$u].text // ""' "$STATE/docs.json")
  printf '%s\n' "$text" > "$STATE/source.cp"
  if [ -n "$CPLUS" ]; then
    if [ -n "${CPLUS_LSP_INCLUDE:-}" ]; then
      # shellcheck disable=SC2086  # intentional -I flag splitting
      out=$("$CPLUS" check --default --sanitize=none $CPLUS_LSP_INCLUDE "$STATE/source.cp" 2>&1 || true)
    else
      out=$("$CPLUS" check --default --sanitize=none "$STATE/source.cp" 2>&1 || true)
    fi
    # Compiler lines are 1-based; LSP ranges are 0-based. Ignore caret/help lines.
    diags=$(printf '%s\n' "$out" | jq -R -s '
      split("\n")
      | map(select(test("^(.+):([0-9]+):([0-9]+): (error|warning|note|fatal error): (.*)$")))
      | map(capture("^(?<file>.+):(?<line>[0-9]+):(?<col>[0-9]+): (?<sev>error|warning|note|fatal error): (?<msg>.*)$")
          | { range: { start: {line: ((.line|tonumber)-1), character: ((.col|tonumber)-1)},
                       end:   {line: ((.line|tonumber)-1), character: (.col|tonumber)} },
              severity: (if .sev == "error" or .sev == "fatal error" then 1
                         elif .sev == "warning" then 2 else 3 end),
              source: "c+", message: .msg })
    ')
  fi
  params=$(jq -n --arg uri "$uri" --argjson diagnostics "$diags" \
    '{uri:$uri,diagnostics:$diagnostics}')
  send_notification 'textDocument/publishDiagnostics' "$params"
}

while :; do
  body=$(read_message) || break
  [ -n "$body" ] || continue
  meta=$(printf '%s' "$body" | jq -r \
    '[ (.method // ""), (.id|tojson), (.params.textDocument.uri // "") ] | @tsv' 2>/dev/null) || continue
  IFS=$'\t' read -r method id uri <<<"$meta"

  case "$method" in
    initialize)
      send_response "$id" '{"capabilities":{"textDocumentSync":1,"hoverProvider":true,"completionProvider":{"resolveProvider":false,"triggerCharacters":[]}},"serverInfo":{"name":"cplus-lsp","version":"0.1.0"}}'
      ;;
    initialized|\$/cancelRequest|textDocument/didSave) ;;
    shutdown) send_response "$id" 'null' ;;
    exit) exit 0 ;;
    textDocument/didOpen)
      text=$(printf '%s' "$body" | jq -r '.params.textDocument.text // ""')
      store_doc "$uri" "$text" "$(printf '%s' "$body" | jq -r '.params.textDocument.version // 0')"
      publish_diagnostics "$uri"
      ;;
    textDocument/didChange)
      text=$(printf '%s' "$body" | jq -r '.params.contentChanges[-1].text // ""')
      store_doc "$uri" "$text" "$(printf '%s' "$body" | jq -r '.params.textDocument.version // 0')"
      publish_diagnostics "$uri"
      ;;
    textDocument/didClose)
      jq --arg u "$uri" 'del(.[$u])' "$STATE/docs.json" > "$STATE/docs.tmp" &&
        mv "$STATE/docs.tmp" "$STATE/docs.json"
      send_notification 'textDocument/publishDiagnostics' "$(jq -n --arg uri "$uri" '{uri:$uri,diagnostics:[]}')"
      ;;
    textDocument/completion)
      send_response "$id" "$(jq -n --argjson items "$KEYWORDS" '{isIncomplete:false,items:$items}')"
      ;;
    textDocument/hover)
      line=$(printf '%s' "$body" | jq -r '.params.position.line // 0')
      ch=$(printf '%s' "$body" | jq -r '.params.position.character // 0')
      word=$(jq -r --arg u "$uri" --argjson l "$line" --argjson c "$ch" '
        (.[$u].text // "" | split("\n") | .[$l] // "") as $s
        | ($s[0:$c] | [scan("[A-Za-z_][A-Za-z0-9_]*")] | last // "")
          + ($s[$c:] | [scan("^[A-Za-z0-9_]*")] | first // "")
      ' "$STATE/docs.json")
      htext=$(jq -r -n --argjson h "$HOVER" --arg w "$word" '$h[$w] // ""')
      if [ -n "$htext" ]; then
        send_response "$id" "$(jq -n --arg v "$htext" '{contents:{kind:"markdown",value:$v}}')"
      else
        send_response "$id" 'null'
      fi
      ;;
    *)
      if [ "$id" != "null" ]; then
        send "$(jq -n --argjson id "$id" '{jsonrpc:"2.0",id:$id,error:{code:-32601,message:"method not found"}}')"
      fi
      ;;
  esac
done
