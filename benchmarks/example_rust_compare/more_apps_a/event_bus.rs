// Safe-Rust model of examples/expanded/systems_2/event_bus.cp
// No unsafe, no FFI, no external crates. Deterministic publish/subscribe bus.
// Build: rustc -O --edition 2021 event_bus.rs -o event_bus

use std::collections::HashMap;

const EB_MAX_TOPICS: usize = 8;
const EB_TOPIC_MAX: usize = 20;
const EB_MAX_SUBS: usize = 6;
const EB_SUB_MAX: usize = 20;
const EB_PAYLOAD_MAX: usize = 40;

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
struct Topic {
    name: String,
    subs: Vec<String>,
    used: bool,
}

impl Topic {
    fn empty() -> Self {
        Self {
            name: String::new(),
            subs: Vec::new(),
            used: false,
        }
    }
}

struct Bus {
    topics: Vec<Topic>,
    index: HashMap<String, i32>,
    deliveries: Vec<i32>,
    ops: i32,
    refused: i32,
    checkfail: i32,
}

impl Bus {
    fn new() -> Self {
        Self {
            topics: (0..EB_MAX_TOPICS).map(|_| Topic::empty()).collect(),
            index: HashMap::new(),
            deliveries: Vec::new(),
            ops: 0,
            refused: 0,
            checkfail: 0,
        }
    }

    fn refuse(&mut self, op: &str, reason: &str, cat: i32, code: i32) {
        self.refused += 1;
        println!(
            "EB REFUSE {} cat= {} code= {} msg= {}",
            op, cat, code, reason
        );
    }

    fn copy_token(
        &mut self,
        src: Option<&str>,
        cap: usize,
        op: &str,
        what: &str,
    ) -> Result<String, ()> {
        let s = match src {
            Some(v) => v,
            None => {
                self.refuse(op, what, CAT_INVALID, EXIT_UNSUPPORTED);
                return Err(());
            }
        };
        if s.is_empty() || s.len() + 1 > cap {
            self.refuse(op, what, CAT_RANGE, EXIT_BOUNDS);
            return Err(());
        }
        Ok(s.to_string())
    }

    fn find_topic(&self, name: &str) -> i32 {
        let slot = match self.index.get(name) {
            Some(v) => *v,
            None => return -1,
        };
        if slot < 0 || slot as usize >= self.topics.len() {
            return -1;
        }
        let t = &self.topics[slot as usize];
        if !t.used || t.name != name {
            return -1;
        }
        slot
    }

    fn resolve(&mut self, name: Option<&str>, op: &str) -> Result<i32, ()> {
        let nm = self.copy_token(name, EB_TOPIC_MAX, op, "topic")?;
        let slot = self.find_topic(&nm);
        if slot < 0 {
            self.refuse(op, "unknown topic", CAT_INVALID, EXIT_OWNERSHIP);
            return Err(());
        }
        Ok(slot)
    }

    fn find_sub(t: &Topic, who: &str) -> i32 {
        t.subs
            .iter()
            .position(|s| s == who)
            .map(|p| p as i32)
            .unwrap_or(-1)
    }

    fn cmd_topic(&mut self, name: Option<&str>) {
        let nm = match self.copy_token(name, EB_TOPIC_MAX, "topic", "name") {
            Ok(v) => v,
            Err(_) => return,
        };
        if self.find_topic(&nm) >= 0 {
            self.refuse("topic", "duplicate topic", CAT_INVALID, EXIT_OWNERSHIP);
            return;
        }
        let slot = (0..EB_MAX_TOPICS).find(|&s| !self.topics[s].used);
        let slot = match slot {
            Some(v) => v,
            None => {
                self.refuse("topic", "topic table full", CAT_MEMORY, EXIT_RESOURCE);
                return;
            }
        };
        self.topics[slot].name = nm.clone();
        self.topics[slot].used = true;
        self.topics[slot].subs.clear();
        self.index.insert(nm.clone(), slot as i32);
        println!("EB topic {}", nm);
    }

