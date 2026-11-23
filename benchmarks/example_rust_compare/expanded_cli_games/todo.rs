// Safe-Rust model of examples/expanded/cli/todo.cp
// No unsafe, no FFI. In-memory task list with chained commands.
// Build: rustc -O --edition 2021 todo.rs -o todo

use std::fs::{self, File, OpenOptions};
use std::io::{BufRead, BufReader, Write};

const MAX_TASKS: usize = 64;
const TITLE_CAP: usize = 80;

macro_rules! cprint {
    () => { println!() };
    ($($arg:expr),+ $(,)?) => {
        println!("{}", [$($arg.to_string()),+].join(" "))
    };
}

struct Todo {
    ids: Vec<i32>,
    states: Vec<i32>,
    titles: Vec<String>,
}

impl Todo {
    fn new() -> Self {
        Self { ids: Vec::new(), states: Vec::new(), titles: Vec::new() }
    }

    fn find(&self, id: i32) -> Option<usize> {
        self.ids.iter().position(|&x| x == id)
    }

    fn error(what: &str) {
        cprint!("TODO error", what);
    }

    fn set_title(index: usize, text: &str) -> Option<String> {
        if text.len() + 1 > TITLE_CAP {
            return None;
        }
        Some(text.to_string())
    }

    fn list(&self) {
        for i in 0..self.ids.len() {
            cprint!("TODO item", self.ids[i], self.titles[i], self.states[i]);
        }
        cprint!("TODO list", self.ids.len());
    }

    fn load_record(&mut self, line: &str) -> bool {
        let fields: Vec<&str> = line.split('|').collect();
        if fields.len() < 3 {
            return false;
        }
        let id = match parse_int(fields[0]) { Some(v) => v, None => return false };
        let done = match parse_int(fields[1]) { Some(v) => v, None => return false };
        let title = fields[2];
        if done < 0 || done > 1 || id < 1 {
            return false;
        }
        if self.find(id).is_some() || self.ids.len() >= MAX_TASKS {
            return false;
        }
        let t = match Self::set_title(0, title) { Some(t) => t, None => return false };
        self.ids.push(id);
        self.states.push(done);
        self.titles.push(t);
        true
    }

    fn execute(&mut self, cmd: &str, a1: Option<&str>, a2: Option<&str>) -> i32 {
        match cmd {
            "add" => {
                let (id, title) = match (a1, a2) {
                    (Some(a), Some(b)) => match parse_int(a) {
                        Some(v) => (v, b),
                        None => { Self::error("bad-add"); return 1; }
                    },
                    _ => { Self::error("bad-add"); return 1; }
                };
                if id < 1 { Self::error("bad-add"); return 1; }
                if self.find(id).is_some() { Self::error("duplicate"); return 1; }
                if self.ids.len() >= MAX_TASKS { Self::error("full"); return 1; }
                let t = match Self::set_title(0, title) {
                    Some(t) => t,
                    None => { Self::error("bad-title"); return 1; }
                };
                self.ids.push(id);
                self.states.push(0);
                self.titles.push(t);
                let pos = self.ids.len() - 1;
                cprint!("TODO add", id, self.titles[pos]);
                0
            }
            "list" => {
                if a1.is_some() { Self::error("bad-list"); return 1; }
                self.list();
                0
            }
            "done" | "remove" => {
                let is_remove = cmd == "remove";
                let id = match a1.and_then(parse_int) {
                    Some(v) => v,
                    None => {
                        Self::error(if is_remove { "bad-remove" } else { "bad-done" });
                        return 1;
                    }
                };
                if id < 1 {
                    Self::error(if is_remove { "bad-remove" } else { "bad-done" });
                    return 1;
                }
                let pos = match self.find(id) {
                    Some(p) => p,
                    None => { Self::error("not-found"); return 1; }
                };
                if is_remove {
                    self.ids.remove(pos);
                    self.states.remove(pos);
                    self.titles.remove(pos);
                    cprint!("TODO remove", id);
                } else {
                    self.states[pos] = 1;
                    cprint!("TODO done", id);
                }
                0
            }
            "save" | "load" => {
                let is_load = cmd == "load";
                let path = match a1 {
                    Some(p) => p,
                    None => {
                        Self::error(if is_load { "bad-load" } else { "bad-save" });
                        return 1;
                    }
                };
                if is_load {
                    let f = match File::open(path) {
                        Ok(f) => f,
                        Err(_) => { Self::error("cannot-open"); return 1; }
                    };
                    let mut loaded = 0;
                    let mut rc = 0;
                    for line in BufReader::new(f).lines() {
                        let line = match line { Ok(l) => l, Err(_) => { rc = 1; break; } };
                        if self.load_record(&line) { loaded += 1; }
                        if self.ids.len() >= MAX_TASKS { break; }
                    }
                    if rc == 0 {
                        cprint!("TODO load", loaded, path);
                        self.list();
                    } else {
                        Self::error("read-failed");
                    }
                    rc
                } else {
                    let mut f = match File::create(path) {
                        Ok(f) => f,
                        Err(_) => { Self::error("cannot-open"); return 1; }
                    };
                    let mut rc = 0;
                    for i in 0..self.ids.len() {
                        let line = format!("{}|{}|{}\n", self.ids[i], self.states[i], self.titles[i]);
                        if f.write_all(line.as_bytes()).is_err() {
                            Self::error("write-failed");
                            rc = 1;
                            break;
                        }
                    }
                    if rc == 0 { cprint!("TODO save", self.ids.len(), path); }
                    rc
                }
            }
            _ => { Self::error("unknown-command"); 1 }
        }
    }
}

fn parse_int(s: &str) -> Option<i32> {
    let v: i64 = s.parse().ok()?;
    if v >= i32::MIN as i64 && v <= i32::MAX as i64 { Some(v as i32) } else { None }
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let mut todo = Todo::new();
    let mut rc = 0;
    if args.len() < 2 {
        Todo::error("usage");
        rc = 2;
    } else {
        let mut i = 1;
        while i < args.len() {
            let cmd = args[i].clone();
            i += 1;
            let a1 = if i < args.len() && args[i] != "--" {
                let v = args[i].clone(); i += 1; Some(v)
            } else { None };
            let a2 = if i < args.len() && args[i] != "--" {
                let v = args[i].clone(); i += 1; Some(v)
            } else { None };
            while i < args.len() && args[i] != "--" {
                Todo::error("bad-args");
                rc = 1;
                i += 1;
            }
            if i < args.len() { i += 1; }
            if todo.execute(&cmd, a1.as_deref(), a2.as_deref()) != 0 { rc = 1; }
        }
    }
    std::process::exit(rc);
}
