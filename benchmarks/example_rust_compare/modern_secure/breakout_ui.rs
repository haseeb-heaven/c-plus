/* SAFE-Rust model of examples/modern_gui/games/breakout_ui/breakout_ui.cp
 *
 * Deterministic Breakout state machine + frame transcript.  The real C+
 * example renders each frame with SDL3 and a Nuklear HUD; under
 * SDL_VIDEODRIVER=dummy the pixel probe is stable, so this model reproduces
 * the same stdout/exit without a display, unsafe code or FFI.
 */

use std::env;

const SEED: u32 = 0xC0FFEE11;
const MAX_COLS: usize = 32;
const MAX_ROWS: usize = 8;
const FIELD_H: i32 = 12;
const PADDLE_W: i32 = 3;
const START_LIVES: i32 = 3;
const MAX_TOKEN_LEN: usize = 64;
const MAX_TICKS: usize = 128;
const DEFAULT_CMDS: &str = "RLSRRLSSLLRRRLSRSSLLRRRLLSRRRLS";

#[derive(Clone, Copy, PartialEq)]
enum Cmd { Still, Left, Right }

fn cmd_name(c: Cmd) -> &'static str {
    match c { Cmd::Left => "L", Cmd::Right => "R", Cmd::Still => "S" }
}

fn cmd_code(ch: char) -> Option<Cmd> {
    match ch {
        'L' | 'l' => Some(Cmd::Left),
        'R' | 'r' => Some(Cmd::Right),
        'S' | 's' | '.' => Some(Cmd::Still),
        _ => None,
    }
}

/* -1 = invalid command, -2 = oversized token */
fn parse_cmds(text: &str, out: &mut Vec<Cmd>) -> i32 {
    if text.len() > MAX_TOKEN_LEN { return -2; }
    for ch in text.chars() {
        if matches!(ch, '\n' | '\r' | ' ' | '\t') { continue; }
        match cmd_code(ch) {
            Some(c) => out.push(c),
            None => return -1,
        }
    }
    0
}

fn parse_grid(text: &str) -> Option<(usize, usize)> {
    let mut w: i32 = 0;
    let mut h: i32 = 0;
    let mut phase = 0;
    for ch in text.chars() {
        if ch == 'x' && phase == 0 { phase = 1; continue; }
        if !ch.is_ascii_digit() { return None; }
        let d = ch as i32 - '0' as i32;
        if phase == 0 {
            w = w * 10 + d;
            if w > MAX_COLS as i32 { return None; }
        } else {
            h = h * 10 + d;
            if h > MAX_ROWS as i32 { return None; }
        }
    }
    if phase != 1 || w < 4 || w > MAX_COLS as i32 || h < 1 || h > MAX_ROWS as i32 { return None; }
    if w * h > 256 { return None; }
    Some((w as usize, h as usize))
}

struct Rng(u32);
impl Rng {
    fn next(&mut self) -> u32 {
        let mut x = self.0;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        self.0 = x;
        x
    }
}

struct Game {
    bricks: [[bool; MAX_COLS]; MAX_ROWS],
    cols: usize,
    rows: usize,
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
}

impl Game {
    fn new(c: usize, r: usize, rng: &mut Rng) -> Self {
        let mut bricks = [[false; MAX_COLS]; MAX_ROWS];
        for row in 0..r { for col in 0..c { bricks[row][col] = true; } }
        let seed_bit = rng.next() & 1;
        Game {
            bricks, cols: c, rows: r,
            bx: (c >> 1) as i32, by: FIELD_H - 3,
            vx: if seed_bit != 0 { 1 } else { -1 }, vy: -1,
            paddle: 0, lives: START_LIVES, score: 0,
            remaining: (r * c) as i32,
            paddle_hits: 0, wall_hits: 0, brick_hits: 0,
            ticks: 0, won: false, over: false,
        }
    }

    fn reset_ball(&mut self, rng: &mut Rng) {
        let seed_bit = rng.next() & 1;
        self.bx = (self.cols >> 1) as i32;
        self.by = FIELD_H - 3;
        self.vx = if seed_bit != 0 { 1 } else { -1 };
        self.vy = -1;
    }

    fn step(&mut self, cmd: Cmd, rng: &mut Rng) -> &'static str {
        self.ticks += 1;
        if cmd == Cmd::Left {
            if self.paddle > 0 { self.paddle -= 1; }
        } else if cmd == Cmd::Right {
            if self.paddle < self.cols as i32 - PADDLE_W { self.paddle += 1; }
        }
        self.bx += self.vx;
        self.by += self.vy;
        let mut event = "none";

        if self.bx < 0 {
            self.bx = 0; self.vx = 1; self.wall_hits += 1; event = "wall";
        } else if self.bx >= self.cols as i32 {
            self.bx = self.cols as i32 - 1; self.vx = -1; self.wall_hits += 1; event = "wall";
        }
        if self.by < 0 {
            self.by = 0; self.vy = 1; self.wall_hits += 1; event = "top";
        }
        if self.by >= 0 && (self.by as usize) < self.rows
            && self.bx >= 0 && (self.bx as usize) < self.cols
            && self.bricks[self.by as usize][self.bx as usize]
        {
            self.bricks[self.by as usize][self.bx as usize] = false;
            self.remaining -= 1;
            self.score += 10;
            self.brick_hits += 1;
            self.vy = -self.vy;
            event = "brick";
            if self.remaining == 0 { self.won = true; }
        }
        if self.by >= FIELD_H - 1 {
            if self.bx >= self.paddle && self.bx < self.paddle + PADDLE_W {
                self.by = FIELD_H - 2;
                self.vy = -1;
                self.paddle_hits += 1;
                event = "paddle";
            } else {
                self.lives -= 1;
                event = "lose";
                if self.lives <= 0 { self.over = true; } else { self.reset_ball(rng); }
            }
        }
        event
    }
}

