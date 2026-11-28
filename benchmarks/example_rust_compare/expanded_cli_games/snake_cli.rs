// Safe-Rust model of examples/expanded/games/snake/snake_cli.cp
// No unsafe, no FFI. Deterministic CLI driver for the Snake logic core.
// Build: rustc -O --edition 2021 snake_cli.rs -o snake_cli

const W: usize = 20;
const H: usize = 20;
const CELLS: i32 = (W * H) as i32;
const INITIAL_LEN: usize = 5;
const MAX_DIRS: usize = 256;
const MAX_STEPS: usize = 256;
const SEED: u32 = 12345;

const UP: i32 = 0;
const DOWN: i32 = 1;
const LEFT: i32 = 2;
const RIGHT: i32 = 3;

const REASON_NONE: i32 = 0;
const REASON_WALL: i32 = 1;
const REASON_SELF: i32 = 2;
const REASON_FULL: i32 = 3;

fn pack(x: i32, y: i32) -> i32 { y * W as i32 + x }

#[derive(Clone)]
struct SnakeCore {
    body: Vec<i32>,
    dir: i32,
    food: i32,
    score: i32,
    game_over: i32,
    reason: i32,
    steps: i32,
    rng: u32,
    err_failed: bool,
}

impl SnakeCore {
    fn new(seed: u32) -> Self {
        let mut c = Self {
            body: Vec::new(), dir: RIGHT, food: -1, score: 0,
            game_over: 0, reason: REASON_NONE, steps: 0,
            rng: seed, err_failed: false,
        };
        for i in 0..INITIAL_LEN {
            c.body.push(pack(10 - i as i32, 10));
        }
        c.place_food();
        c
    }

    fn next_rand(&mut self) -> u32 {
        self.rng = self.rng.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
        self.rng
    }

    fn occupied(&self, cell: i32) -> bool {
        self.body.iter().any(|&v| v == cell)
    }

    fn place_food(&mut self) {
        let start = (self.next_rand() % CELLS as u32) as i32;
        for k in 0..CELLS {
            let cell = (start + k) % CELLS;
            if !self.occupied(cell) {
                self.food = cell;
                return;
            }
        }
        self.food = -1;
        self.game_over = 1;
        self.reason = REASON_FULL;
        self.err_failed = true;
    }

    fn set_food(&mut self, x: i32, y: i32) {
        if x < 0 || x >= W as i32 || y < 0 || y >= H as i32 {
            self.err_failed = true;
            return;
        }
        if self.occupied(pack(x, y)) {
            self.err_failed = true;
            return;
        }
        self.food = pack(x, y);
    }

    fn step(&mut self, dir: i32) -> i32 {
        if self.game_over != 0 { self.err_failed = true; return -1; }
        if dir < UP || dir > RIGHT { self.err_failed = true; return -1; }
        if (dir == LEFT && self.dir == RIGHT) || (dir == RIGHT && self.dir == LEFT)
            || (dir == UP && self.dir == DOWN) || (dir == DOWN && self.dir == UP)
        {
            self.err_failed = true;
            return -1;
        }
        let head = match self.body.first() { Some(&h) => h, None => { self.err_failed = true; return -1; } };
        self.dir = dir;

        let mut nx = head % W as i32;
        let mut ny = head / W as i32;
        if dir == UP { ny -= 1; }
        else if dir == DOWN { ny += 1; }
        else if dir == LEFT { nx -= 1; }
        else { nx += 1; }

        if nx < 0 || nx >= W as i32 || ny < 0 || ny >= H as i32 {
            self.game_over = 1;
            self.reason = REASON_WALL;
            self.err_failed = true;
            return -1;
        }

        let new_head = pack(nx, ny);
        let growing = new_head == self.food;
        let mut limit = self.body.len();
        if !growing && limit > 0 { limit -= 1; }
        for &segment in self.body.iter().take(limit) {
            if segment == new_head {
                self.game_over = 1;
                self.reason = REASON_SELF;
                self.err_failed = true;
                return -1;
            }
        }

        self.body.insert(0, new_head);
        if growing {
            self.score += 1;
            self.place_food();
        } else {
            self.body.pop();
        }
        self.steps += 1;
        0
    }

