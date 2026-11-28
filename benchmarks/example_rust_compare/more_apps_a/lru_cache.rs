// Safe-Rust model of examples/expanded/systems_2/lru_cache.cp
// No unsafe, no FFI, no external crates. Deterministic fixed-capacity LRU.
// Build: rustc -O --edition 2021 lru_cache.rs -o lru_cache

use std::collections::HashMap;

const LRU_MAX_CAP: usize = 8;
const LRU_KEY_MAX: usize = 24;
const LRU_VAL_MAX: usize = 32;
const LRU_MAX_EV: usize = 32;

const CAT_INVALID: i32 = 1;
const CAT_RANGE: i32 = 2;
const CAT_MEMORY: i32 = 4;
const CAT_BOUNDS: i32 = 5;
const CAT_OWNERSHIP: i32 = 6;
const EXIT_BOUNDS: i32 = 101;
const EXIT_OWNERSHIP: i32 = 106;
const EXIT_RESOURCE: i32 = 107;
const EXIT_UNSUPPORTED: i32 = 108;

#[derive(Clone)]
struct Slot {
    key: String,
    val: String,
    used: bool,
}

impl Slot {
    fn empty() -> Self {
        Self {
            key: String::new(),
            val: String::new(),
            used: false,
        }
    }
}

struct Lru {
    slots: Vec<Slot>,
    index: HashMap<String, i32>,
    order: Vec<i32>, // least-recent first
    capacity: i32,
    ops: i32,
    refused: i32,
    checkfail: i32,
    evlog: Vec<String>,
}

impl Lru {
    fn new() -> Self {
        Self {
            slots: (0..LRU_MAX_CAP).map(|_| Slot::empty()).collect(),
            index: HashMap::new(),
            order: Vec::new(),
            capacity: 0,
            ops: 0,
            refused: 0,
            checkfail: 0,
            evlog: Vec::new(),
        }
    }

    fn refuse(&mut self, op: &str, reason: &str, cat: i32, code: i32) {
        self.refused += 1;
        println!(
            "LRU REFUSE {} cat= {} code= {} msg= {}",
            op, cat, code, reason
        );
        eprintln!("lru: error code={} category={} message={}", code, cat, reason);
    }

    // strtol-like: full-string decimal integer within i32 range.
    fn parse_int(text: &str) -> Option<i32> {
        let t = text.trim_start_matches(|c: char| c.is_ascii_whitespace());
        if t.is_empty() {
            return None;
        }
        let (neg, digits) = match t.as_bytes()[0] {
            b'-' => (true, &t[1..]),
            b'+' => (false, &t[1..]),
            _ => (false, t),
        };
        if digits.is_empty() || !digits.bytes().all(|c| c.is_ascii_digit()) {
            return None;
        }
        let value = digits.parse::<i64>().ok()?;
        let value = if neg { -value } else { value };
        if value < i32::MIN as i64 || value > i32::MAX as i64 {
            return None;
        }
        Some(value as i32)
    }

    fn copy_token(&mut self, src: Option<&str>, cap: usize, op: &str, what: &str) -> Result<String, ()> {
        let s = match src {
            Some(v) => v,
            None => {
                self.refuse(op, what, CAT_INVALID, EXIT_UNSUPPORTED);
                return Err(());
            }
        };
        let n = s.len();
        if n == 0 || n + 1 > cap {
            self.refuse(op, what, CAT_RANGE, EXIT_BOUNDS);
            return Err(());
        }
        Ok(s.to_string())
    }

    fn promote(&mut self, slot: i32) {
        if let Some(pos) = self.order.iter().position(|&s| s == slot) {
            self.order.remove(pos);
            self.order.push(slot);
        }
    }

    fn find_slot(&self, key: &str) -> i32 {
        let slot = match self.index.get(key) {
            Some(v) => *v,
            None => return -1,
        };
        if slot < 0 || slot as usize >= self.slots.len() {
            return -1;
        }
        let s = &self.slots[slot as usize];
        if !s.used || s.key != key {
            return -1;
        }
        slot
    }

    fn free_slot(&self) -> i32 {
        for (i, s) in self.slots.iter().enumerate() {
            if !s.used {
                return i as i32;
            }
        }
        -1
    }

    fn cmd_cap(&mut self, a1: Option<&str>) {
        let n = a1.and_then(Self::parse_int);
        let n = match n {
            Some(v) if v > 0 && (v as usize) <= LRU_MAX_CAP => v,
            _ => {
                self.refuse("cap", "capacity out of range", CAT_RANGE, EXIT_BOUNDS);
                return;
            }
        };
        if (n as usize) < self.order.len() {
            self.refuse("cap", "capacity below live entries", CAT_RANGE, EXIT_BOUNDS);
            return;
        }
        self.capacity = n;
        println!("LRU cap {}", n);
    }

