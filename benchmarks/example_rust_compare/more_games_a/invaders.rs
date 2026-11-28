// Safe-Rust model of examples/expanded/games_2/invaders.cp
//
// Models ONLY the deterministic observable behavior (stdout + exit status) for
// fixed argv inputs. No unsafe, no FFI, no wall clock, no external randomness.
// A fixed xorshift32 seed sets the initial ship column; everything after that
// is a pure function of the fixed command sequence.

use std::env;

const INVADERS_SEED: u32 = 0x13579BDF;
const W: i32 = 16;
const H: i32 = 10;
const ENEMY_ROWS: i32 = 3;
const ENEMY_COLS: i32 = 5;
const ENEMY_BASE: i32 = 5;
const MAX_BULLETS: usize = 16;
const MAX_TOKEN_LEN: usize = 64;
const MAX_TICKS: usize = 128;
const DEFAULT_CMDS: &str = "LFRFLFSRFLFLSFRFL";

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

fn cmd_code(ch: char) -> i32 {
    match ch {
        'L' | 'l' => 1, // CMD_LEFT
        'R' | 'r' => 2, // CMD_RIGHT
        'F' | 'f' => 3, // CMD_FIRE
        'S' | 's' | '.' => 0, // CMD_STILL
        _ => -1,
    }
}

fn cmd_name(code: i32) -> &'static str {
    match code {
        1 => "L",
        2 => "R",
        3 => "F",
        _ => "S",
    }
}

fn enemy_value(row: i32) -> i32 {
    if row == 0 {
        30
    } else if row == 1 {
        20
    } else {
        10
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

struct Invaders {
    enemies: Vec<Vec<i32>>,
    // Packed bullet: (by << 6) | bx, with bx < 64 and by < 64.
    bullets: [i32; MAX_BULLETS],
    bullet_used: [bool; MAX_BULLETS],
    ship: i32,
    score: i32,
    remaining: i32,
    fired: i32,
    hits: i32,
    shots_lost: i32,
    ticks: i32,
    won: bool,
}

impl Invaders {
    fn new(rng: &mut Rng) -> Self {
        let enemies = vec![vec![1; ENEMY_COLS as usize]; ENEMY_ROWS as usize];
        Invaders {
            enemies,
            bullets: [0; MAX_BULLETS],
            bullet_used: [false; MAX_BULLETS],
            ship: (rng.next() & 15) as i32,
            score: 0,
            remaining: ENEMY_ROWS * ENEMY_COLS,
            fired: 0,
            hits: 0,
            shots_lost: 0,
            ticks: 0,
            won: false,
        }
    }

    fn spawn_bullet(&mut self) {
        for i in 0..MAX_BULLETS {
            if !self.bullet_used[i] {
                let by = H - 2;
                self.bullets[i] = self.ship | (by << 6);
                self.bullet_used[i] = true;
                self.fired += 1;
                return;
            }
        }
        // Pool full: the shot is ignored (bounded, deterministic).
    }

    fn step(&mut self, cmd: i32) -> &'static str {
        self.ticks += 1;
        let mut event = "none";

        if cmd == 1 {
            if self.ship > 0 {
                self.ship -= 1;
            }
        } else if cmd == 2 {
            if self.ship < W - 1 {
                self.ship += 1;
            }
        } else if cmd == 3 {
            self.spawn_bullet();
            event = "fire";
        }

        // Walk backwards so removal by index cannot skip a bullet.
        for i in (0..MAX_BULLETS).rev() {
            if !self.bullet_used[i] {
                continue;
            }
            let bx = self.bullets[i] & 63;
            let mut by = self.bullets[i] >> 6;
            by -= 1;
            if by < 0 {
                self.bullet_used[i] = false;
                self.shots_lost += 1;
                event = "off";
                continue;
            }
            if by < ENEMY_ROWS {
                let ec = bx - ENEMY_BASE;
                if ec >= 0 && ec < ENEMY_COLS && self.enemies[by as usize][ec as usize] != 0 {
                    self.enemies[by as usize][ec as usize] = 0;
                    self.remaining -= 1;
                    self.score += enemy_value(by);
                    self.hits += 1;
                    self.bullet_used[i] = false;
                    event = "hit";
                    if self.remaining == 0 {
                        self.won = true;
                    }
                    continue;
                }
            }
            self.bullets[i] = bx | (by << 6);
        }
        event
    }

    fn live_bullets(&self) -> i32 {
        self.bullet_used.iter().filter(|&&u| u).count() as i32
    }

    fn render(&self) -> String {
        let mut out = String::new();
        for r in 0..H {
            out.push_str("  ");
            for c in 0..W {
                let mut ch = b'.';
                if r < ENEMY_ROWS
                    && c >= ENEMY_BASE
                    && c < ENEMY_BASE + ENEMY_COLS
                    && self.enemies[r as usize][(c - ENEMY_BASE) as usize] != 0
                {
                    ch = b'E';
                }
                for i in 0..MAX_BULLETS {
                    if self.bullet_used[i] {
                        let bx = self.bullets[i] & 63;
                        let by = self.bullets[i] >> 6;
                        if by == r && bx == c {
                            ch = b'|';
                        }
                    }
                }
                if r == H - 1 && c == self.ship {
                    ch = b'A';
                }
                out.push(ch as char);
            }
            out.push('\n');
        }
        out
    }
}

fn refuse(msg: &str) -> ! {
    eprintln!("invaders: refused: {}", msg);
    std::process::exit(2);
}

fn main() {
    let mut rng = Rng::new(INVADERS_SEED);
    let mut cmds: Vec<i32> = Vec::new();
    let mut game = Invaders::new(&mut rng);

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
                refuse("invalid command in stdin (expected L/R/S/F)");
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
                Err(false) => refuse("invalid command in stdin (expected L/R/S/F)"),
                Ok(()) => {}
            }
        } else {
            for arg in &args {
                match parse_cmds(arg, &mut cmds) {
                    Err(true) => refuse("token is oversized (max 64 characters)"),
                    Err(false) => refuse("invalid command token (expected L/R/S/F)"),
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
        "INVADERS seed=0x{:08X} grid={}x{} formation={}x{} enemies={} max_ticks={} cmds={}\n",
        INVADERS_SEED,
        W,
        H,
        ENEMY_ROWS,
        ENEMY_COLS,
        ENEMY_ROWS * ENEMY_COLS,
        MAX_TICKS,
        cmds.len()
    ));
    out.push_str(&format!(
        "INIT ship={} score=0 enemies={} bullets={}\n",
        game.ship,
        game.remaining,
        game.live_bullets()
    ));
    out.push_str(&game.render());

    for (i, &code) in cmds.iter().enumerate() {
        let event = game.step(code);
        out.push_str(&format!(
            "TICK {} cmd={} ship={} bullets={} event={} score={} enemies={}\n",
            i + 1,
            cmd_name(code),
            game.ship,
            game.live_bullets(),
            event,
            game.score,
            game.remaining
        ));
        out.push_str(&game.render());
        if game.won {
            break;
        }
    }

    let outcome = if game.won { "win" } else { "incomplete" };
    out.push_str(&format!(
        "FINAL outcome={} ticks={} score={} enemies={} fired={} hits={} shots_lost={}\n",
        outcome,
        game.ticks,
        game.score,
        game.remaining,
        game.fired,
        game.hits,
        game.shots_lost
    ));
    print!("{}", out);
}
