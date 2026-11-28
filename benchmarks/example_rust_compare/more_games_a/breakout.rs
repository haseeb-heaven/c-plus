// Safe-Rust model of examples/expanded/games_2/breakout.cp
//
// Models ONLY the deterministic observable behavior (stdout + exit status) for
// fixed argv inputs. No unsafe, no FFI, no wall clock, no external randomness.
// The xorshift32 state is seeded with BREAKOUT_SEED before construction, as in
// the C+ source; the single seed bit drives the initial and reset ball velocity.

use std::env;

const BREAKOUT_SEED: u32 = 0x5EED1234;
const W: i32 = 16;
const BRICK_ROWS: i32 = 4;
const H: i32 = 12;
const PADDLE_W: i32 = 3;
const START_LIVES: i32 = 3;
const MAX_TOKEN_LEN: usize = 64;
const MAX_TICKS: usize = 128;
const DEFAULT_CMDS: &str = "RLSRRLSSLLRRRLSRS";

struct Rng {
    state: u32,
}

impl Rng {
    fn new(seed: u32) -> Self {
        Rng { state: seed }
    }
    fn next(&mut self) -> u32 {
        let mut x = self.state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        self.state = x;
        x
    }
}

fn sgn(v: i32) -> String {
    if v >= 0 {
        format!("+{}", v)
    } else {
        format!("{}", v)
    }
}

fn cmd_code(ch: char) -> i32 {
    match ch {
        'L' | 'l' => 1,
        'R' | 'r' => 2,
        'S' | 's' | '.' => 0,
        _ => -1,
    }
}

fn cmd_name(code: i32) -> &'static str {
    match code {
        1 => "L",
        2 => "R",
        _ => "S",
    }
}

fn parse_cmds(text: &str, out: &mut Vec<i32>) -> Result<(), bool> {
    if text.chars().count() > MAX_TOKEN_LEN {
        return Err(true);
    }
    for ch in text.chars() {
        if ch == '\n' || ch == '\r' || ch == ' ' || ch == '\t' {
            continue;
        }
        let code = cmd_code(ch);
        if code < 0 {
            return Err(false);
        }
        out.push(code);
    }
    Ok(())
}

struct Breakout {
    bricks: Vec<Vec<i32>>,
    bx: i32,
    by: i32,
    vx: i32,
    vy: i32,
    paddle: i32,
    lives: i32,
    score: i32,
    remaining: i32,
    paddle_hits: i32,
    wall_hits: i32,
    brick_hits: i32,
    ticks: i32,
    won: bool,
    over: bool,
    rng: Rng,
}

impl Breakout {
    fn new(mut rng: Rng) -> Self {
        let seed_bit = rng.next() & 1;
        let bricks = vec![vec![1; W as usize]; BRICK_ROWS as usize];
        Breakout {
            bricks,
            bx: W / 2,
            by: H - 3,
            vx: if seed_bit != 0 { 1 } else { -1 },
            vy: -1,
            paddle: 0,
            lives: START_LIVES,
            score: 0,
            remaining: BRICK_ROWS * W,
            paddle_hits: 0,
            wall_hits: 0,
            brick_hits: 0,
            ticks: 0,
            won: false,
            over: false,
            rng,
        }
    }

    fn reset_ball(&mut self) {
        let seed_bit = self.rng.next() & 1;
        self.bx = W / 2;
        self.by = H - 3;
        self.vx = if seed_bit != 0 { 1 } else { -1 };
        self.vy = -1;
    }

    fn step(&mut self, cmd: i32) -> &'static str {
        self.ticks += 1;
        if cmd == 1 {
            if self.paddle > 0 {
                self.paddle -= 1;
            }
        } else if cmd == 2 {
            if self.paddle < W - PADDLE_W {
                self.paddle += 1;
            }
        }

        self.bx += self.vx;
        self.by += self.vy;
        let mut event = "none";

        if self.bx < 0 {
            self.bx = 0;
            self.vx = 1;
            self.wall_hits += 1;
            event = "wall";
        } else if self.bx >= W {
            self.bx = W - 1;
            self.vx = -1;
            self.wall_hits += 1;
            event = "wall";
        }

        if self.by < 0 {
            self.by = 0;
            self.vy = 1;
            self.wall_hits += 1;
            event = "top";
        }

        if self.by < BRICK_ROWS && self.by >= 0 && self.bx >= 0 && self.bx < W
            && self.bricks[self.by as usize][self.bx as usize] != 0
        {
            self.bricks[self.by as usize][self.bx as usize] = 0;
            self.remaining -= 1;
            self.score += 10;
            self.brick_hits += 1;
            self.vy = -self.vy;
            event = "brick";
            if self.remaining == 0 {
                self.won = true;
            }
        }

