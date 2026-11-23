/* SAFE-Rust model of examples/secure_apps/firewall/firewall.cp
 *
 * Deterministic ordered rule checks (empty / size / NUL marker / control /
 * traversal / SQLi / policy) plus the "unchecked path" CONTRAST lines.
 * No unsafe, no FFI; all inputs come from argv and all scans are bounded.
 */

use std::env;

struct Verdict {
    ok: bool,
    rule: &'static str,
    code: &'static str,
    message: &'static str,
    errcode: i32,
}

fn refuse(rule: &'static str, code: &'static str, message: &'static str, errcode: i32) -> Verdict {
    Verdict { ok: false, rule, code, message, errcode }
}

fn accepted() -> Verdict {
    Verdict { ok: true, rule: "-", code: "-", message: "accepted", errcode: 0 }
}

fn lower_ascii(s: &str) -> String {
    s.bytes().map(|b| if b.is_ascii_uppercase() { b + 32 } else { b } as char).collect()
}

fn validate(raw: &str, n: usize, policy: &str) -> Verdict {
    if n == 0 {
        return refuse("no-empty", "E_EMPTY", "input is empty", 108);
    }
    if n > 256 {
        return refuse("max-len", "E_SIZE", "input exceeds 256 bytes", 101);
    }
    let low = lower_ascii(raw);
    if raw.contains("%00") || raw.contains("\\0") || raw.contains("\\x00") || low.contains("%u0000") {
        return refuse("no-nul", "E_NUL", "NUL byte injection marker", 108);
    }
    if raw.bytes().any(|c| c < 0x20 || c == 0x7f) {
        return refuse("no-control", "E_CONTROL", "control character", 108);
    }
    if raw.contains("../") || raw.contains("..\\") || raw.contains("/..")
        || raw.contains("\\..") || raw == ".."
    {
        return refuse("no-traversal", "E_TRAVERSAL", "path traversal sequence", 108);
    }
    if low.contains("or 1=1") || low.contains("' or") || low.contains("union select")
        || low.contains("drop table") || raw.contains('\'') || raw.contains('"')
        || raw.contains(';') || raw.contains("--") || raw.contains("/*") || raw.contains("*/")
    {
        return refuse("no-sqli", "E_SQLI", "SQL injection marker", 108);
    }
    match policy {
        "token" => {
            const ALLOW: [&str; 5] = ["alpha", "beta", "gamma", "report", "status"];
            if !ALLOW.contains(&raw) {
                return refuse("allowlist", "E_NOTALLOWED", "token not in allow-list", 108);
            }
        }
        "pathseg" => {
            if raw.bytes().any(|c| !(c.is_ascii_alphanumeric() || c == b'_' || c == b'-' || c == b'.')) {
                return refuse("pattern", "E_NOTALLOWED", "character outside path-segment pattern", 108);
            }
        }
        "sqlsafe" => {
            if raw.bytes().any(|c| !(c.is_ascii_alphanumeric() || c == b'_' || c == b' ')) {
                return refuse("pattern", "E_NOTALLOWED", "character outside sqlsafe pattern", 108);
            }
        }
        "text" => {
            if raw.bytes().any(|c| c < 0x20 || c > 0x7e) {
                return refuse("pattern", "E_NOTALLOWED", "non-printable byte", 108);
            }
        }
        _ => return refuse("usage", "E_POLICY", "unknown policy", 108),
    }
    accepted()
}

fn contrast(policy: &str, raw: &str, n: usize, v: &Verdict) {
    let writes = n as i64 + 1;
    let overflow = writes - 32;
    println!("CONTRAST policy={} naive=strcpy32 writes={} buffer=32 overflow={}", policy, writes, overflow);
    match v.code {
        "E_TRAVERSAL" => {
            println!("CONTRAST flavor=path-concat unchecked=accept escapes_base=1 path=/srv/app/data/{}", raw);
        }
        "E_SQLI" => {
            println!("CONTRAST flavor=sql-format unchecked=accept always_true=1 query=SELECT * FROM users WHERE name='{}'", raw);
        }
        "E_NUL" => {
            let mut cut = raw.find("%00")
                .or_else(|| raw.find("\\0"))
                .or_else(|| raw.find("\\x00"))
                .unwrap_or(0);
            if cut > n { cut = n; }
            if cut > 575 { cut = 575; }
            println!("CONTRAST flavor=nul-truncate unchecked=accept visible={} os_sees_only_prefix=1", &raw[..cut]);
        }
        "E_CONTROL" => {
            let ctrl = raw.bytes().filter(|&c| c < 0x20 || c == 0x7f).count();
            println!("CONTRAST flavor=log-passthrough unchecked=accept control_bytes={} emitted_to_terminal=1", ctrl);
        }
        "E_SIZE" => {
            println!("CONTRAST flavor=unbounded-copy unchecked=corrupts overrun_bytes={}", overflow);
        }
        _ if v.ok => {
            println!("CONTRAST flavor=normal unchecked=accept safe_for_this_input=1");
        }
        _ => {
            println!("CONTRAST flavor=policy unchecked=accept unknown_input_would_pass=1");
        }
    }
}

fn main() {
    let args: Vec<String> = env::args().skip(1).collect();
    if args.len() < 2 {
        println!("FIREWALL usage: firewall <token|pathseg|sqlsafe|text> <input>");
        std::process::exit(3);
    }
    let policy = args[0].as_str();
    let raw = args[1].as_str();
    let n = raw.len().min(512);
    let v = validate(raw, n, policy);
    contrast(policy, raw, n, &v);
    if v.ok {
        println!("OK {}", raw.trim_matches(' '));
    } else {
        println!("REFUSED {} {}", v.rule, v.code);
    }
    println!("DETAIL policy={} message={} errcode={}", policy, v.message, v.errcode);
    println!("FIREWALL verdict={} rule={} code={} errcode={}", if v.ok { "OK" } else { "REFUSED" }, v.rule, v.code, v.errcode);
    if !v.ok { std::process::exit(2); }
}
