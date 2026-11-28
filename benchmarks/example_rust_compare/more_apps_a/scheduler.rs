// Safe-Rust model of examples/expanded/systems_2/scheduler.cp
// No unsafe, no FFI, no external crates. Deterministic RR + priority scheduler.
// Build: rustc -O --edition 2021 scheduler.rs -o scheduler

use std::collections::VecDeque;

const SCH_MAX_Q: usize = 8;
const SCH_NAME_MAX: usize = 20;
const SCH_MAX_TASKS: usize = 48;
const SCH_MAX_BURST: i32 = 16;
const SCH_MAX_RUN: i32 = 64;

const CAT_INVALID: i32 = 1;
const CAT_RANGE: i32 = 2;
const CAT_MEMORY: i32 = 4;
const CAT_BOUNDS: i32 = 5;
const CAT_OWNERSHIP: i32 = 6;
const CAT_LIFECYCLE: i32 = 7;
const EXIT_BOUNDS: i32 = 101;
const EXIT_OWNERSHIP: i32 = 106;
const EXIT_RESOURCE: i32 = 107;
const EXIT_UNSUPPORTED: i32 = 108;
const EXIT_USE_AFTER_FREE: i32 = 103;

#[derive(Clone)]
struct Task {
    name: String,
    prio: i32,
    remain: i32,
    live: bool,
}

struct Scheduler {
    tasks: Vec<Task>,
    high: VecDeque<i32>,
    norm: VecDeque<i32>,
    done: Vec<i32>,
    capacity: i32,
    ops: i32,
    refused: i32,
    checkfail: i32,
}

enum Tick {
    Ok,
    Empty,
    Error,
}

impl Scheduler {
    fn new() -> Self {
        Self {
            tasks: Vec::new(),
            high: VecDeque::new(),
            norm: VecDeque::new(),
            done: Vec::new(),
            capacity: 0,
            ops: 0,
            refused: 0,
            checkfail: 0,
        }
    }