    fn cmd_sub(&mut self, topic: Option<&str>, who: Option<&str>) {
        let slot = match self.resolve(topic, "sub") {
            Ok(v) => v,
            Err(_) => return,
        };
        let w = match self.copy_token(who, EB_SUB_MAX, "sub", "subscriber") {
            Ok(v) => v,
            Err(_) => return,
        };
        let t = &mut self.topics[slot as usize];
        if Self::find_sub(t, &w) >= 0 {
            self.refuse("sub", "duplicate subscription", CAT_INVALID, EXIT_OWNERSHIP);
            return;
        }
        if t.subs.len() >= EB_MAX_SUBS {
            self.refuse("sub", "subscriber list full", CAT_RANGE, EXIT_RESOURCE);
            return;
        }
        t.subs.push(w.clone());
        println!("EB sub {} {}", t.name, w);
    }

    fn cmd_unsub(&mut self, topic: Option<&str>, who: Option<&str>) {
        let slot = match self.resolve(topic, "unsub") {
            Ok(v) => v,
            Err(_) => return,
        };
        let w = match self.copy_token(who, EB_SUB_MAX, "unsub", "subscriber") {
            Ok(v) => v,
            Err(_) => return,
        };
        let t = &mut self.topics[slot as usize];
        let at = Self::find_sub(t, &w);
        if at < 0 {
            self.refuse("unsub", "unknown subscriber", CAT_INVALID, EXIT_OWNERSHIP);
            return;
        }
        t.subs.remove(at as usize);
        println!("EB unsub {} {}", t.name, w);
    }

    fn cmd_pub(&mut self, topic: Option<&str>, payload: Option<&str>) {
        let slot = match self.resolve(topic, "pub") {
            Ok(v) => v,
            Err(_) => return,
        };
        let p = match self.copy_token(payload, EB_PAYLOAD_MAX, "pub", "payload") {
            Ok(v) => v,
            Err(_) => return,
        };
        let t = &self.topics[slot as usize];
        println!("EB pub {} deliver= {} payload= {}", t.name, t.subs.len(), p);
        for sub in &t.subs {
            println!("EB deliver {} -> {} {}", t.name, sub, p);
        }
        self.deliveries.push(t.subs.len() as i32);
    }

    fn cmd_expect(&mut self, topic: Option<&str>, who: Option<&str>) {
        let slot = match self.resolve(topic, "expect") {
            Ok(v) => v,
            Err(_) => return,
        };
        let w = match self.copy_token(who, EB_SUB_MAX, "expect", "subscriber") {
            Ok(v) => v,
            Err(_) => return,
        };
        let t = &self.topics[slot as usize];
        if Self::find_sub(t, &w) < 0 {
            self.checkfail += 1;
            println!("EB expect {} {} FAIL not-subscribed", t.name, w);
            return;
        }
        println!("EB expect {} {} OK subscribed", t.name, w);
    }

    fn cmd_dump(&self) {
        let topics = self.topics.iter().filter(|t| t.used).count();
        println!("EB dump topics= {}", topics);
        for t in &self.topics {
            if !t.used {
                continue;
            }
            println!("EB topic {} subs= {}", t.name, t.subs.len());
            for sub in &t.subs {
                println!("EB has {} {}", t.name, sub);
            }
        }
        println!("EB deliveries n= {}", self.deliveries.len());
        for (k, &d) in self.deliveries.iter().enumerate() {
            println!("EB delivery {} {}", k, d);
        }
    }
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let mut bus = Bus::new();
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
            bus.refuse(&cmd, "bad args", CAT_INVALID, EXIT_UNSUPPORTED);
            i += 1;
        }
        if i < args.len() {
            i += 1; // consume "--"
        }

        bus.ops += 1;
        match cmd.as_str() {
            "topic" => bus.cmd_topic(a1.as_deref()),
            "sub" => bus.cmd_sub(a1.as_deref(), a2.as_deref()),
            "unsub" => bus.cmd_unsub(a1.as_deref(), a2.as_deref()),
            "pub" => bus.cmd_pub(a1.as_deref(), a2.as_deref()),
            "expect" => bus.cmd_expect(a1.as_deref(), a2.as_deref()),
            "dump" => bus.cmd_dump(),
            _ => bus.refuse(&cmd, "unknown command", CAT_INVALID, EXIT_UNSUPPORTED),
        }
    }

    if args.len() < 2 {
        bus.refuse("usage", "no commands", CAT_INVALID, EXIT_UNSUPPORTED);
    }

    println!(
        "EB_SUMMARY {} {} {} {}",
        bus.ops,
        bus.ops - bus.refused,
        bus.refused,
        bus.checkfail
    );
    std::process::exit(if bus.checkfail != 0 { 1 } else { 0 });
}
