// Safe-Rust model of examples/expanded/apps_2/vault.cp
// No unsafe, no FFI, no external crates. Deterministic in-memory password vault.
// Build: rustc -O --edition 2021 vault.rs -o vault
//
// The C+ source uses fixed-capacity tables plus a checked string API.  Here an
// ordered Vec<(String, String)> is used as the map so `list` keeps the exact
// insertion order the C+ table prints; all operations stay bounds-checked.

const VAULT_MAX: usize = 16;
const VAULT_LIMIT: usize = 256; // 256 payload bytes; field capacity is 257

struct Vault {
    entries: Vec<(String, String)>,
    last_range: bool,
}

impl Vault {
    fn new() -> Self {
        Self {
            entries: Vec::new(),
            last_range: false,
        }
    }

    fn refuse(&mut self, reason: &str) {
        self.last_range = false;
        println!("VAULT error {}", reason);
        eprintln!("vault: error code=108 category=1 message={}", reason);
    }

    fn refuse_range(&mut self, reason: &str) {
        self.last_range = true;
        println!("VAULT error {}", reason);
        eprintln!("vault: error code=101 category=5 message={}", reason);
    }

    fn find(&self, service: &str) -> Option<usize> {
        self.entries.iter().position(|(name, _)| name == service)
    }

    fn add(&mut self, service: &str, password: &str) -> bool {
        // Length is validated before copying (substr would truncate silently).
        if service.len() > VAULT_LIMIT {
            self.refuse_range("name-too-long");
            return false;
        }
        if password.len() > VAULT_LIMIT {
            self.refuse_range("password-too-long");
            return false;
        }
        if service.is_empty() {
            self.refuse("empty-name");
            return false;
        }
        if password.is_empty() {
            self.refuse("empty-password");
            return false;
        }
        if self.find(service).is_some() {
            self.refuse("duplicate");
            return false;
        }
        if self.entries.len() >= VAULT_MAX {
            self.refuse_range("full");
            return false;
        }
        self.entries
            .push((service.to_string(), password.to_string()));
        println!("VAULT add {}", service);
        true
    }

    fn get(&mut self, service: &str) -> bool {
        match self.find(service) {
            Some(pos) => {
                // The only place a secret is ever emitted.
                println!("VAULT get {} {}", service, self.entries[pos].1);
                true
            }
            None => {
                self.refuse("missing-key");
                false
            }
        }
    }

    fn list(&self) {
        println!("VAULT list {}", self.entries.len());
        for (i, (name, _)) in self.entries.iter().enumerate() {
            println!("VAULT entry {} {}", i, name); // never the secret
        }
    }

    fn delete(&mut self, service: &str) -> bool {
        match self.find(service) {
            Some(pos) => {
                self.entries.remove(pos);
                println!("VAULT delete {}", service);
                true
            }
            None => {
                self.refuse("missing-key");
                false
            }
        }
    }

    fn execute(&mut self, cmd: Option<&str>, a1: Option<&str>, a2: Option<&str>) -> i32 {
        let cmd = match cmd {
            Some(c) => c,
            None => {
                self.refuse("usage");
                return 2;
            }
        };
        if cmd == "add" {
            if a1.is_none() || a2.is_none() {
                self.refuse("bad-args");
                return 1;
            }
            return if self.add(a1.unwrap(), a2.unwrap()) { 0 } else { 1 };
        }
        if cmd == "get" || cmd == "delete" {
            if a1.is_none() || a2.is_some() {
                self.refuse("bad-args");
                return 1;
            }
            let key = a1.unwrap();
            let ok = if cmd == "delete" {
                self.delete(key)
            } else {
                self.get(key)
            };
            return if ok { 0 } else { 1 };
        }
        if cmd == "list" {
            if a1.is_some() {
                self.refuse("bad-args");
                return 1;
            }
            self.list();
            return 0;
        }
        self.refuse("unknown-command");
        1
    }
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let mut vault = Vault::new();
    if args.len() < 2 {
        vault.refuse("usage");
        std::process::exit(2);
    }

    let mut i = 1usize;
    let mut rc = 0i32;
    while i < args.len() {
        let cmd = args[i].clone();
        i += 1;
        let mut a1: Option<String> = None;
        let mut a2: Option<String> = None;
        if i < args.len() && args[i] != "--" {
            a1 = Some(args[i].clone());
            i += 1;
        }
        if i < args.len() && args[i] != "--" {
            a2 = Some(args[i].clone());
            i += 1;
        }
        while i < args.len() && args[i] != "--" {
            vault.refuse("bad-args");
            rc = 1;
            i += 1;
        }
        if i < args.len() {
            i += 1; // consume "--"
        }
        if vault.execute(Some(&cmd), a1.as_deref(), a2.as_deref()) != 0 {
            rc = 1;
        }
    }
    std::process::exit(rc);
}
