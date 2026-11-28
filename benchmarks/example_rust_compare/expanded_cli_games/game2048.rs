// Safe-Rust model of examples/expanded/games/cores/game2048.cp
// No unsafe, no FFI. Fixed-seed xorshift64* 2048 logic core with CLI driver.
// Build: rustc -O --edition 2021 game2048.rs -o game2048

use std::io::{self, Read};
use std::num::Wrapping;

const DIR_UP: i32 = 0;
const DIR_DOWN: i32 = 1;
const DIR_LEFT: i32 = 2;
const DIR_RIGHT: i32 = 3;
const DEFAULT_MOVES: &str = "RDLURDLUDD";
const SEED: u64 = 0x9E37_79B9_7F4A_7C15;

macro_rules! cprint {
    () => { println!() };
    ($($arg:expr),+ $(,)?) => {
        println!("{}", [$($arg.to_string()),+].join(" "))
    };
}

struct Rng { state: Wrapping<u64> }

impl Rng {
    fn new() -> Self { Self { state: Wrapping(SEED) } }
    fn next(&mut self) -> u64 {
        let mut x = self.state;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        self.state = x;
        (x * Wrapping(2_685_821_657_736_338_717u64)).0
    }
    fn below(&mut self, n: usize) -> usize {
        let r = self.next();
        let hi = r >> 32;
        ((hi * (n as u64)) >> 32) as usize
    }
}

fn dir_code(ch: char) -> Option<i32> {
    match ch {
        'U' | 'u' => Some(DIR_UP),
        'D' | 'd' => Some(DIR_DOWN),
        'L' | 'l' => Some(DIR_LEFT),
        'R' | 'r' => Some(DIR_RIGHT),
        _ => None,
    }
}

fn dir_name(code: i32) -> &'static str {
    match code {
        DIR_UP => "U",
        DIR_DOWN => "D",
        DIR_LEFT => "L",
        _ => "R",
    }
}

fn parse_moves(text: &str, out: &mut Vec<i32>) -> bool {
    for ch in text.chars() {
        if ch == '\n' || ch == '\r' || ch == ' ' || ch == '\t' { continue; }
        match dir_code(ch) {
            Some(c) => out.push(c),
            None => return false,
        }
    }
    true
}

struct Board {
    cells: [i32; 16],
    score: i32,
    spawns: i32,
    rng: Rng,
}

impl Board {
    fn new(rng: Rng) -> Self {
        Self { cells: [0; 16], score: 0, spawns: 0, rng }
    }

    fn spawn(&mut self) -> Option<(usize, i32)> {
        let empty: Vec<usize> = (0..16).filter(|&i| self.cells[i] == 0).collect();
        if empty.is_empty() { return None; }
        let pick = self.rng.below(empty.len());
        let value = if self.rng.below(10) == 0 { 4 } else { 2 };
        let idx = empty[pick];
        self.cells[idx] = value;
        self.spawns += 1;
        Some((idx, value))
    }

    fn traverse_index(dir: i32, line: usize, j: usize) -> usize {
        match dir {
            DIR_LEFT => line * 4 + j,
            DIR_RIGHT => line * 4 + (3 - j),
            DIR_UP => j * 4 + line,
            _ => (3 - j) * 4 + line,
        }
    }

    fn move_dir(&mut self, dir: i32) -> (i32, i32, Option<(usize, i32)>) {
        let before = self.cells;
        let mut gain = 0;
        for line in 0..4 {
            let mut vals = [0i32; 4];
            for j in 0..4 {
                vals[j] = self.cells[Self::traverse_index(dir, line, j)];
            }
            let mut packed = [0i32; 4];
            let mut k = 0;
            for j in 0..4 {
                if vals[j] != 0 { packed[k] = vals[j]; k += 1; }
            }
            let mut merged = [0i32; 4];
            let mut m = 0;
            let mut j = 0;
            while j < k {
                if j + 1 < k && packed[j] == packed[j + 1] {
                    merged[m] = packed[j] + packed[j + 1];
                    gain += merged[m];
                    j += 2;
                } else {
                    merged[m] = packed[j];
                    j += 1;
                }
                m += 1;
            }
            for j in 0..4 {
                self.cells[Self::traverse_index(dir, line, j)] = merged[j];
            }
        }
        let changed = if before != self.cells { 1 } else { 0 };
        self.score += gain;
        let spawned = if changed == 1 { self.spawn() } else { None };
        (changed, gain, spawned)
    }

    fn render(&self) {
        for r in 0..4 {
            print!("  ");
            for c in 0..4 {
                if c != 0 { print!(" "); }
                print!("{:4}", self.cells[r * 4 + c]);
            }
            println!();
        }
    }
}

fn fail(message: &str) {
    eprintln!("game2048: {}", message);
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let mut moves: Vec<i32> = Vec::new();

    if args.len() > 1 {
        if args[1] == "-" {
            if args.len() != 2 {
                fail("' - ' must be the only argument when reading stdin");
                std::process::exit(2);
            }
            let mut buf = String::new();
            if io::stdin().read_to_string(&mut buf).is_err()
                || !parse_moves(&buf, &mut moves)
            {
                fail("invalid direction in stdin input");
                std::process::exit(2);
            }
        } else {
            for a in args.iter().skip(1) {
                if !parse_moves(a, &mut moves) {
                    fail("invalid direction token (expected U/D/L/R)");
                    std::process::exit(2);
                }
            }
        }
    } else if !parse_moves(DEFAULT_MOVES, &mut moves) {
        fail("internal default sequence is invalid");
        std::process::exit(2);
    }
    if moves.is_empty() {
        fail("empty move sequence");
        std::process::exit(2);
    }

    let mut board = Board::new(Rng::new());
    board.spawn();
    board.spawn();

    cprint!("GAME2048 seed=0x9E3779B97F4A7C15 moves=", moves.len());
    cprint!("START score=0");
    board.render();

    let mut total_changed = 0;
    let mut noop = 0;
    for (i, &dir) in moves.iter().enumerate() {
        let (changed, gained, spawned) = board.move_dir(dir);
        if changed == 1 {
            total_changed += 1;
            match spawned {
                Some((idx, value)) => println!(
                    "MOVE {} {} changed=1 gained={} score={} spawn=r{}c{}:{}",
                    i + 1, dir_name(dir), gained, board.score, idx / 4, idx % 4, value
                ),
                None => println!(
                    "MOVE {} {} changed=1 gained={} score={} spawn=none",
                    i + 1, dir_name(dir), gained, board.score
                ),
            }
        } else {
            noop += 1;
            println!(
                "MOVE {} {} changed=0 gained=0 score={} spawn=none",
                i + 1, dir_name(dir), board.score
            );
        }
        board.render();
    }

    cprint!("FINAL score=", board.score, "moves=", moves.len(),
            "effective=", total_changed, "noops=", noop, "spawns=", board.spawns);
    cprint!("FINAL_BOARD");
    board.render();
}
