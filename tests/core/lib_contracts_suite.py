#!/usr/bin/env python3
"""Compile-time libc/stdlib contract regression suite (Agent 1, issue #25).

For every rule below there is a negative case that must be rejected with an
exact diagnostic substring and a positive counterpart that must be accepted.
Contract-loader failures are exercised through CPLUS_CONTRACT_PATH.
"""
import hashlib
import json
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).parents[2]
CC = os.environ.get("CPLUS_BIN", str(ROOT / "build/c+"))
CPLUS_CLANG = os.environ.get("CPLUS_CLANG", "")
OUT = ROOT / "tests/out/core/lib_contracts"
OUT.mkdir(parents=True, exist_ok=True)
ENV = dict(os.environ)
if CPLUS_CLANG:
    ENV["CPLUS_CLANG"] = CPLUS_CLANG

HEAD = "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n"

# name, source, exact expected diagnostic substring
NEGATIVE = {
    "format_nonliteral": (
        HEAD + 'int main(void){char s[16];fgets(s,sizeof s,stdin);printf(s);return 0;}\n',
        "format string in printf is not a string literal (CWE-134; attacker-controlled format)"),
    "format_fn_pointer": (
        HEAD + 'int main(void){char s[16];fgets(s,sizeof s,stdin);'
               'int(*f)(const char*,...)=printf;f(s);return 0;}\n',
        "format string in printf is not a string literal (CWE-134; attacker-controlled format)"),
    "format_percent_n": (
        HEAD + 'int main(void){int n=0;printf("n=%n",&n);return n;}\n',
        "'%n' in printf performs an unverifiable write through a variadic argument"),
    "format_missing_arg": (
        HEAD + 'int main(void){printf("%d %s\\n",42);return 0;}\n',
        "more '%' conversions than data arguments"),
    "format_swapped_width": (
        HEAD + 'int main(void){printf("%*s\\n","admin",16);return 0;}\n',
        "field width should have type"),
    "scanf_unbounded_s": (
        HEAD + 'int main(void){char w[8];fscanf(stdin,"%s",w);return 0;}\n',
        "unbounded '%s' in fscanf writes into fixed-size array 'w'; add a field width"),
    "unterminated_char_array": (
        HEAD + "int main(void){char t[4]={'A','B','C','D'};printf(\"%s\",t);return 0;}\n",
        "char array 't' has no NUL terminator; %s will over-read"),
    "gets_unbounded": (
        HEAD + 'int main(void){char b[8];gets(b);return 0;}\n',
        "gets() performs an unbounded write; use fgets with an explicit capacity"),
    "sprintf_unbounded": (
        HEAD + 'int main(void){char b[8];sprintf(b,"long string");return 0;}\n',
        "sprintf() writes without a destination capacity; use snprintf or cplus_sprintf_s"),
    "calloc_overflow": (
        HEAD + 'int main(void){void*p=calloc(2,(size_t)-1);(void)p;return 0;}\n',
        "calloc count * size overflows size_t; the allocation is smaller than requested"),
    "fgets_size_overflow": (
        HEAD + 'int main(void){char s[8];fgets(s,64,stdin);return 0;}\n',
        "fgets reads 64 bytes into destination 's' of 8 bytes"),
    "fgets_sizeof_pointer": (
        HEAD + 'void f(char*b){fgets(b,sizeof(b),stdin);}\nint main(void){char x[8];f(x);return 0;}\n',
        "sizeof(char*) is the pointer size, not the buffer size"),
    "fgets_ignored_result": (
        HEAD + 'int main(void){char b[32];fgets(b,sizeof b,stdin);printf("%s",b);return 0;}\n',
        "ignored fgets return value leaves 'b' uninitialized on EOF; check the result"),
    "fgets_null_stream": (
        HEAD + 'int main(void){char b[8];fgets(b,sizeof b,NULL);return 0;}\n',
        "null FILE* passed to fgets"),
    "fread_overflow": (
        HEAD + 'int main(void){char b[64];size_t n=(size_t)-1/4+10;'
               'size_t r=fread(b,4,n,stdin);(void)r;return 0;}\n',
        "size * count overflows size_t in fread; the requested transfer wraps to a small value"),
    "fread_dest_overflow": (
        HEAD + 'int main(void){char b[8];size_t r=fread(b,1,64,stdin);(void)r;return 0;}\n',
        "fread reads 64 bytes into destination 'b' of 8 bytes"),
    "fread_zero_uninit": (
        HEAD + 'int main(void){int r[4];size_t n=fread(r,sizeof r,1,stdin);'
               'if(n==0)return r[0];return 0;}\n',
        "read of uninitialized array 'r'; fread returned zero and wrote nothing"),
    "fclose_alias": (
        HEAD + 'int main(void){FILE*f=fopen("t","w");if(!f)return 1;'
               'FILE*g=f;fclose(f);f=NULL;fprintf(g,"x");return 0;}\n',
        "use of closed FILE* 'g'"),
    "snprintf_index": (
        HEAD + 'int main(void){char b[8];int n=snprintf(b,sizeof b,"too long");'
               'b[n]=0;return 0;}\n',
        "snprintf return value used as index into its destination 'b'; it may exceed the buffer capacity on truncation"),
    "strerror_static": (
        '#include <errno.h>\n#include <string.h>\n' + HEAD +
        'int main(void){const char*a=strerror(ENOENT);const char*b=strerror(ENOMEM);'
        'printf("%s|%s",a,b);return 0;}\n',
        "'strerror' keeps static state; a second call invalidates earlier results"),
    "getenv_setenv": (
        HEAD + 'int main(void){setenv("K","v",1);const char*p=getenv("K");'
               'setenv("X","y",1);printf("%s",p);return 0;}\n',
        "getenv result 'p' may dangle after setenv reallocates environ"),
    "checked_capacity_lying": (
        '#include "cplus/cplus.h"\n' + HEAD +
        'int main(void){char d[4];int n=cplus_strcpy(d,100,"abcdefgh");'
        '(void)n;return 0;}\n',
        "the _s argument does not validate the buffer size"),
    "snprintf_capacity_lying": (
        HEAD + 'int main(void){char d[8];int n=snprintf(d,100,"abcdefghijk");'
               '(void)n;return 0;}\n',
        "format write of 100 bytes exceeds destination 'd'"),
    "packet_size_overflow": (
        '#include <stdint.h>\n' + HEAD +
        'struct P{uint32_t count; uint32_t element_size;};\n'
        'int main(void){struct P p={.count=UINT32_MAX,.element_size=16};\n'
        ' size_t b=p.count*p.element_size; unsigned char*q=malloc(b);\n'
        ' if(!q)return 1; for(uint32_t i=0;i<p.count;i++)\n'
        '  memset(q+(size_t)i*p.element_size,0x41,p.element_size);\n'
        ' free(q); return 0;}\n',
        "integer overflow in multiplication assigned to 'b'; the 32-bit result wraps before it is widened"),
    "packet_write_exceeds": (
        '#include <stdint.h>\n' + HEAD +
        'struct P{uint32_t count; uint32_t element_size;};\n'
        'int main(void){struct P p={.count=UINT32_MAX,.element_size=16};\n'
        ' size_t b=p.count*p.element_size; unsigned char*q=malloc(b);\n'
        ' if(!q)return 1; for(uint32_t i=0;i<p.count;i++)\n'
        '  memset(q+(size_t)i*p.element_size,0x41,p.element_size);\n'
        ' free(q); return 0;}\n',
        "write of 68719476720 bytes exceeds allocated object 'q' of 4294967280 bytes"),
}

