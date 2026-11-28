/* SAFE-Rust model of examples/secure_apps/vault/vault.cp
 *
 * The deterministic observable core: fixed-capacity credential table (a
 * Vec-backed HashMap equivalent), ordered audit records, canary summary and
 * typed refusals.  No files, no clock, no unsafe, no FFI, no real secrets.
 */

use std::env;

const MAX: usize = 8;
const NAME_MAX: usize = 16;
const SECRET_MAX: usize = 32;
const AUDIT_CAP: usize = 512;
const REC_LEN: usize = 31;

struct Vault {
    entries: Vec<(String, String)>,
    audit: Vec<String>,
    seq: u32,
    ops: i32,
    refusals: i32,
}

impl Vault {
    fn new() -> Self {
        Vault { entries: Vec::new(), audit: Vec::new(), seq: 0, ops: 0, refusals: 0 }
    }

    fn refuse(&mut self, reason: &str, code: i32, cat: &str) -> i32 {
        println!("VAULT refuse {} code {} cat {}", reason, code, cat);
        self.refusals += 1;
        1
    }

    fn audit_used(&self) -> usize {
        self.audit.len() * REC_LEN
    }

    fn audit_append(&mut self, op: &str, arg1: Option<&str>) -> i32 {
        let name = arg1.unwrap_or("");
        let projected: String = String::from_utf8_lossy(
            &name.as_bytes()[..name.len().min(NAME_MAX)],
        ).into_owned();
        let rec = format!("{:04} {:<8} {:<16}", self.seq, op, projected);
        if rec.len() + 1 > AUDIT_CAP - self.audit_used() {
            return self.refuse("audit-overflow", 107, "memory");
        }
        self.audit.push(rec);
        self.seq += 1;
        0
    }

    fn find(&self, name: &str) -> Option<usize> {
        self.entries.iter().position(|(n, _)| n == name)
    }

    fn add(&mut self, name: &str, secret: &str) -> i32 {
        let nl = name.len();
        let sl = secret.len();
        if nl == 0 { return self.refuse("empty-name", 108, "invalid"); }
        if sl == 0 { return self.refuse("empty-secret", 108, "invalid"); }
        if nl > NAME_MAX { return self.refuse("oversized-name", 101, "range"); }
        if sl > SECRET_MAX { return self.refuse("oversized-secret", 101, "range"); }
        if self.find(name).is_some() { return self.refuse("duplicate-service", 108, "invalid"); }
        if self.entries.len() >= MAX { return self.refuse("vault-full", 101, "range"); }
        self.entries.push((name.to_string(), secret.to_string()));
        println!("VAULT ok add {}", name);
        0
    }

    fn get(&mut self, name: &str) -> i32 {
        match self.find(name) {
            None => self.refuse("missing-service", 108, "invalid"),
            Some(pos) => {
                println!("VAULT get {} {}", self.entries[pos].0, self.entries[pos].1);
                0
            }
        }
    }

    fn list(&self) {
        println!("VAULT list {}", self.entries.len());
        for (i, (name, _)) in self.entries.iter().enumerate() {
            println!("VAULT entry {} {}", i, name);
        }
    }

    fn peek(&mut self, text: &str) -> i32 {
        let idx = text.parse::<i64>();
        match idx {
            Ok(v) if v >= 0 && (v as usize) < self.entries.len() && (v as usize) < MAX => {
                println!("VAULT peek {} {}", v, self.entries[v as usize].0);
                0
            }
            _ => self.refuse("index-out-of-range", 101, "range"),
        }
    }

    fn delete(&mut self, name: &str) -> i32 {
        match self.find(name) {
            None => self.refuse("missing-service", 108, "invalid"),
            Some(pos) => {
                self.entries.remove(pos);
                println!("VAULT ok delete {}", name);
                0
            }
        }
    }

    fn audit_print(&self) {
        println!("VAULT audit bytes {} cap {}", self.audit_used(), AUDIT_CAP);
        for rec in &self.audit {
            println!("VAULT audit-line {}", rec);
        }
    }

    fn execute(&mut self, cmd: &str, a1: Option<&str>, a2: Option<&str>) -> i32 {
        self.ops += 1;
        let audited = matches!(cmd, "add" | "delete" | "get" | "debug");
        if audited {
            let op = cmd;
            if self.audit_append(op, a1) != 0 {
                return 1;
            }
        }
        match cmd {
            "add" => match (a1, a2) {
                (Some(n), Some(s)) => self.add(n, s),
                _ => self.refuse("bad-args", 108, "invalid"),
            },
            "get" => match (a1, a2) {
                (Some(n), None) => self.get(n),
                _ => self.refuse("bad-args", 108, "invalid"),
            },
            "delete" => match (a1, a2) {
                (Some(n), None) => self.delete(n),
                _ => self.refuse("bad-args", 108, "invalid"),
            },
            "peek" => match (a1, a2) {
                (Some(n), None) => self.peek(n),
                _ => self.refuse("bad-args", 108, "invalid"),
            },
            "debug" => match (a1, a2) {
                (Some(_), None) => self.refuse("secret-logging-forbidden", 108, "invalid"),
                _ => self.refuse("bad-args", 108, "invalid"),
            },
            "list" => {
                if a1.is_some() {
                    self.refuse("bad-args", 108, "invalid")
                } else {
                    self.list();
                    0
                }
            }
            "audit" => {
                if a1.is_some() {
                    self.refuse("bad-args", 108, "invalid")
                } else {
                    self.audit_print();
                    0
                }
            }
            _ => self.refuse("unknown-command", 108, "invalid"),
        }
    }
}

fn main() {
    let args: Vec<String> = env::args().skip(1).collect();
    let mut v = Vault::new();
    let mut rc = 0;

    if args.is_empty() {
        v.refuse("usage", 108, "invalid");
        rc = 2;
    } else {
        let mut i = 0usize;
        while i < args.len() {
            let cmd = args[i].as_str();
            i += 1;
            let mut a1: Option<&str> = None;
            let mut a2: Option<&str> = None;
            if i < args.len() && args[i] != "--" {
                a1 = Some(args[i].as_str());
                i += 1;
            }
            if i < args.len() && args[i] != "--" {
                a2 = Some(args[i].as_str());
                i += 1;
            }
            while i < args.len() && args[i] != "--" {
                v.refuse("bad-args", 108, "invalid");
                rc = 1;
                i += 1;
            }
            if i < args.len() {
                i += 1;
            }
            if v.execute(cmd, a1, a2) != 0 {
                rc = 1;
            }
        }
    }

    println!(
        "VAULT summary ops {} refused {} audit-bytes {} canary ok",
        v.ops, v.refusals, v.audit_used()
    );
    std::process::exit(rc);
}