        if self.by >= H - 1 {
            if self.bx >= self.paddle && self.bx < self.paddle + PADDLE_W {
                self.by = H - 2;
                self.vy = -1;
                self.paddle_hits += 1;
                event = "paddle";
            } else {
                self.lives -= 1;
                event = "lose";
                if self.lives <= 0 {
                    self.over = true;
                } else {
                    self.reset_ball();
                }
            }
        }
        event
    }

    fn render(&self) -> String {
        let mut out = String::new();
        for r in 0..H {
            out.push_str("  ");
            for c in 0..W {
                let mut ch = b'.';
                if r < BRICK_ROWS && self.bricks[r as usize][c as usize] != 0 {
                    ch = b'#';
                }
                if r == self.by && c == self.bx {
                    ch = b'o';
                }
                if r == H - 1 && c >= self.paddle && c < self.paddle + PADDLE_W {
                    ch = b'=';
                }
                out.push(ch as char);
            }
            out.push('\n');
        }
        out
    }
}

fn refuse(msg: &str) -> ! {
    eprintln!("breakout: refused: {}", msg);
    std::process::exit(2);
}

fn main() {
    let mut cmds: Vec<i32> = Vec::new();
    let mut game = Breakout::new(Rng::new(BREAKOUT_SEED));

    let args: Vec<String> = env::args().skip(1).collect();

    if !args.is_empty() {
        if args[0] == "-" {
            if args.len() != 2 {
                refuse("'-' must be the only argument when reading stdin");
            }
            use std::io::Read;
            let mut raw = Vec::new();
            let n = std::io::stdin().read_to_end(&mut raw).unwrap_or(0);
            if n == 0 {
                refuse("invalid command in stdin (expected L/R/S)");
            }
            let first_line: Vec<u8> = raw
                .split(|&b| b == b'\n')
                .next()
                .unwrap_or(&[])
                .to_vec();
            if first_line.len() + 1 >= MAX_TOKEN_LEN + 1 {
                refuse("stdin line is oversized");
            }
            let text = String::from_utf8_lossy(&first_line).into_owned();
            match parse_cmds(&text, &mut cmds) {
                Err(true) => refuse("stdin line is oversized"),
                Err(false) => refuse("invalid command in stdin (expected L/R/S)"),
                Ok(()) => {}
            }
        } else {
            for arg in &args {
                match parse_cmds(arg, &mut cmds) {
                    Err(true) => refuse("token is oversized (max 64 characters)"),
                    Err(false) => refuse("invalid command token (expected L/R/S)"),
                    Ok(()) => {}
                }
            }
        }
    } else if parse_cmds(DEFAULT_CMDS, &mut cmds).is_err() {
        refuse("internal default command sequence is invalid");
    }

    if cmds.is_empty() {
        refuse("empty command sequence");
    }
    if cmds.len() > MAX_TICKS {
        refuse("too many commands (max 128 ticks)");
    }

    let mut out = String::new();
    out.push_str(&format!(
        "BREAKOUT seed=0x{:08X} grid={}x{} bricks={} paddle_w={} lives={} cmds={}\n",
        BREAKOUT_SEED,
        W,
        H,
        BRICK_ROWS * W,
        PADDLE_W,
        START_LIVES,
        cmds.len()
    ));
    out.push_str(&format!(
        "INIT ball=({},{}) v=({},{}) paddle=[{},{}] score=0 bricks={}\n",
        game.bx,
        game.by,
        sgn(game.vx),
        sgn(game.vy),
        game.paddle,
        game.paddle + PADDLE_W - 1,
        game.remaining
    ));
    out.push_str(&game.render());

    for (i, &code) in cmds.iter().enumerate() {
        let event = game.step(code);
        out.push_str(&format!(
            "TICK {} cmd={} ball=({},{}) v=({},{}) paddle=[{},{}] event={} score={} bricks={} lives={}\n",
            i + 1,
            cmd_name(code),
            game.bx,
            game.by,
            sgn(game.vx),
            sgn(game.vy),
            game.paddle,
            game.paddle + PADDLE_W - 1,
            event,
            game.score,
            game.remaining,
            game.lives
        ));
        out.push_str(&game.render());
        if game.won || game.over {
            break;
        }
    }

    let outcome = if game.won {
        "win"
    } else if game.over {
        "game-over"
    } else {
        "incomplete"
    };
    out.push_str(&format!(
        "FINAL outcome={} ticks={} score={} bricks={} paddle_hits={} wall_hits={} brick_hits={} lives={}\n",
        outcome,
        game.ticks,
        game.score,
        game.remaining,
        game.paddle_hits,
        game.wall_hits,
        game.brick_hits,
        game.lives
    ));
    print!("{}", out);
}