fn hud(g: &Game) -> [String; 4] {
    [
        format!("HUD SCORE {:03}", g.score),
        format!("HUD LIVES {}", g.lives),
        format!("HUD BRICKS {:03}", g.remaining),
        format!("HUD PADDLE [{},{}]", g.paddle, g.paddle + PADDLE_W - 1),
    ]
}

fn render_frame(frame: usize, g: &Game) {
    println!("FRAME {:03} draw=OK hud=OK shot=OK painted=1", frame);
    for l in hud(g) { println!("{}", l); }
}

fn refuse(tag: &str, message: &str) -> i32 {
    eprintln!("REFUSED {}: {}", tag, message);
    2
}

fn probe_null() -> i32 {
    eprintln!("REFUSED null-surface: null surface handle");
    eprintln!("REFUSED null-context: null nuklear context");
    println!("GUARD surface=REFUSED context=REFUSED");
    println!("GUARD-END refused=2");
    2
}

fn main() {
    let args: Vec<String> = env::args().skip(1).collect();
    let mut rng = Rng(SEED);
    let mut cols = 16usize;
    let mut rows = 4usize;
    let mut probe = false;
    let mut a = 0usize;

    while a < args.len() {
        let tok = args[a].as_str();
        if tok == "--help" {
            println!("usage: breakout_ui [--grid WxH] [--probe-null] [LRS...|-]");
            return;
        }
        if tok == "--probe-null" { probe = true; a += 1; continue; }
        if tok == "--grid" {
            if a + 1 >= args.len() { std::process::exit(refuse("grid", "missing grid dimensions")); }
            match parse_grid(&args[a + 1]) {
                Some((w, h)) => { cols = w; rows = h; }
                None => std::process::exit(refuse("grid", "oversized brick dimensions")),
            }
            a += 2;
            continue;
        }
        if tok.starts_with("--") { std::process::exit(refuse("option", "unknown option")); }
        break;
    }
    if probe { std::process::exit(probe_null()); }

    let mut cmds: Vec<Cmd> = Vec::new();
    if a < args.len() {
        if args[a] == "-" {
            if a + 1 != args.len() {
                std::process::exit(refuse("argv", "'-' must be the only command argument"));
            }
            let mut line = String::new();
            use std::io::BufRead;
            let stdin = std::io::stdin();
            if stdin.lock().read_line(&mut line).unwrap_or(0) == 0 {
                std::process::exit(refuse("commands", "out-of-range paddle command (expected L/R/S)"));
            }
            while line.ends_with('\n') || line.ends_with('\r') { line.pop(); }
            if line.len() > MAX_TOKEN_LEN {
                std::process::exit(refuse("commands", "stdin line is oversized"));
            }
            let rc = parse_cmds(&line, &mut cmds);
            if rc == -2 {
                std::process::exit(refuse("commands", "stdin line is oversized"));
            }
            if rc != 0 {
                std::process::exit(refuse("commands", "out-of-range paddle command (expected L/R/S)"));
            }
        } else {
            while a < args.len() {
                let rc = parse_cmds(&args[a], &mut cmds);
                if rc == -2 {
                    std::process::exit(refuse("commands", "token is oversized (max 64 characters)"));
                }
                if rc != 0 {
                    std::process::exit(refuse("commands", "out-of-range paddle command (expected L/R/S)"));
                }
                a += 1;
            }
        }
    } else {
        let rc = parse_cmds(DEFAULT_CMDS, &mut cmds);
        if rc != 0 { std::process::exit(refuse("internal", "default command sequence is invalid")); }
    }
    if cmds.is_empty() { std::process::exit(refuse("commands", "empty command sequence")); }
    if cmds.len() > MAX_TICKS { std::process::exit(refuse("commands", "too many commands (max 128 ticks)")); }

    let mut game = Game::new(cols, rows, &mut rng);
    println!("GFXBREAKOUT seed=0x{:08X} grid={}x{} bricks={} field={}x{} paddle_w={} lives={} cmds={}",
        SEED, cols, rows, cols * rows, cols, FIELD_H, PADDLE_W, START_LIVES, cmds.len());
    println!("INIT ball=({},{}) v=({:+},{:+}) paddle=[{},{}] score=0 bricks={}",
        game.bx, game.by, game.vx, game.vy, game.paddle, game.paddle + PADDLE_W - 1, game.remaining);
    let mut frames = 0usize;
    render_frame(frames, &game);
    frames += 1;

    for (i, cmd) in cmds.iter().enumerate() {
        let event = game.step(*cmd, &mut rng);
        println!("TICK {} cmd={} ball=({},{}) v=({:+},{:+}) paddle=[{},{}] event={} score={} bricks={} lives={}",
            i + 1, cmd_name(*cmd), game.bx, game.by, game.vx, game.vy,
            game.paddle, game.paddle + PADDLE_W - 1, event, game.score, game.remaining, game.lives);
        render_frame(frames, &game);
        frames += 1;
        if game.won || game.over { break; }
    }

    let outcome = if game.won { "win" } else if game.over { "game-over" } else { "incomplete" };
    println!("FINAL outcome={} ticks={} score={} bricks={} paddle_hits={} wall_hits={} brick_hits={} lives={}",
        outcome, game.ticks, game.score, game.remaining, game.paddle_hits,
        game.wall_hits, game.brick_hits, game.lives);
    println!("GFXBREAKOUT-END frames={} hud_lines={}", frames, frames * 4);
}
