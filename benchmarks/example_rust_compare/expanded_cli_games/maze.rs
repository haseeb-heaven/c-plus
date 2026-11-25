// Safe-Rust model of examples/expanded/games/cores/maze.cp
// No unsafe, no FFI. Fixed-layout maze logic core with CLI driver.
// Build: rustc -O --edition 2021 maze.rs -o maze

use std::io::{self, Read};

const W: usize = 9;
const H: usize = 7;
const DEFAULT_MOVES: &str = "UDDDDRRRRR";
const LAYOUT: [&str; H] = [
    "#########",
    "#S..#...#",
    "#.#.#.#.#",
    "#.#...#.#",
    "#.#####.#",
    "#.....G.#",
    "#########",
];

const DIR_UP: i32 = 0;
const DIR_DOWN: i32 = 1;
const DIR_LEFT: i32 = 2;
const DIR_RIGHT: i32 = 3;

const CELL_FLOOR: i32 = 0;
const CELL_WALL: i32 = 1;
const CELL_START: i32 = 2;
const CELL_GOAL: i32 = 3;

macro_rules! cprint {
    () => { println!() };
    ($($arg:expr),+ $(,)?) => {
        println!("{}", [$($arg.to_string()),+].join(" "))
    };
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

struct Maze {
    cells: Vec<Vec<i32>>,
    pr: i32,
    pc: i32,
    sr: i32,
    sc: i32,
    gr: i32,
    gc: i32,
    steps: i32,
    blocked: i32,
    goal: i32,
}

impl Maze {
    fn new() -> Self {
        let mut cells = vec![vec![CELL_FLOOR; W]; H];
        let (mut sr, mut sc, mut gr, mut gc) = (0i32, 0i32, 0i32, 0i32);
        for (r, row) in LAYOUT.iter().enumerate() {
            for (c, ch) in row.chars().enumerate() {
                let kind = match ch {
                    '#' => CELL_WALL,
                    'S' => CELL_START,
                    'G' => CELL_GOAL,
                    _ => CELL_FLOOR,
                };
                cells[r][c] = kind;
                if kind == CELL_START { sr = r as i32; sc = c as i32; }
                if kind == CELL_GOAL { gr = r as i32; gc = c as i32; }
            }
        }
        Self {
            cells, pr: sr, pc: sc, sr, sc, gr, gc,
            steps: 0, blocked: 0, goal: 0,
        }
    }

    fn kind_at(&self, r: i32, c: i32) -> i32 {
        if r < 0 || r >= H as i32 || c < 0 || c >= W as i32 {
            return CELL_WALL;
        }
        self.cells[r as usize][c as usize]
    }

    // status: 0 blocked, 1 moved, 2 goal reached
    fn step(&mut self, dir: i32) -> i32 {
        let mut nr = self.pr;
        let mut nc = self.pc;
        if dir == DIR_UP { nr -= 1; }
        else if dir == DIR_DOWN { nr += 1; }
        else if dir == DIR_LEFT { nc -= 1; }
        else { nc += 1; }
        let kind = self.kind_at(nr, nc);
        if kind == CELL_WALL {
            self.blocked += 1;
            return 0;
        }
        self.pr = nr;
        self.pc = nc;
        self.steps += 1;
        if kind == CELL_GOAL { self.goal = 1; 2 } else { 1 }
    }

    fn render(&self) {
        for r in 0..H {
            print!("  ");
            for c in 0..W {
                let ch = if r as i32 == self.pr && c as i32 == self.pc {
                    '@'
                } else {
                    match self.cells[r][c] {
                        CELL_WALL => '#',
                        CELL_START => 'S',
                        CELL_GOAL => 'G',
                        _ => '.',
                    }
                };
                print!("{}", ch);
            }
            println!();
        }
    }
}

fn fail(message: &str) {
    eprintln!("maze: {}", message);
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let mut moves: Vec<i32> = Vec::new();

    if args.len() > 1 {
        if args[1] == "-" {
            if args.len() != 2 { fail("' - ' must be the only argument when reading stdin"); std::process::exit(2); }
            let mut buf = String::new();
            if io::stdin().read_to_string(&mut buf).is_err() || !parse_moves(&buf, &mut moves) {
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

    let mut maze = Maze::new();
    println!("MAZE {}x{} start=({},{}) goal=({},{})",
             W, H, maze.sr, maze.sc, maze.gr, maze.gc);
    cprint!("INITIAL");
    maze.render();

    let mut stopped_early = 0;
    let mut stop_step = 0;
    for (i, &dir) in moves.iter().enumerate() {
        let status = maze.step(dir);
        if status == 0 {
            println!("STEP {} {} blocked pos=({},{})", i + 1, dir_name(dir), maze.pr, maze.pc);
        } else if status == 1 {
            println!("STEP {} {} moved pos=({},{})", i + 1, dir_name(dir), maze.pr, maze.pc);
        } else {
            println!("STEP {} {} GOAL pos=({},{})", i + 1, dir_name(dir), maze.pr, maze.pc);
        }
        maze.render();
        if status == 2 {
            stopped_early = 1;
            stop_step = i + 1;
            break;
        }
    }

    if maze.goal == 1 {
        println!("GOAL step={} pos=({},{})", stop_step, maze.pr, maze.pc);
    } else {
        cprint!("NO_GOAL");
    }
    println!("FINAL steps={} blocked={} goal={} pos=({},{}) moves={}",
             maze.steps, maze.blocked, maze.goal, maze.pr, maze.pc, moves.len());
    if stopped_early == 1 {
        println!("STOP reason=goal at step={}", stop_step);
    }
}
