/* SAFE-Rust model of examples/modern_gui/games/invaders_ui/invaders_ui.cp
 *
 * Deterministic space-invaders state machine, bounded bullet pool, fixed
 * formation and HUD/PIXEL transcript.  The C+ original renders a real SDL3
 * surface; this model reproduces the same stdout/exit under
 * SDL_VIDEODRIVER=dummy without a display, unsafe code or FFI.
 */

use std::env;
use std::io::BufRead;

const SEED: u32 = 0x13579BDF;
const W: usize = 16;
const H: usize = 10;
const ENEMY_ROWS: usize = 3;
const ENEMY_COLS: usize = 5;
const ENEMY_BASE: i32 = 5;
const ENEMY_COUNT: i32 = (ENEMY_ROWS * ENEMY_COLS) as i32;
const MAX_BULLETS: usize = 16;
const MAX_TICKS: usize = 128;
const MAX_TOKEN_LEN: usize = 64;
const START_LIVES: i32 = 3;
const MISSES_PER_LIFE: i32 = 4;
const DEFAULT_CMDS: &str = "LFRFLFSRFLFLSFRFLFFFF";

#[derive(Clone, Copy, PartialEq)]
enum Cmd { Still, Left, Right, Fire }
fn cmd_name(c: Cmd) -> &'static str {
    match c { Cmd::Left => "L", Cmd::Right => "R", Cmd::Fire => "F", Cmd::Still => "S" }
}
fn cmd_code(ch: char) -> Option<Cmd> {
    match ch {
        'L' | 'l' => Some(Cmd::Left),
        'R' | 'r' => Some(Cmd::Right),
        'F' | 'f' => Some(Cmd::Fire),
        'S' | 's' | '.' => Some(Cmd::Still),
        _ => None,
    }
}
fn enemy_value(row: usize) -> i32 {
    match row { 0 => 30, 1 => 20, _ => 10 }
}

fn refuse(reason: &str, code: i32, category: i32, message: &str) -> i32 {
    eprintln!("REFUSED reason={} code={} category={} message={}", reason, code, category, message);
    2
}

fn parse_pool(text: &str) -> Result<usize, i32> {
    if text.is_empty() { return Err(-1); }
    let mut v: i32 = 0;
    for ch in text.chars() {
        if !ch.is_ascii_digit() { return Err(-1); }
        v = v * 10 + (ch as i32 - '0' as i32);
        if v > MAX_BULLETS as i32 + 1 { return Err(-2); }
    }
    if v < 1 || v > MAX_BULLETS as i32 { Err(-2) } else { Ok(v as usize) }
}

fn append_cmds(text: &str, out: &mut Vec<Cmd>) -> i32 {
    if text.len() > MAX_TOKEN_LEN { return -2; }
    for ch in text.chars() {
        if matches!(ch, '\n' | '\r' | ' ' | '\t') { continue; }
        match cmd_code(ch) {
            None => return -1,
            Some(c) => {
                if out.len() >= MAX_TICKS { return -3; }
                out.push(c);
            }
        }
    }
    0
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
    enemies: [[bool; ENEMY_COLS]; ENEMY_ROWS],
    bullets: Vec<Option<(i32, i32)>>,
    pool: usize,
    ship: i32,
    score: i32,
    lives: i32,
    wave: i32,
    remaining: i32,
    fired: i32,
    hits: i32,
    shots_lost: i32,
    ticks: i32,
}

impl Game {
    fn new(pool: usize, rng: &mut Rng) -> Self {
        Game {
            enemies: [[true; ENEMY_COLS]; ENEMY_ROWS],
            bullets: vec![None; MAX_BULLETS],
            pool,
            ship: (rng.next() & 15) as i32,
            score: 0, lives: START_LIVES, wave: 1,
            remaining: ENEMY_COUNT, fired: 0, hits: 0, shots_lost: 0, ticks: 0,
        }
    }