# name, source, exact expected diagnostic substring
POSITIVE = {
    "format_literal_ok": HEAD + 'int main(void){printf("ok %d\\n",42);return 0;}\n',
    "percent_n_ok": HEAD + 'int main(void){printf("ok\\n");return 0;}\n',
    "scanf_width_ok": HEAD + 'int main(void){char w[8];fscanf(stdin,"%7s",w);return 0;}\n',
    "gets_replaced_ok": HEAD + 'int main(void){char b[8];fgets(b,sizeof b,stdin);return 0;}\n',
    "sprintf_replaced_ok": HEAD + 'int main(void){char b[8];snprintf(b,sizeof b,"x");return 0;}\n',
    "calloc_ok": HEAD + 'int main(void){void*p=calloc(4,sizeof(int));if(!p)return 1;free(p);return 0;}\n',
    "fgets_ok": HEAD + 'int main(void){char b[8];if(fgets(b,sizeof b,stdin))printf("%s",b);return 0;}\n',
    "fread_ok": HEAD + 'int main(void){char b[64];size_t n=fread(b,1,sizeof b,stdin);if(n==0)return 1;return 0;}\n',
    "fclose_ok": HEAD + 'int main(void){FILE*f=fopen("t","w");if(!f)return 1;fclose(f);return 0;}\n',
    "snprintf_index_guard_ok": HEAD + 'int main(void){char b[8];int n=snprintf(b,sizeof b,"x");if(n<8)b[n]=0;return 0;}\n',
    "strerror_ok": '#include <errno.h>\n' + HEAD +
                   'int main(void){printf("%s",strerror(ENOENT));return 0;}\n',
    "checked_capacity_ok": '#include "cplus/cplus.h"\n' + HEAD +
        'int main(void){char d[8];int n=cplus_strcpy(d,sizeof d,"abc");'
        '(void)n;return 0;}\n',
    "packet_size_ok": '#include <stdint.h>\n' + HEAD +
        'struct P{uint32_t count; uint32_t element_size;};\n'
        'int main(void){struct P p={.count=4,.element_size=16};\n'
        ' size_t b=(size_t)p.count*p.element_size; unsigned char*q=malloc(b);\n'
        ' if(!q)return 1; for(uint32_t i=0;i<p.count;i++)\n'
        '  memset(q+(size_t)i*p.element_size,0x41,p.element_size);\n'
        ' free(q); return 0;}\n',
    "packet_size_dynamic": '#include <stdint.h>\n' + HEAD +
        'struct P{uint32_t count; uint32_t element_size;};\n'
        'int main(void){struct P p; scanf("%u %u",&p.count,&p.element_size);\n'
        ' size_t b=(size_t)p.count*p.element_size; unsigned char*q=malloc(b);\n'
        ' if(!q)return 1; for(uint32_t i=0;i<p.count;i++)\n'
        '  memset(q+(size_t)i*p.element_size,0x41,p.element_size);\n'
        ' free(q); return 0;}\n',
}