    fn refuse(&mut self, op: &str, reason: &str, cat: i32, code: i32) {
        self.refused += 1;
        println!(
            "SCH REFUSE {} cat= {} code= {} msg= {}",
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

    fn copy_token(&mut self, src: Option<&str>, op: &str, what: &str) -> Result<String, ()> {
        let s = match src {
            Some(v) => v,
            None => {
                self.refuse(op, what, CAT_INVALID, EXIT_UNSUPPORTED);
                return Err(());
            }
        };
        if s.is_empty() || s.len() + 1 > SCH_NAME_MAX {
            self.refuse(op, what, CAT_RANGE, EXIT_BOUNDS);
            return Err(());
        }
        Ok(s.to_string())
    }

    fn find_name(&self, name: &str) -> i32 {
        self.tasks
            .iter()
            .position(|t| t.live && t.name == name)
            .map(|p| p as i32)
            .unwrap_or(-1)
    }

    fn waiting(&self) -> usize {
        self.high.len() + self.norm.len()
    }

    fn cmd_capacity(&mut self, a1: Option<&str>) {
        let n = a1.and_then(Self::parse_int);
        let n = match n {
            Some(v) if v > 0 && (v as usize) <= SCH_MAX_Q => v,
            _ => {
                self.refuse(
                    "capacity",
                    "capacity out of range",
                    CAT_RANGE,
                    EXIT_BOUNDS,
                );
                return;
            }
        };
        if (n as usize) < self.waiting() {
            self.refuse(
                "capacity",
                "capacity below waiting tasks",
                CAT_RANGE,
                EXIT_BOUNDS,
            );
            return;
        }
        self.capacity = n;
        println!("SCH capacity {}", n);
    }

    fn cmd_arrive(&mut self, name: Option<&str>, prio_s: Option<&str>, burst_s: Option<&str>) {
        let nm = match self.copy_token(name, "arrive", "name") {
            Ok(v) => v,
            Err(_) => return,
        };
        let prio = match prio_s.and_then(Self::parse_int) {
            Some(v) if v == 0 || v == 1 => v,
            _ => {
                self.refuse("arrive", "invalid priority", CAT_INVALID, EXIT_UNSUPPORTED);
                return;
            }
        };
        let burst = match burst_s.and_then(Self::parse_int) {
            Some(v) if v > 0 && v <= SCH_MAX_BURST => v,
            _ => {
                self.refuse("arrive", "burst out of range", CAT_RANGE, EXIT_BOUNDS);
                return;
            }
        };
        if self.capacity <= 0 {
            self.refuse("arrive", "zero capacity", CAT_RANGE, EXIT_BOUNDS);
            return;
        }
        if self.waiting() >= self.capacity as usize {
            self.refuse("arrive", "queue overflow", CAT_RANGE, EXIT_RESOURCE);
            return;
        }
        if self.find_name(&nm) >= 0 {
            self.refuse("arrive", "duplicate name", CAT_INVALID, EXIT_OWNERSHIP);
            return;
        }
        if self.tasks.len() >= SCH_MAX_TASKS {
            self.refuse("arrive", "task table full", CAT_MEMORY, EXIT_RESOURCE);
            return;
        }
        let id = self.tasks.len() as i32;
        self.tasks.push(Task {
            name: nm.clone(),
            prio,
            remain: burst,
            live: true,
        });
        let q = if prio == 1 {
            &mut self.high
        } else {
            &mut self.norm
        };
        q.push_back(id);
        println!("SCH arrive {} prio= {} burst= {}", nm, prio, burst);
    }

    fn tick(&mut self) -> Tick {
        if !self.high.is_empty() || self.norm.is_empty() {
            if self.high.is_empty() {
                return Tick::Empty;
            }
        }
        let is_high = !self.high.is_empty();
        let id = if is_high {
            self.high.pop_front()
        } else {
            self.norm.pop_front()
        };
        let id = match id {
            Some(v) => v,
            None => return Tick::Empty,
        };
        let t = match self.tasks.get_mut(id as usize) {
            Some(v) if v.live => v,
            _ => {
                self.refuse(
                    "tick",
                    "stale task id",
                    CAT_LIFECYCLE,
                    EXIT_USE_AFTER_FREE,
                );
                return Tick::Error;
            }
        };
        t.remain -= 1;
        println!("SCH run {} prio= {} remain= {}", t.name, t.prio, t.remain);
        let name = t.name.clone();
        let prio = t.prio;
        let remain = t.remain;
        if remain > 0 {
            let q = if prio == 1 {
                &mut self.high
            } else {
                &mut self.norm
            };
            q.push_back(id);
        } else {
            self.tasks[id as usize].live = false;
            self.done.push(id);
            println!("SCH done {}", name);
        }
        Tick::Ok
    }

    fn cmd_tick(&mut self) {
        if let Tick::Empty = self.tick() {
            self.refuse("tick", "empty queue", CAT_RANGE, EXIT_BOUNDS);
        }
    }

    fn cmd_run(&mut self, a1: Option<&str>) {
        let n = match a1.and_then(Self::parse_int) {
            Some(v) if v > 0 && v <= SCH_MAX_RUN => v,
            _ => {
                self.refuse(
                    "run",
                    "run count out of range",
                    CAT_RANGE,
                    EXIT_BOUNDS,
                );
                return;
            }
        };
        for _ in 0..n {
            match self.tick() {
                Tick::Ok => {}
                Tick::Empty => {
                    self.refuse("run", "empty queue", CAT_RANGE, EXIT_BOUNDS);
                    return;
                }
                Tick::Error => return,
            }
        }
    }

    fn dump_queue(&self, label: &str, q: &VecDeque<i32>) {
        println!("SCH queue {} n= {}", label, q.len());
        for &id in q {
            if let Some(t) = self.tasks.get(id as usize) {
                if t.live {
                    println!("SCH waiting {} {} remain= {}", label, t.name, t.remain);
                }
            }
        }
    }

    fn cmd_dump(&self) {
        self.dump_queue("high", &self.high);
        self.dump_queue("norm", &self.norm);
        println!("SCH completed {}", self.done.len());
        for &id in &self.done {
            if let Some(t) = self.tasks.get(id as usize) {
                println!("SCH did {}", t.name);
            }
        }
    }

    fn cmd_expect(&mut self, name: Option<&str>, remain_s: Option<&str>) {
        let nm = match self.copy_token(name, "expect", "name") {
            Ok(v) => v,
            Err(_) => return,
        };
        let want = match remain_s.and_then(Self::parse_int) {
            Some(v) => v,
            None => {
                self.refuse("expect", "invalid remain", CAT_INVALID, EXIT_UNSUPPORTED);
                return;
            }
        };
        let id = self.find_name(&nm);
        if id < 0 {
            self.checkfail += 1;
            println!("SCH expect {} FAIL missing", nm);
            return;
        }
        let remain = self.tasks[id as usize].remain;
        if remain != want {
            self.checkfail += 1;
            println!("SCH expect {} FAIL got= {} want= {}", nm, remain, want);
            return;
        }
        println!("SCH expect {} OK {}", nm, want);
    }
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let mut sch = Scheduler::new();
    let mut i = 1usize;

    while i < args.len() {
        let cmd = args[i].clone();
        i += 1;
        let mut a1: Option<String> = None;
        let mut a2: Option<String> = None;
        let mut a3: Option<String> = None;
        if i < args.len() && args[i] != "--" {
            a1 = Some(args[i].clone());
            i += 1;
        }
        if i < args.len() && args[i] != "--" {
            a2 = Some(args[i].clone());
            i += 1;
        }
        if i < args.len() && args[i] != "--" {
            a3 = Some(args[i].clone());
            i += 1;
        }
        while i < args.len() && args[i] != "--" {
            sch.refuse(&cmd, "bad args", CAT_INVALID, EXIT_UNSUPPORTED);
            i += 1;
        }
        if i < args.len() {
            i += 1; // consume "--"
        }

        sch.ops += 1;
        match cmd.as_str() {
            "capacity" => sch.cmd_capacity(a1.as_deref()),
            "arrive" => sch.cmd_arrive(a1.as_deref(), a2.as_deref(), a3.as_deref()),
            "tick" => sch.cmd_tick(),
            "run" => sch.cmd_run(a1.as_deref()),
            "expect" => sch.cmd_expect(a1.as_deref(), a2.as_deref()),
            "dump" => sch.cmd_dump(),
            _ => sch.refuse(&cmd, "unknown command", CAT_INVALID, EXIT_UNSUPPORTED),
        }
    }

    if args.len() < 2 {
        sch.refuse("usage", "no commands", CAT_INVALID, EXIT_UNSUPPORTED);
    }

    println!(
        "SCH_SUMMARY {} {} {} {}",
        sch.ops,
        sch.ops - sch.refused,
        sch.refused,
        sch.checkfail
    );
    std::process::exit(if sch.checkfail != 0 { 1 } else { 0 });
}