    fn render(&self) -> String {
        let head = self.body.first().copied().unwrap_or(-1);
        let mut out = String::with_capacity(H * (W + 1));
        for y in 0..H {
            for x in 0..W {
                let cell = pack(x as i32, y as i32);
                let mut ch = '.';
                if cell == self.food { ch = 'F'; }
                for &segment in self.body.iter().skip(1) {
                    if segment == cell { ch = 'o'; break; }
                }
                if head == cell { ch = 'O'; }
                out.push(ch);
            }
            out.push('\n');
        }
        out
    }
}

fn reason_name(reason: i32) -> &'static str {
    match reason {
        REASON_WALL => "wall",
        REASON_SELF => "self",
        REASON_FULL => "full",
        _ => "none",
    }
}

fn dir_name(dir: i32) -> &'static str {
    match dir {
        UP => "U",
        DOWN => "D",
        LEFT => "L",
        _ => "R",
    }
}

fn parse_dir(ch: char) -> Option<i32> {
    match ch {
        'U' | 'u' | 'W' | 'w' => Some(UP),
        'D' | 'd' | 'S' | 's' => Some(DOWN),
        'L' | 'l' | 'A' | 'a' => Some(LEFT),
        'R' | 'r' => Some(RIGHT),
        _ => None,
    }
}

use std::io::{self, Read};

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let mut dirs: Vec<i32> = Vec::new();
    let (scenario_name, food_x, food_y, expected_score, expected_over, expected_reason):
        (&str, i32, i32, i32, i32, i32) = if args.len() > 1 {
        match args[1].as_bytes().first().copied().unwrap_or(0) as char {
            'g' | 'G' => ("growth", 11, 10, 1, 0, REASON_NONE),
            'w' | 'W' => ("wall", 0, 0, 0, 1, REASON_WALL),
            's' | 'S' => ("self", 0, 0, 0, 1, REASON_SELF),
            _ => {
                eprintln!("usage: snake_cli <growth|wall|self> [directions]");
                std::process::exit(2);
            }
        }
    } else {
        ("growth", 11, 10, 1, 0, REASON_NONE)
    };

    if args.len() > 1 {
        for token in args.iter().skip(2) {
            for ch in token.chars() {
                if dirs.len() >= MAX_DIRS { break; }
                match parse_dir(ch) {
                    Some(d) => dirs.push(d),
                    None => eprintln!("ignoring unknown direction '{}'", ch),
                }
            }
        }
    } else {
        let mut buf = String::new();
        let _ = io::stdin().read_to_string(&mut buf);
        for ch in buf.chars() {
            if dirs.len() >= MAX_DIRS { break; }
            if let Some(d) = parse_dir(ch) { dirs.push(d); }
        }
    }

    let mut core = SnakeCore::new(SEED);
    core.set_food(food_x, food_y);

    println!("INIT scenario {} seed {} len {}", scenario_name, SEED, core.body.len());
    print!("{}", core.render());

    let steps = std::cmp::min(dirs.len(), MAX_STEPS);
    for step in 0..steps {
        let moved = core.step(dirs[step]);
        println!("STEP {} DIR {} MOVED {} SCORE {} OVER {} REASON {}",
                 step + 1, dir_name(dirs[step]), if moved == 0 { 1 } else { 0 },
                 core.score, core.game_over, reason_name(core.reason));
        print!("{}", core.render());
        if core.game_over != 0 { break; }
    }

    println!("FINAL score {}", core.score);
    let ok = if core.score == expected_score
        && core.game_over == expected_over
        && core.reason == expected_reason { 1 } else { 0 };
    println!("SCENARIO {} score {} over {} reason {} ok {}",
             scenario_name, core.score, core.game_over, reason_name(core.reason), ok);
    std::process::exit(if ok == 1 { 0 } else { 1 });
}
