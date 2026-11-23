/* Safe-Rust equivalent of examples/expanded/engine/engine_loop.cp.
 *
 * Fixed-timestep, fixed-seed state machine. The C+ original stores input tokens
 * in stdlib::vector with at_checked(); this model uses an owned Vec<i32> and
 * slice indexing guarded by iteration bounds. No unsafe, no FFI.
 */

const MAX_TICKS: usize = 16;
const LANE_MIN: i32 = -8;
const LANE_MAX: i32 = 8;
const TARGET_COUNT: usize = 3;

const IN_NONE: i32 = 0;
const IN_LEFT: i32 = -1;
const IN_RIGHT: i32 = 1;
const IN_FIRE: i32 = 2;

fn rng_next(state: &mut u64, lo: i32, hi: i32) -> i32 {
    if hi <= lo {
        return lo;
    }
    *state = state
        .wrapping_mul(6_364_136_223_846_793_005)
        .wrapping_add(1_442_695_040_888_963_407);
    let bits = ((*state >> 33) & 0xffff_ffff) as u32;
    let span = (hi - lo + 1) as u32;
    if span == 0 {
        return lo;
    }
    lo + (bits % span) as i32
}

fn input_name(cmd: i32) -> &'static str {
    match cmd {
        IN_LEFT => "L",
        IN_RIGHT => "R",
        IN_FIRE => "F",
        _ => "N",
    }
}

fn parse_token(token: &str) -> Option<i32> {
    if token.len() != 1 {
        return None;
    }
    match token.as_bytes()[0] {
        b'L' => Some(IN_LEFT),
        b'R' => Some(IN_RIGHT),
        b'F' => Some(IN_FIRE),
        b'N' => Some(IN_NONE),
        _ => None,
    }
}

struct Game {
    x: i32,
    lives: i32,
    score: i32,
    hits: i32,
    collisions: i32,
    hazard: i32,
    targets: [i32; TARGET_COUNT],
}

impl Game {
    fn checksum(&self) -> i32 {
        let mut sum = self.score * 100 + self.hits * 10 + self.collisions;
        sum += self.x + self.lives * 7 + self.hazard;
        for &t in &self.targets {
            sum += t;
        }
        sum
    }
}

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    if args.is_empty() {
        println!(
            "ERROR usage: engine_loop <N|L|R|F> [token ...] code= 108"
        );
        std::process::exit(2);
    }

    let mut inputs: Vec<i32> = Vec::new();
    for token in &args {
        match parse_token(token) {
            Some(cmd) => inputs.push(cmd),
            None => {
                println!("ERROR invalid input token token= {} code= 108", token);
                std::process::exit(2);
            }
        }
    }

    let n = inputs.len().min(MAX_TICKS);
    let mut rng_state = 0xabcd_1234u64;
    let mut g = Game {
        x: 0,
        lives: 3,
        score: 0,
        hits: 0,
        collisions: 0,
        hazard: 7,
        targets: [-5, 0, 5],
    };

    println!(
        "LOOP ticks= {} seed=0xabcd1234 lane= {} .. {}",
        n, LANE_MIN, LANE_MAX
    );
    for tick in 0..n {
        let cmd = inputs[tick];
        g.x += cmd;
        if g.x < LANE_MIN {
            g.x = LANE_MIN;
        }
        if g.x > LANE_MAX {
            g.x = LANE_MAX;
        }

        g.hazard -= 1;
        if g.hazard < LANE_MIN {
            g.hazard = LANE_MAX;
        }

        if g.x == g.hazard {
            g.lives -= 1;
            g.collisions += 1;
            g.x = 0;
            println!("COLLISION tick= {} lives= {}", tick, g.lives);
        }

        if cmd == IN_FIRE {
            let mut hit_index: i32 = -1;
            for i in 0..TARGET_COUNT {
                let mut distance = g.x - g.targets[i];
                if distance < 0 {
                    distance = -distance;
                }
                if distance <= 1 {
                    hit_index = i as i32;
                    break;
                }
            }
            if hit_index >= 0 {
                g.score += 10;
                g.hits += 1;
                g.targets[hit_index as usize] = rng_next(&mut rng_state, LANE_MIN, LANE_MAX);
                println!(
                    "HIT tick= {} target= {} score= {}",
                    tick, hit_index, g.score
                );
            } else {
                println!("MISS tick= {} x= {}", tick, g.x);
            }
        }

        println!(
            "STATE tick= {} input= {} x= {} hazard= {} score= {} lives= {} hits= {}",
            tick,
            input_name(cmd),
            g.x,
            g.hazard,
            g.score,
            g.lives,
            g.hits
        );
    }

    println!(
        "SUMMARY ticks= {} score= {} hits= {} collisions= {} lives= {} checksum= {}",
        n,
        g.score,
        g.hits,
        g.collisions,
        g.lives,
        g.checksum()
    );
}
