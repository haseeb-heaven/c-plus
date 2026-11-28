// Safe-Rust model of examples/expanded/systems_2/ring_buffer.cp
// No unsafe, no FFI, no external crates. Deterministic byte ring buffer.
// Build: rustc -O --edition 2021 ring_buffer.rs -o ring_buffer

const RB_MAX_CAP: usize = 32;
const RB_MAX_ARGS: usize = 40;
const RB_MAX_BATCH: usize = 32;

const CAT_INVALID: i32 = 1;
const CAT_RANGE: i32 = 2;
const CAT_MEMORY: i32 = 4;
const EXIT_BOUNDS: i32 = 101;
const EXIT_RESOURCE: i32 = 107;
const EXIT_UNSUPPORTED: i32 = 108;

struct Ring {
    buf: Vec<u8>,
    head: i32,
    used: i32,
    capacity: i32,
    log: Vec<i32>,
    ops: i32,
    refused: i32,
    checkfail: i32,
}

impl Ring {
    fn new() -> Self {
        Self {
            buf: vec![0u8; RB_MAX_CAP],
            head: 0,
            used: 0,
            capacity: 0,
            log: Vec::new(),
            ops: 0,
            refused: 0,
            checkfail: 0,
        }
    }

    fn refuse(&mut self, op: &str, reason: &str, cat: i32, code: i32) {
        self.refused += 1;
        println!(
            "RB REFUSE {} cat= {} code= {} msg= {}",
            op, cat, code, reason
        );
    }

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
        let v = digits.parse::<i64>().ok()?;
        let v = if neg { -v } else { v };
        if v < i32::MIN as i64 || v > i32::MAX as i64 {
            return None;
        }
        Some(v as i32)
    }

    fn parse_byte(&mut self, token: Option<&str>, op: &str) -> Result<u8, ()> {
        let t = match token {
            Some(v) => v,
            None => {
                self.refuse(op, "missing byte", CAT_INVALID, EXIT_UNSUPPORTED);
                return Err(());
            }
        };
        if t.is_empty() || t.len() > 3 {
            self.refuse(op, "byte token length", CAT_RANGE, EXIT_BOUNDS);
            return Err(());
        }
        match Self::parse_int(t) {
            Some(v) if (0..=255).contains(&v) => Ok(v as u8),
            _ => {
                self.refuse(op, "byte out of range", CAT_RANGE, EXIT_BOUNDS);
                Err(())
            }
        }
    }

    fn cmd_cap(&mut self, a1: Option<&str>) {
        let n = a1.and_then(Self::parse_int);
        let n = match n {
            Some(v) if v > 0 && (v as usize) <= RB_MAX_CAP => v,
            _ => {
                self.refuse("cap", "capacity out of range", CAT_RANGE, EXIT_BOUNDS);
                return;
            }
        };
        if n < self.used {
            self.refuse("cap", "capacity below used bytes", CAT_RANGE, EXIT_BOUNDS);
            return;
        }
        self.capacity = n;
        println!("RB cap {}", n);
    }

    fn linear_index(&self, k: i32) -> usize {
        let mut pos = self.head + k;
        if pos >= self.capacity {
            pos -= self.capacity;
        }
        pos as usize
    }

    fn cmd_push(&mut self, tok: &[String]) {
        if self.capacity <= 0 {
            self.refuse("push", "zero capacity", CAT_RANGE, EXIT_BOUNDS);
            return;
        }
        let n = tok.len() as i32 - 1;
        if n <= 0 {
            self.refuse("push", "empty batch", CAT_INVALID, EXIT_UNSUPPORTED);
            return;
        }
        if n as usize > RB_MAX_BATCH {
            self.refuse("push", "batch too large", CAT_RANGE, EXIT_BOUNDS);
            return;
        }
        // Validate every byte before mutating (failure-atomic).
        let mut bytes = Vec::with_capacity(n as usize);
        for t in tok.iter().skip(1) {
            match self.parse_byte(Some(t), "push") {
                Ok(b) => bytes.push(b),
                Err(_) => return,
            }
        }
        if n > self.capacity - self.used {
            self.refuse("push", "overflow", CAT_RANGE, EXIT_RESOURCE);
            return;
        }
        println!("RB PUSH n= {} used= {}", n, self.used);
        for b in bytes {
            let pos = self.linear_index(self.used);
            self.buf[pos] = b;
            self.used += 1;
        }
        println!("RB push_bytes used= {} head= {}", self.used, self.head);
        self.log.push(n);
    }

    fn read_n(&mut self, a1: Option<&str>, op: &str) -> Result<i32, ()> {
        let n = a1.and_then(Self::parse_int);
        let n = match n {
            Some(v) if v > 0 => v,
            _ => {
                self.refuse(op, "count out of range", CAT_RANGE, EXIT_BOUNDS);
                return Err(());
            }
        };
        if n > self.used {
            self.refuse(op, "underflow", CAT_RANGE, EXIT_BOUNDS);
            return Err(());
        }
        if n as usize > RB_MAX_CAP {
            self.refuse(op, "read too large", CAT_RANGE, EXIT_BOUNDS);
            return Err(());
        }
        Ok(n)
    }

    fn cmd_pop(&mut self, a1: Option<&str>) {
        let n = match self.read_n(a1, "pop") {
            Ok(v) => v,
            Err(_) => return,
        };
        println!("RB POP n= {} used= {}", n, self.used);
        for k in 0..n {
            let byte = self.buf[self.head as usize];
            println!("RB popped {} {}", k, byte);
            self.head += 1;
            if self.head >= self.capacity {
                self.head = 0;
            }
            self.used -= 1;
        }
        println!("RB pop_bytes used= {} head= {}", self.used, self.head);
        self.log.push(-n);
    }

    fn cmd_peek(&mut self, a1: Option<&str>) {
        let n = match self.read_n(a1, "peek") {
            Ok(v) => v,
            Err(_) => return,
        };
        println!("RB PEEK n= {} used= {}", n, self.used);
        for k in 0..n {
            let pos = self.linear_index(k);
            println!("RB peeked {} {}", k, self.buf[pos]);
        }
        self.log.push(0);
    }

    fn cmd_expect(&mut self, tok: &[String]) {
        if tok.len() < 2 {
            return;
        }
        let count = match self.read_n(Some(&tok[1]), "expect") {
            Ok(v) => v,
            Err(_) => return,
        };
        if tok.len() as i32 - 2 != count {
            self.refuse("expect", "arity mismatch", CAT_INVALID, EXIT_UNSUPPORTED);
            return;
        }
        for k in 0..count {
            let want = match self.parse_byte(tok.get(2 + k as usize).map(|s| s.as_str()), "expect") {
                Ok(v) => v,
                Err(_) => return,
            };
            let pos = self.linear_index(k);
            let got = self.buf[pos];
            if got != want {
                self.checkfail += 1;
                println!("RB expect {} FAIL got= {} want= {}", k, got, want);
                return;
            }
        }
        println!("RB expect OK n= {}", count);
    }

    fn cmd_dump(&self) {
        println!(
            "RB dump used= {} cap= {} head= {}",
            self.used, self.capacity, self.head
        );
        for i in 0..self.used {
            let pos = self.linear_index(i);
            println!("RB byte {} {}", i, self.buf[pos]);
        }
        println!("RB oplog n= {}", self.log.len());
        for (k, &v) in self.log.iter().enumerate() {
            println!("RB op {} {}", k, v);
        }
    }
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let mut ring = Ring::new();
    let mut i = 1usize;

    while i < args.len() {
        let mut tok: Vec<String> = Vec::new();
        while i < args.len() && tok.len() < RB_MAX_ARGS && args[i] != "--" {
            tok.push(args[i].clone());
            i += 1;
        }
        while i < args.len() && args[i] != "--" {
            i += 1; // drop excess tokens past the fixed window
        }
        if i < args.len() {
            i += 1; // consume "--"
        }
        if tok.is_empty() {
            continue;
        }
        let cmd = tok[0].clone();
        ring.ops += 1;
        match cmd.as_str() {
            "cap" => ring.cmd_cap(tok.get(1).map(|s| s.as_str())),
            "push" => ring.cmd_push(&tok),
            "pop" => ring.cmd_pop(tok.get(1).map(|s| s.as_str())),
            "peek" => ring.cmd_peek(tok.get(1).map(|s| s.as_str())),
            "expect" => ring.cmd_expect(&tok),
            "dump" => ring.cmd_dump(),
            _ => ring.refuse(&cmd, "unknown command", CAT_INVALID, EXIT_UNSUPPORTED),
        }
    }

    if args.len() < 2 {
        ring.refuse("usage", "no commands", CAT_INVALID, EXIT_UNSUPPORTED);
    }

    println!(
        "RB_SUMMARY {} {} {} {}",
        ring.ops,
        ring.ops - ring.refused,
        ring.refused,
        ring.checkfail
    );
    std::process::exit(if ring.checkfail != 0 { 1 } else { 0 });
}