CONTRACT_FILES = {
    "contract_missing_file": ("/tmp/cplus_missing_contract.json", None,
        "libc contract file not found"),
    "contract_malformed_json": (None, "{ bad json\n",
        "libc contract file is not valid JSON"),
    "contract_bad_version": (None, '{"version":1,"functions":{}}\n',
        "unsupported libc contract version (expected 2)"),
    "contract_unknown_field": (None,
        json.dumps({"version": 2, "functions": {"malloc": {"magicField": 1}}}),
        "unsupported contract field 'magicField'"),
}


def run(src, mode="--strict", contract=None):
    # Content-addressed source name: concurrent test runners share tests/out,
    # so a fixed "case.c" would be clobbered mid-run by another agent.
    tag = hashlib.sha1((mode + "|" + src).encode()).hexdigest()[:12]
    p = OUT / ("case_" + tag + ".c")
    p.write_text(src)
    env = dict(ENV)
    env.pop("CPLUS_CONTRACT_PATH", None)
    if contract:
        path, content, _ = contract
        if content is not None:
            path = str(OUT / (os.path.basename(path) if path
                              else "contract_override.json"))
            pathlib.Path(path).write_text(content)
        env["CPLUS_CONTRACT_PATH"] = path
    r = subprocess.run([CC, "check", mode, str(p)], capture_output=True,
                       text=True, env=env, timeout=120)
    return r.returncode, r.stderr + r.stdout


# ---------------------------------------------------------------------------
# Expanded isolated v2 contract-validation cases (ct_*).
#
# Every case owns its own generated source and JSON under tests/out (which is
# git-ignored), so no fixtures are committed and no case shares mutable state
# with another.  Negative cases assert one exact loader diagnostic substring;
# positive cases require clean acceptance.  `ct_spec` builds a complete v2
# function contract (all 21 required fields, no optional extras) so that each
# negative case differs from a known-good contract in exactly one way.
# ---------------------------------------------------------------------------

def ct_spec(**overrides):
    spec = {
        "params": ["size_t bytes"],
        "nullableArgs": [],
        "ownership": "returns_owned",
        "transfer": "return",
        "extent": "bytes",
        "effects": ["allocates"],
        "borrow": [],
        "failure": "returns NULL",
        "diagnostics": "CPLUS-SAFE-ALLOC",
        "dynamic": ["null-check"],
        "paramTypes": ["size_t bytes"],
        "pointerBounds": "bytes",
        "allocationSize": "bytes",
        "ownershipTransfer": "return",
        "borrowLifetime": "n/a",
        "readEffects": [],
        "writeEffects": [],
        "freeEffects": [],
        "returnState": "returns_owned",
        "failureBehavior": "returns NULL",
        "threadAssumptions": ["not thread-safe"],
    }
    spec.update(overrides)
    return spec


