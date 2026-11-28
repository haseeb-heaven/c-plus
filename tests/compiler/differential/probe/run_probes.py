#!/usr/bin/env python3
"""Differential probe runner: clang -std=c11 vs build/c+ --legacy-c --sanitize=none.

For every p*.c probe in this directory:
  1. compile with /usr/bin/clang -std=c11
  2. compile with build/c+ --legacy-c --sanitize=none
  3. run both binaries and compare stdout + exit status
  4. write a detailed report to REPORT.md next to this script

No `|| true`: every command's real return code is recorded.
"""
import pathlib
import subprocess
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[3]           # .../c-plus-core/tests/compiler/differential/probe -> repo root
CPLUS = ROOT / "build" / "c+"
CLANG = "/usr/bin/clang"

PROBES = sorted(HERE.glob("p*.c"))


def run(cmd, **kwargs):
    return subprocess.run(
        [str(c) for c in cmd],
        capture_output=True,
        text=True,
        timeout=120,
        **kwargs,
    )


def fmt_block(text):
    return text.rstrip("\n") if text else "(empty)"


def main():
    results = []
    with tempfile.TemporaryDirectory(prefix="probe-diff-") as td:
        tmp = pathlib.Path(td)
        for src in PROBES:
            name = src.stem
            out_c = tmp / f"{name}.clang"
            out_p = tmp / f"{name}.cplus"

            clang_cmd = [CLANG, "-std=c11", str(src), "-o", str(out_c)]
            cplus_cmd = [CPLUS, "--legacy-c", "--sanitize=none", str(src),
                         "-o", str(out_p)]

            cc = run(clang_cmd)
            cp = run(cplus_cmd)

            rec = {
                "name": name,
                "src": src,
                "source": src.read_text(),
                "clang_cmd": clang_cmd,
                "cplus_cmd": cplus_cmd,
                "cc": cc,
                "cp": cp,
                "rc_clang": None, "rc_cplus": None,
                "out_clang": "", "out_cplus": "",
                "err_clang": "", "err_cplus": "",
                "mismatch": False, "reason": "",
            }

            if cc.returncode != 0 or cp.returncode != 0:
                rec["mismatch"] = True
                parts = []
                if cc.returncode != 0 and cp.returncode == 0:
                    parts.append("clang compile FAILED but c+ compiled")
                elif cc.returncode == 0 and cp.returncode != 0:
                    parts.append("c+ compile FAILED but clang compiled")
                else:
                    parts.append("both compilers FAILED to compile")
                rec["reason"] = "; ".join(parts)
            else:
                rc = run([str(out_c)], cwd=tmp, input="")
                rp = run([str(out_p)], cwd=tmp, input="")
                rec["rc_clang"] = rc.returncode
                rec["rc_cplus"] = rp.returncode
                rec["out_clang"] = rc.stdout
                rec["out_cplus"] = rp.stdout
                rec["err_clang"] = rc.stderr
                rec["err_cplus"] = rp.stderr
                rec["run_clang_cmd"] = [str(out_c)]
                rec["run_cplus_cmd"] = [str(out_p)]

                diffs = []
                if rc.stdout != rp.stdout:
                    diffs.append("stdout differs")
                if rc.returncode != rp.returncode:
                    diffs.append("exit status differs")
                if rc.stderr != rp.stderr:
                    diffs.append("stderr differs")
                if diffs:
                    rec["mismatch"] = True
                    rec["reason"] = "; ".join(diffs)

            results.append(rec)

    write_report(results)

    mismatches = [r for r in results if r["mismatch"]]
    print(f"probe differential: {len(results)} probes, {len(mismatches)} mismatch(es)")
    for r in results:
        tag = "MISMATCH" if r["mismatch"] else "ok"
        print(f"  [{tag:8}] {r['name']}: {r['reason']}")
    if CPLUS.exists() is False:
        print(f"WARNING: {CPLUS} does not exist")
    return 0


def write_report(results):
    lines = []
    lines.append("# Legacy-C differential probe report")
    lines.append("")
    lines.append(f"- Reference compiler: `/usr/bin/clang -std=c11`")
    lines.append(f"- Compiler under test: `{CPLUS} --legacy-c --sanitize=none`")
    lines.append(f"- Total probes: {len(results)}")
    mismatches = [r for r in results if r["mismatch"]]
    lines.append(f"- Mismatches: {len(mismatches)}")
    lines.append("")
    lines.append("## Summary")
    lines.append("")
    lines.append("| probe | status | reason |")
    lines.append("|-------|--------|--------|")
    for r in results:
        lines.append(f"| {r['name']} | {'MISMATCH' if r['mismatch'] else 'ok'} | {r['reason']} |")
    lines.append("")

    if mismatches:
        lines.append("## Mismatch details")
        lines.append("")
        for r in mismatches:
            lines.append(f"### {r['name']}")
            lines.append("")
            lines.append(f"Reason: {r['reason']}")
            lines.append("")
            lines.append("Exact source:")
            lines.append("")
            lines.append("```c")
            lines.append(r["source"].rstrip("\n"))
            lines.append("```")
            lines.append("")
            lines.append("Commands and outputs:")
            lines.append("")
            lines.append("```text")
            lines.append("$ " + " ".join(r["clang_cmd"]))
            lines.append(f"clang compile rc = {r['cc'].returncode}")
            if r["cc"].stderr:
                lines.append("clang compile stderr:")
                lines.append(fmt_block(r["cc"].stderr))
            lines.append("$ " + " ".join(r["cplus_cmd"]))
            lines.append(f"c+ compile rc = {r['cp'].returncode}")
            if r["cp"].stderr:
                lines.append("c+ compile stderr:")
                lines.append(fmt_block(r["cp"].stderr))
            if r["rc_clang"] is not None:
                lines.append("$ " + " ".join(r["run_clang_cmd"]))
                lines.append(f"clang run rc = {r['rc_clang']}")
                lines.append("clang stdout:")
                lines.append(fmt_block(r["out_clang"]))
                if r["err_clang"]:
                    lines.append("clang stderr:")
                    lines.append(fmt_block(r["err_clang"]))
                lines.append("$ " + " ".join(r["run_cplus_cmd"]))
                lines.append(f"c+ run rc = {r['rc_cplus']}")
                lines.append("c+ stdout:")
                lines.append(fmt_block(r["out_cplus"]))
                if r["err_cplus"]:
                    lines.append("c+ stderr:")
                    lines.append(fmt_block(r["err_cplus"]))
            lines.append("```")
            lines.append("")
    else:
        lines.append("No mismatches detected.")
        lines.append("")

    (HERE / "REPORT.md").write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    raise SystemExit(main())