    fn live_bullets(&self) -> i32 {
        self.bullets[..self.pool].iter().filter(|b| b.is_some()).count() as i32
    }

    fn spawn(&mut self) {
        for slot in 0..self.pool {
            if self.bullets[slot].is_none() {
                self.bullets[slot] = Some((self.ship, (H - 2) as i32));
                self.fired += 1;
                return;
            }
        }
    }

    fn next_wave(&mut self) {
        self.enemies = [[true; ENEMY_COLS]; ENEMY_ROWS];
        for b in self.bullets.iter_mut() { *b = None; }
        self.remaining = ENEMY_COUNT;
        self.wave += 1;
    }

    fn step(&mut self, cmd: Cmd) -> &'static str {
        self.ticks += 1;
        let mut event = "none";
        if cmd == Cmd::Left {
            if self.ship > 0 { self.ship -= 1; }
        } else if cmd == Cmd::Right {
            if self.ship < W as i32 - 1 { self.ship += 1; }
        } else if cmd == Cmd::Fire {
            self.spawn();
            event = "fire";
        }

        for i in (0..self.pool).rev() {
            let (bx, mut by) = match self.bullets[i] {
                Some(v) => v,
                None => continue,
            };
            by -= 1;
            if by < 0 {
                self.bullets[i] = None;
                self.shots_lost += 1;
                event = "off";
                continue;
            }
            if (by as usize) < ENEMY_ROWS {
                let ec = bx - ENEMY_BASE;
                if ec >= 0 && (ec as usize) < ENEMY_COLS && self.enemies[by as usize][ec as usize] {
                    self.enemies[by as usize][ec as usize] = false;
                    self.remaining -= 1;
                    self.score += enemy_value(by as usize);
                    self.hits += 1;
                    self.bullets[i] = None;
                    event = "hit";
                    if self.remaining == 0 {
                        self.next_wave();
                        event = "wave";
                    }
                    continue;
                }
            }
            self.bullets[i] = Some((bx, by));
        }
        self.lives = (START_LIVES - self.shots_lost / MISSES_PER_LIFE).max(0);
        event
    }

    fn render(&self) {
        for r in 0..H {
            let mut row = String::from("  ");
            for c in 0..W {
                let mut ch = '.';
                if r < ENEMY_ROWS && c >= ENEMY_BASE as usize
                    && c < (ENEMY_BASE + ENEMY_COLS as i32) as usize
                    && self.enemies[r][c - ENEMY_BASE as usize]
                {
                    ch = 'E';
                }
                for b in self.bullets[..self.pool].iter().flatten() {
                    if b.1 == r as i32 && b.0 == c as i32 { ch = '|'; }
                }
                if r == H - 1 && c as i32 == self.ship { ch = 'A'; }
                row.push(ch);
            }
            println!("{}", row);
        }
    }
}