def ct_doc(fn, spec):
    return json.dumps({"version": 2, "functions": {fn: spec}})


CT_MALLOC_SRC = HEAD + (
    'int main(void){void*p=malloc(4);if(!p)return 1;free(p);return 0;}\n')
CT_UNUSED_SRC = (HEAD +
    'void ct_unused_fn(int*p){(void)p;}\n'
    'int main(void){void*p=malloc(4);if(!p)return 1;free(p);return 0;}\n')
CT_NULLABLE_SRC = (HEAD +
    'void ct_nullable_api(int*p){(void)p;}\n'
    'int main(void){ct_nullable_api(0);return 0;}\n')

CONTRACT_CASES = [
    # Loader-level failures (empty issue.function: reported on any call).
    dict(name="ct_missing_file", src=CT_MALLOC_SRC,
         file="ct_missing_file.json", content=None,
         expect="libc contract file not found"),
    dict(name="ct_malformed_json", src=CT_MALLOC_SRC,
         file="ct_malformed_json.json", content='{"version":2,"functions":{\n',
         expect="libc contract file is not valid JSON"),
    dict(name="ct_wrong_version", src=CT_MALLOC_SRC,
         file="ct_wrong_version.json", content='{"version":3,"functions":{}}\n',
         expect="unsupported libc contract version (expected 2)"),

    # Per-function schema validation (issue.function == called 'malloc').
    dict(name="ct_unknown_field", src=CT_MALLOC_SRC,
         file="ct_unknown_field.json",
         content=ct_doc("malloc", ct_spec(bogusField=1)),
         expect="unsupported contract field 'bogusField'"),
    dict(name="ct_missing_required_field", src=CT_MALLOC_SRC,
         file="ct_missing_required_field.json",
         content=json.dumps({"version": 2, "functions": {
             "malloc": {k: v for k, v in ct_spec().items()
                        if k != "threadAssumptions"}}}),
         expect="missing contract field 'threadAssumptions'"),
    dict(name="ct_wrong_array_type", src=CT_MALLOC_SRC,
         file="ct_wrong_array_type.json",
         content=ct_doc("malloc", ct_spec(params="not-an-array")),
         expect="contract field 'params' must be an array"),
    dict(name="ct_invalid_effect_verb", src=CT_MALLOC_SRC,
         file="ct_invalid_effect_verb.json",
         content=ct_doc("malloc", ct_spec(effects=["teleports"])),
         expect="unsupported effect 'teleports'"),
    dict(name="ct_invalid_diagnostics_category", src=CT_MALLOC_SRC,
         file="ct_invalid_diagnostics_category.json",
         content=ct_doc("malloc", ct_spec(diagnostics="WAT-INVALID")),
         expect="invalid diagnostics category"),
    dict(name="ct_nullable_non_integer", src=CT_MALLOC_SRC,
         file="ct_nullable_non_integer.json",
         content=ct_doc("malloc", ct_spec(nullableArgs=["zero"])),
         expect="nullableArgs must contain integers"),

    # Positive cases: a bad contract is ignored until its function is called,
    # and a fully valid minimal contract is accepted without complaint.
    dict(name="ct_unused_function_contract", src=CT_UNUSED_SRC,
         file="ct_unused_function_contract.json",
         content=ct_doc("ct_unused_fn", ct_spec(effects=["teleports"])),
         clean=True),
    dict(name="ct_valid_minimal_contract", src=CT_MALLOC_SRC,
         file="ct_valid_minimal_contract.json",
         content=ct_doc("malloc", ct_spec()),
         clean=True),

    # The category declared by a contract must appear in the diagnostic.
    dict(name="ct_category_honoured", src=CT_NULLABLE_SRC,
         file="ct_category_honoured.json",
         content=ct_doc("ct_nullable_api", ct_spec(
             params=["int *p"], paramTypes=["int *p"], nullableArgs=[0],
             ownership="none", ownershipTransfer="none", returnState="none",
             transfer="none", extent="n/a", pointerBounds="n/a",
             allocationSize="n/a", effects=["borrows"],
             diagnostics="CPLUS-SAFE-HONOURED")),
         expect="null pointer argument 0 passed to ct_nullable_api "
                "(CPLUS-SAFE-HONOURED)"),
]


