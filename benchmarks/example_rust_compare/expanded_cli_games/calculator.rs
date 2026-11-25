// Safe-Rust model of examples/expanded/cli/calculator.cp
// No unsafe, no FFI. Deterministic integer expression evaluator.
// Build: rustc -O --edition 2021 calculator.rs -o calculator

use std::io::{self, Read};

const LIMIT: i64 = 1_000_000_000;

macro_rules! cprint {
    () => { println!() };
    ($($arg:expr),+ $(,)?) => {
        println!("{}", [$($arg.to_string()),+].join(" "))
    };
}

struct Parser<'a> {
    src: &'a [u8],
    pos: usize,
    ops: usize,
    err: Option<&'static str>,
}

impl<'a> Parser<'a> {
    fn new(src: &'a [u8]) -> Self {
        Self { src, pos: 0, ops: 0, err: None }
    }

    fn skip(&mut self) {
        while self.pos < self.src.len()
            && (self.src[self.pos] == b' ' || self.src[self.pos] == b'\t')
        {
            self.pos += 1;
        }
    }

    fn peek(&self) -> u8 {
        if self.pos < self.src.len() { self.src[self.pos] } else { 0 }
    }

    fn number(&mut self) -> Option<i64> {
        let mut v: i64 = 0;
        let mut digits = 0;
        while self.pos < self.src.len() && self.src[self.pos].is_ascii_digit() {
            let d = (self.src[self.pos] - b'0') as i64;
            if v > 100_000_000 {
                self.err = Some("overflow");
                return None;
            }
            v = v * 10 + d;
            digits += 1;
            self.pos += 1;
        }
        if digits == 0 {
            self.err = Some("syntax");
            return None;
        }
        if v > LIMIT {
            self.err = Some("overflow");
            return None;
        }
        Some(v)
    }

    fn factor(&mut self) -> Option<i64> {
        self.skip();
        let ch = self.peek();
        if ch == b'+' || ch == b'-' {
            let negative = ch == b'-';
            self.pos += 1;
            let v = self.factor()?;
            Some(if negative { -v } else { v })
        } else if ch == b'(' {
            self.pos += 1;
            let v = self.expr()?;
            self.skip();
            if self.peek() != b')' {
                self.err = Some("syntax");
                return None;
            }
            self.pos += 1;
            Some(v)
        } else {
            self.number()
        }
    }

    fn term(&mut self) -> Option<i64> {
        let mut a = self.factor()?;
        loop {
            self.skip();
            let op = self.peek();
            if op == b'*' {
                self.pos += 1;
                let b = self.factor()?;
                a = a * b; // |a|,|b| <= 1e9 -> fits in i64
                self.ops += 1;
            } else if op == b'/' {
                self.pos += 1;
                let b = self.factor()?;
                if b == 0 {
                    self.err = Some("divide-by-zero");
                    return None;
                }
                a = a / b; // truncates toward zero, as in C
                self.ops += 1;
            } else {
                break;
            }
            if a > LIMIT || a < -LIMIT {
                self.err = Some("overflow");
                return None;
            }
        }
        Some(a)
    }

    fn expr(&mut self) -> Option<i64> {
        let mut a = self.term()?;
        loop {
            self.skip();
            let op = self.peek();
            if op == b'+' {
                self.pos += 1;
                let b = self.term()?;
                a = a + b;
                self.ops += 1;
            } else if op == b'-' {
                self.pos += 1;
                let b = self.term()?;
                a = a - b;
                self.ops += 1;
            } else {
                break;
            }
            if a > LIMIT || a < -LIMIT {
                self.err = Some("overflow");
                return None;
            }
        }
        Some(a)
    }
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let src: String = if args.len() > 1 {
        let s = &args[1];
        if s.len() + 1 > 256 {
            cprint!("CALC ERROR input-too-long");
            std::process::exit(1);
        }
        s.clone()
    } else {
        let mut input = String::new();
        if io::stdin().read_to_string(&mut input).is_err() {
            cprint!("CALC ERROR no-input");
            std::process::exit(1);
        }
        // read_line takes one line, strips the trailing newline.
        let line = input.split('\n').next().unwrap_or("").trim_end_matches('\r').to_string();
        if line.is_empty() {
            cprint!("CALC ERROR no-input");
            std::process::exit(1);
        }
        if line.len() + 1 > 256 {
            cprint!("CALC ERROR input-too-long");
            std::process::exit(1);
        }
        line
    };

    let mut p = Parser::new(src.as_bytes());
    let result = p.expr();
    let ok = match result {
        Some(v) => {
            p.skip();
            if p.peek() != 0 {
                p.err = Some("syntax");
                None
            } else {
                Some(v)
            }
        }
        None => None,
    };

    match ok {
        Some(v) => {
            cprint!("CALC", src, "=", v);
            cprint!("CALC ops", p.ops);
        }
        None => {
            cprint!("CALC ERROR", p.err.unwrap_or("syntax"));
            std::process::exit(1);
        }
    }
}
