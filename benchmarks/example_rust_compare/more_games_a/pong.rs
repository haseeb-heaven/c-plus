// Safe-Rust model of examples/expanded/games_2/pong.cp
//
// Models ONLY the deterministic observable behavior (stdout + exit status) for
// fixed argv inputs. No unsafe, no FFI, no wall clock, no external randomness.
// The C+ source constructs the Pong object BEFORE rng_reseed(PONG_SEED), so the
// constructor consumes the statically zero-initialised xorshift32 state; the
// reseed has no observable effect. This model preserves that ordering.

use std::env;

const PONG_SEED: u32 = 0x1A2B3C4D;
const GRID_W: i32 = 21;
const PADDLE_W: i32 = 3;
const MAX_TOKEN_LEN: usize = 64;
const MAX_TICKS: usize = 128;
const DEFAULT_CMDS: &str = "SLLLRRRSSRRLL";

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

// Emulates C printf("%+d") for a small signed integer.
fn sgn(v: i32) -> String {
    if v >= 0 {
        format!("+{}", v)
    } else {
        format!("{}", v)
    }
}

fn cmd_code(ch: char) -> i32 {
    match ch {
        'L' | 'l' => 1, // CMD_LEFT
        'R' | 'r' => 2, // CMD_RIGHT
        'S' | 's' | '.' => 0, // CMD_STILL
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

// Ok(()) on success; Err(true) = oversized token, Err(false) = invalid command.
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

struct Pong {
    ball: i32,
    vel: i32,
    paddle: i32,
    score: i32,
    opp: i32,
    saves: i32,
    misses: i32,
    wall_hits: i32,
    ticks: i32,
}

impl Pong {
    fn new(rng: &mut Rng) -> Self {
        let first = rng.next();
        let second = rng.next();
        Pong {
            ball: GRID_W / 2,
            vel: if first & 1 != 0 { 1 } else { -1 },
            paddle: (second & 3) as i32,
            score: 0,
            opp: 0,
            saves: 0,
            misses: 0,
            wall_hits: 0,
            ticks: 0,
        }
    }

    fn step(&mut self, cmd: i32) -> &'static str {
        self.ticks += 1;
        if cmd == 1 {
            if self.paddle > 0 {
                self.paddle -= 1;
            }
        } else if cmd == 2 {
            if self.paddle < GRID_W - PADDLE_W {
                self.paddle += 1;
            }
        }

        let mut b = self.ball + self.vel;
        let mut event = "none";
        if b >= GRID_W - 1 {
            b = GRID_W - 2;
            self.vel = -1;
            self.wall_hits += 1;
            event = "wall";
        }
        if b <= 0 {
            if self.paddle == 0 {
                b = 1;
                self.vel = 1;
                self.score += 1;
                self.saves += 1;
                event = "save";
            } else {
                b = GRID_W / 2;
                self.vel = 1;
                self.opp += 1;
                self.misses += 1;
                event = "miss";
            }
        }
        self.ball = b;
        event
    }

    fn render(&self) -> String {
        let mut line = vec![b'.'; GRID_W as usize];
        line[(GRID_W - 1) as usize] = b'|';
        for i in self.paddle..self.paddle + PADDLE_W {
            line[i as usize] = b'=';
        }
        line[self.ball as usize] = b'o';
        format!("  {}\n", String::from_utf8(line).unwrap())
    }
}

fn refuse(msg: &str) -> ! {
    eprintln!("pong: refused: {}", msg);
    std::process::exit(2);
}

fn main() {
    // Constructor runs before the (no-op) reseed in the C+ source.
    let mut rng = Rng::new(0);
    let mut cmds: Vec<i32> = Vec::new();
    let mut game = Pong::new(&mut rng);

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
            // Emulate fgets(buf, MAX_TOKEN_LEN + 2, stdin): a line whose
            // stored length (newline included) is >= MAX_TOKEN_LEN + 1 is
            // treated as oversized.
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
    } else if let Err(_) = parse_cmds(DEFAULT_CMDS, &mut cmds) {
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
        "PONG seed=0x{:08X} grid={} paddle_w={} max_ticks={} cmds={}\n",
        PONG_SEED, GRID_W, PADDLE_W, MAX_TICKS, cmds.len()
    ));
    out.push_str(&format!(
        "INIT ball={} v={} paddle=[{},{}] score=0 opp=0\n",
        game.ball,
        sgn(game.vel),
        game.paddle,
        game.paddle + PADDLE_W - 1
    ));
    out.push_str(&game.render());

    for (i, &code) in cmds.iter().enumerate() {
        let event = game.step(code);
        out.push_str(&format!(
            "TICK {} cmd={} ball={} v={} paddle=[{},{}] event={} score={} opp={}\n",
            i + 1,
            cmd_name(code),
            game.ball,
            sgn(game.vel),
            game.paddle,
            game.paddle + PADDLE_W - 1,
            event,
            game.score,
            game.opp
        ));
        out.push_str(&game.render());
    }

    out.push_str(&format!(
        "FINAL ticks={} saves={} misses={} wall_hits={} score={} opp={}\n",
        game.ticks, game.saves, game.misses, game.wall_hits, game.score, game.opp
    ));
    print!("{}", out);
}