def run_ct(name, src, filename, content):
    sp = OUT / (name + ".c")
    sp.write_text(src)
    cp = OUT / filename
    env = dict(ENV)
    env.pop("CPLUS_CONTRACT_PATH", None)
    if content is None:
        cp.unlink(missing_ok=True)  # guarantee a genuinely missing file
    else:
        cp.write_text(content)
    env["CPLUS_CONTRACT_PATH"] = str(cp)
    r = subprocess.run([CC, "check", "--strict", str(sp)], capture_output=True,
                       text=True, env=env, timeout=120)
    return r.returncode, r.stderr + r.stdout


results = []
for name, (src, needle) in NEGATIVE.items():
    rc, diag = run(src)
    results.append((name, "negative", rc != 0 and needle in diag, needle,
                    diag.strip().splitlines()[:1]))
for name, src in POSITIVE.items():
    rc, diag = run(src)
    results.append((name, "positive", rc == 0 and "error:" not in diag,
                    "accepted", diag.strip().splitlines()[:1]))
for name, (path, content, needle) in CONTRACT_FILES.items():
    rc, diag = run(HEAD + 'int main(void){void*p=malloc(4);free(p);return 0;}\n',
                   contract=(path, content, needle))
    results.append((name, "contract", rc != 0 and needle in diag, needle,
                    diag.strip().splitlines()[:1]))

for case in CONTRACT_CASES:
    rc, diag = run_ct(case["name"], case["src"], case["file"],
                      case["content"])
    if case.get("clean"):
        ok = rc == 0 and "error:" not in diag
        needle = "accepted"
    else:
        ok = rc != 0 and case["expect"] in diag
        needle = case["expect"]
    results.append((case["name"], "contract_v2", ok, needle,
                    diag.strip().splitlines()[:1]))

# Cross-TU: contracts are enforced in each translation unit independently.
# (True interprocedural ownership across TUs is NOT claimed.)
TU_A = HEAD + 'void tu_a(void){void*p=malloc(8);if(!p)return;free(p);}\n'
TU_B = HEAD + 'void tu_b(void){char s[16];fgets(s,sizeof s,stdin);printf(s);}\n'
pa, pb = OUT / "lib_contracts_tu_a.c", OUT / "lib_contracts_tu_b.c"
pa.write_text(TU_A)
pb.write_text(TU_B)
for name, p, want_reject, needle in [
    ("cross_tu_safe_tu", pa, False, ""),
    ("cross_tu_violating_tu", pb, True,
     "format string in printf is not a string literal (CWE-134; attacker-controlled format)"),
]:
    r = subprocess.run([CC, "check", "--strict", str(p)], capture_output=True,
                       text=True, env=ENV, timeout=120)
    diag = r.stderr + r.stdout
    ok = (r.returncode != 0 and needle in diag) if want_reject else (
        r.returncode == 0 and "error:" not in diag)
    results.append((name, "cross_tu", ok, needle or "accepted",
                    diag.strip().splitlines()[:1]))

# A proven violation must not produce an executable.
build_src = OUT / "packet_no_executable_case.c"
build_out = OUT / "packet_must_not_exist"
build_src.write_text(NEGATIVE["packet_size_overflow"][0])
if build_out.exists():
    build_out.unlink()
b = subprocess.run([CC, "build", str(build_src), "-o", str(build_out)],
                   capture_output=True, text=True, env=ENV, timeout=120)
ok = b.returncode != 0 and not build_out.exists()
results.append(("packet_no_executable", "build", ok, "build refused",
                [f"rc={b.returncode}"]))

passed = sum(1 for _, _, ok, _, _ in results if ok)
lines = [f"lib contracts suite: {passed}/{len(results)} passed"]
for name, kind, ok, needle, diag in results:
    lines.append(f"  {'ok  ' if ok else 'FAIL'} {kind:8} {name:28} "
                 f"expect={needle[:55]!r} got={diag[0][-70:] if diag else ''}")
(OUT / "report.txt").write_text("\n".join(lines) + "\n")
print(lines[0])
if passed != len(results):
    print("\n".join(l for l in lines[1:] if "FAIL" in l))
sys.exit(0 if passed == len(results) else 1)