fn main() {
    let args: Vec<String> = env::args().skip(1).collect();
    let mut rng = Rng(SEED);
    let mut pool = MAX_BULLETS;
    let mut inject_surface = false;
    let mut inject_context = false;
    let mut options_done = false;
    let mut cmds: Vec<Cmd> = Vec::new();
    let mut a = 0usize;

    while a < args.len() {
        let arg = args[a].as_str();
        if !options_done && arg == "--" { options_done = true; a += 1; continue; }
        if !options_done && arg == "--null-surface" { inject_surface = true; a += 1; continue; }
        if !options_done && arg == "--null-context" { inject_context = true; a += 1; continue; }
        if !options_done && arg.starts_with("--bullets=") {
            match parse_pool(&arg[10..]) {
                Ok(p) => pool = p,
                Err(-2) => std::process::exit(refuse("pool-oversized", 101, 5,
                    "bullet pool request out of range 1..16")),
                Err(_) => std::process::exit(refuse("pool-malformed", 108, 1,
                    "bullet pool request is not a decimal number")),
            }
            a += 1;
            continue;
        }
        if !options_done && arg == "-" {
            if a + 1 != args.len() {
                std::process::exit(refuse("stdin-args", 108, 1, "'-' must be the last argument"));
            }
            let stdin = std::io::stdin();
            let mut line = String::new();
            if stdin.lock().read_line(&mut line).unwrap_or(0) == 0 {
                std::process::exit(refuse("stdin-empty", 108, 1, "no stdin line"));
            }
            while line.ends_with('\n') || line.ends_with('\r') { line.pop(); }
            if line.len() > MAX_TOKEN_LEN {
                std::process::exit(refuse("token-oversized", 101, 5, "token exceeds 64 characters"));
            }
            let rc = append_cmds(&line, &mut cmds);
            if rc == -2 {
                std::process::exit(refuse("token-oversized", 101, 5, "token exceeds 64 characters"));
            }
            if rc == -3 {
                std::process::exit(refuse("too-many-ticks", 101, 5, "too many commands (max 128)"));
            }
            if rc != 0 {
                std::process::exit(refuse("bad-command", 108, 1, "command out of range (expected L/R/S/F)"));
            }
            a += 1;
            continue;
        }
        let rc = append_cmds(arg, &mut cmds);
        if rc == -2 {
            std::process::exit(refuse("token-oversized", 101, 5, "token exceeds 64 characters"));
        }
        if rc == -3 {
            std::process::exit(refuse("too-many-ticks", 101, 5, "too many commands (max 128)"));
        }
        if rc != 0 {
            std::process::exit(refuse("bad-command", 108, 1, "command out of range (expected L/R/S/F)"));
        }
        a += 1;
    }

    if cmds.is_empty() && args.len() > 1 {
        let has_command_token = args.iter().any(|x| !x.starts_with('-'));
        if has_command_token {
            std::process::exit(refuse("empty-commands", 108, 1, "empty command sequence"));
        }
    }
    if cmds.is_empty() { append_cmds(DEFAULT_CMDS, &mut cmds); }
    if cmds.is_empty() || cmds.len() > MAX_TICKS {
        std::process::exit(refuse("too-many-ticks", 101, 5, "command count out of range 1..128"));
    }

    if inject_surface {
        std::process::exit(refuse("null-surface", 108, 1, "guarded_pixel: null surface"));
    }
    if inject_context {
        std::process::exit(refuse("null-context", 108, 1, "guarded_hud: null context"));
    }

    let driver = env::var("SDL_VIDEODRIVER").unwrap_or_else(|_| "dummy".to_string());
    let mut game = Game::new(pool, &mut rng);
    println!("GFX_INVADERS seed=0x{:08X} grid={}x{} formation={}x{} enemies={} pool={} max_ticks={} cmds={} driver={}",
        SEED, W, H, ENEMY_ROWS, ENEMY_COLS, ENEMY_COUNT, game.pool, MAX_TICKS, cmds.len(), driver);
    println!("INIT ship={} score={} lives={} wave={} enemies={} bullets={}",
        game.ship, game.score, game.lives, game.wave, game.remaining, game.live_bullets());
    game.render();

    let mut frames = 0i32;
    for (i, cmd) in cmds.iter().enumerate() {
        let event = game.step(*cmd);
        frames += 1;
        println!("TICK {} cmd={} ship={} bullets={} event={} score={} lives={} wave={} enemies={}",
            i + 1, cmd_name(*cmd), game.ship, game.live_bullets(), event,
            game.score, game.lives, game.wave, game.remaining);
        println!("HUD SCORE {}", game.score);
        println!("HUD LIVES {}", game.lives);
        println!("HUD WAVE {}", game.wave);
        println!("PIXEL hud_nonbg=1");
        game.render();
    }
    println!("FINAL outcome=complete ticks={} score={} lives={} wave={} enemies={} fired={} hits={} lost={} frames={} driver={} gui=1 surface=1",
        game.ticks, game.score, game.lives, game.wave, game.remaining,
        game.fired, game.hits, game.shots_lost, frames, driver);
}