    fn cmd_put(&mut self, key: Option<&str>, val: Option<&str>) {
        let k = match self.copy_token(key, LRU_KEY_MAX, "put", "key") {
            Ok(v) => v,
            Err(_) => return,
        };
        let v = match self.copy_token(val, LRU_VAL_MAX, "put", "value") {
            Ok(v) => v,
            Err(_) => return,
        };
        if self.capacity <= 0 {
            self.refuse("put", "zero capacity", CAT_RANGE, EXIT_BOUNDS);
            return;
        }
        let existing = self.find_slot(&k);
        if existing >= 0 {
            let slot = existing as usize;
            self.slots[slot].val = v.clone();
            self.promote(existing);
            println!("LRU PUT {} {} update=1", k, v);
            return;
        }
        let slot = if self.order.len() as i32 >= self.capacity {
            let old = self.order.remove(0);
            let old_key = self.slots[old as usize].key.clone();
            if self.evlog.len() < LRU_MAX_EV {
                self.evlog.push(old_key.clone());
            }
            println!("LRU EVICT {}", old_key);
            self.index.remove(&old_key);
            self.slots[old as usize] = Slot::empty();
            old
        } else {
            self.free_slot()
        };
        if slot < 0 {
            self.refuse("put", "no free slot", CAT_MEMORY, EXIT_RESOURCE);
            return;
        }
        let s = slot as usize;
        self.slots[s].key = k.clone();
        self.slots[s].val = v.clone();
        self.slots[s].used = true;
        self.index.insert(k.clone(), slot);
        self.order.push(slot);
        println!("LRU PUT {} {} update=0", k, v);
    }

    fn cmd_get(&mut self, key: Option<&str>) {
        let k = match self.copy_token(key, LRU_KEY_MAX, "get", "key") {
            Ok(v) => v,
            Err(_) => return,
        };
        let slot = self.find_slot(&k);
        if slot < 0 {
            self.refuse("get", "missing key", CAT_INVALID, EXIT_OWNERSHIP);
            return;
        }
        self.promote(slot);
        println!("LRU GET {} {}", k, self.slots[slot as usize].val);
    }

    fn cmd_expect(&mut self, key: Option<&str>, val: Option<&str>) {
        let k = match self.copy_token(key, LRU_KEY_MAX, "expect", "key") {
            Ok(v) => v,
            Err(_) => return,
        };
        let v = match self.copy_token(val, LRU_VAL_MAX, "expect", "value") {
            Ok(v) => v,
            Err(_) => return,
        };
        let slot = self.find_slot(&k);
        if slot < 0 || self.slots[slot as usize].val != v {
            self.checkfail += 1;
            let got = if slot < 0 {
                "<missing>".to_string()
            } else {
                self.slots[slot as usize].val.clone()
            };
            println!("LRU expect {} FAIL got= {} want= {}", k, got, v);
            return;
        }
        println!("LRU expect {} OK {}", k, v);
    }

    fn cmd_dump(&self) {
        println!("LRU dump n= {}", self.order.len());
        for &slot in self.order.iter().rev() {
            if slot >= 0 && (slot as usize) < self.slots.len() && self.slots[slot as usize].used {
                println!(
                    "LRU entry {} {}",
                    self.slots[slot as usize].key, self.slots[slot as usize].val
                );
            }
        }
        println!("LRU evictions {}", self.evlog.len());
        for k in &self.evlog {
            println!("LRU evicted {}", k);
        }
    }
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let mut lru = Lru::new();
    let mut i = 1usize;

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
            lru.refuse(&cmd, "bad args", CAT_INVALID, EXIT_UNSUPPORTED);
            i += 1;
        }
        if i < args.len() {
            i += 1; // consume "--"
        }

        lru.ops += 1;
        match cmd.as_str() {
            "cap" => lru.cmd_cap(a1.as_deref()),
            "put" => lru.cmd_put(a1.as_deref(), a2.as_deref()),
            "get" => lru.cmd_get(a1.as_deref()),
            "expect" => lru.cmd_expect(a1.as_deref(), a2.as_deref()),
            "dump" => lru.cmd_dump(),
            _ => lru.refuse(&cmd, "unknown command", CAT_INVALID, EXIT_UNSUPPORTED),
        }
    }

    if args.len() < 2 {
        lru.refuse("usage", "no commands", CAT_INVALID, EXIT_UNSUPPORTED);
    }

    println!(
        "LRU_SUMMARY {} {} {} {}",
        lru.ops,
        lru.ops - lru.refused,
        lru.refused,
        lru.checkfail
    );
    std::process::exit(if lru.checkfail != 0 { 1 } else { 0 });
}
