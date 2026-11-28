// Safe-Rust model of examples/expanded/games_3/minesweeper.cp
//
// Models ONLY the deterministic observable behavior (stdout + exit status) for
// fixed argv inputs. No unsafe, no FFI, no randomness. The mine layout is fixed
// static data; all refusals are observed run-time errors.

use std::env;

const N: i32 = 9;
const CELLS: usize = 81;
const NMINES: usize = 11;

const CMD_REVEAL: i32 = -100;
const CMD_FLAG: i32 = -101;
const CMD_STATUS: i32 = -102;

const STATE_PLAY: i32 = 0;
const STATE_WIN: i32 = 1;
const STATE_LOSE: i32 = 2;

const E_OK: i32 = 0;
const E_PARSE: i32 = 1;
const E_FORMAT: i32 = 2;
const E_BOUNDS: i32 = 3;
const E_FLAGGED: i32 = 4;
const E_REVEALED: i32 = 5;

const MINES: [usize; NMINES] = [0, 8, 13, 20, 33, 37, 43, 50, 57, 63, 80];

fn state_name(state: i32) -> &'static str {
    match state {
        STATE_WIN => "win",
        STATE_LOSE => "lose",
        _ => "playing",
    }
}

fn code_name(code: i32) -> &'static str {
    match code {
        E_OK => "E_OK",
        E_PARSE => "E_PARSE",
        E_FORMAT => "E_FORMAT",
        E_BOUNDS => "E_BOUNDS",
        E_FLAGGED => "E_FLAGGED",
        _ => "E_REVEALED",
    }
}

fn parse_int(arg: &str) -> Result<i32, bool> {
    let bytes = arg.as_bytes();
    if bytes.is_empty() {
        return Err(false);
    }
    let mut i = 0usize;
    let mut negative = false;
    if bytes[0] == b'-' {
        negative = true;
        i = 1;
    } else if bytes[0] == b'+' {
        i = 1;
    }
    if i == bytes.len() {
        return Err(false);
    }
    let mut value: i32 = 0;
    let mut digits = 0i32;
    while i < bytes.len() {
        let ch = bytes[i];
        if ch < b'0' || ch > b'9' {
            return Err(false);
        }
        if value > 100000 || digits >= 8 {
            return Err(true);
        }
        value = value * 10 + (ch - b'0') as i32;
        digits += 1;
        i += 1;
    }
    Ok(if negative { -value } else { value })
}

struct Minefield {
    mine: [bool; CELLS],
    adj: [i32; CELLS],
    revealed: [bool; CELLS],
    flagged: [bool; CELLS],
    safe_total: i32,
    revealed_count: i32,
    flag_count: i32,
    state: i32,
    lose_idx: i32,
}

impl Minefield {
    fn new() -> Self {
        let mut mine = [false; CELLS];
        for &idx in MINES.iter() {
            if idx < CELLS {
                mine[idx] = true;
            }
        }
        let mut adj = [0i32; CELLS];
        for idx in 0..CELLS {
            let mut count = 0;
            let r = (idx as i32) / N;
            let c = (idx as i32) - ((idx as i32) / N) * N;
            for dr in -1..=1 {
                for dc in -1..=1 {
                    if dr == 0 && dc == 0 {
                        continue;
                    }
                    let nr = r + dr;
                    let nc = c + dc;
                    if nr < 0 || nr >= N || nc < 0 || nc >= N {
                        continue;
                    }
                    let nidx = (nr * N + nc) as usize;
                    if mine[nidx] {
                        count += 1;
                    }
                }
            }
            adj[idx] = count;
        }
        Minefield {
            mine,
            adj,
            revealed: [false; CELLS],
            flagged: [false; CELLS],
            safe_total: (CELLS - NMINES) as i32,
            revealed_count: 0,
            flag_count: 0,
            state: STATE_PLAY,
            lose_idx: -1,
        }
    }

    fn index_of(r: i32, c: i32) -> i32 {
        if r < 0 || r >= N || c < 0 || c >= N {
            -1
        } else {
            r * N + c
        }
    }

    fn over(&self) -> bool {
        self.state != STATE_PLAY
    }

    // Returns Ok(effect) or Err(code).
    fn reveal(&mut self, r: i32, c: i32) -> Result<i32, i32> {
        let idx = Self::index_of(r, c);
        if idx < 0 {
            return Err(E_BOUNDS);
        }
        let idx = idx as usize;
        if self.over() {
            return Ok(0);
        }
        if self.flagged[idx] {
            return Err(E_FLAGGED);
        }
        if self.revealed[idx] {
            return Ok(0);
        }
        let mut queued = [false; CELLS];
        if self.mine[idx] {
            self.revealed[idx] = true;
            self.state = STATE_LOSE;
            self.lose_idx = idx as i32;
            return Ok(1);
        }
        let mut stack: Vec<usize> = Vec::with_capacity(CELLS);
        stack.push(idx);
        queued[idx] = true;
        while let Some(cur) = stack.pop() {
            if self.revealed[cur] || self.flagged[cur] {
                continue;
            }
            self.revealed[cur] = true;
            self.revealed_count += 1;
            if self.adj[cur] != 0 {
                continue;
            }
            let cr = (cur as i32) / N;
            let cc = (cur as i32) - ((cur as i32) / N) * N;
            for dr in -1..=1 {
                for dc in -1..=1 {
                    if dr == 0 && dc == 0 {
                        continue;
                    }
                    let nr = cr + dr;
                    let nc = cc + dc;
                    if nr < 0 || nr >= N || nc < 0 || nc >= N {
                        continue;
                    }
                    let nidx = (nr * N + nc) as usize;
                    if self.mine[nidx] || self.revealed[nidx] || self.flagged[nidx] || queued[nidx] {
                        continue;
                    }
                    if stack.len() >= CELLS {
                        continue;
                    }
                    stack.push(nidx);
                    queued[nidx] = true;
                }
            }
        }
        if self.revealed_count >= self.safe_total {
            self.state = STATE_WIN;
        }
        Ok(1)
    }

    fn flag(&mut self, r: i32, c: i32) -> Result<i32, i32> {
        let idx = Self::index_of(r, c);
        if idx < 0 {
            return Err(E_BOUNDS);
        }
        let idx = idx as usize;
        if self.over() {
            return Ok(0);
        }
        if self.revealed[idx] {
            return Err(E_REVEALED);
        }
        if self.flagged[idx] {
            self.flagged[idx] = false;
            self.flag_count -= 1;
            Ok(2)
        } else {
            self.flagged[idx] = true;
            self.flag_count += 1;
            Ok(1)
        }
    }

    fn render(&self) -> String {
        let mut out = String::new();
        for i in 0..CELLS {
            let r = (i as i32) / N;
            let c = (i as i32) - ((i as i32) / N) * N;
            if c != 0 {
                out.push(' ');
            }
            if self.flagged[i] {
                out.push('F');
            } else if self.mine[i] && self.state == STATE_LOSE {
                out.push('*');
            } else if !self.revealed[i] {
                out.push('#');
            } else if self.adj[i] == 0 {
                out.push('.');
            } else {
                out.push(char::from(b'0' + self.adj[i] as u8));
            }
            if c == N - 1 {
                out.push_str(&format!(" r{}\n", r));
            }
        }
        out
    }
}

fn refuse_line(out: &mut String, step: i32, detail: &str, code: i32, reason: &str) {
    out.push_str(&format!(
        "REFUSE step={} {} code={} reason={}\n",
        step, detail, code_name(code), reason
    ));
}

fn refuse_arg(out: &mut String, token: &str, code: i32, reason: &str) {
    refuse_line(out, 0, &format!("arg={}", token), code, reason);
}

fn main() {
    let mut field = Minefield::new();
    let args: Vec<String> = env::args().skip(1).collect();
    let mut prog: Vec<i32> = Vec::new();
    let mut out = String::new();

    let parse_code = if !args.is_empty() {
        let mut code = E_OK;
        for arg in &args {
            if arg.len() == 1 {
                match arg.as_bytes()[0] {
                    b'R' | b'r' => { prog.push(CMD_REVEAL); continue; }
                    b'F' | b'f' => { prog.push(CMD_FLAG); continue; }
                    b'V' | b'v' => { prog.push(CMD_STATUS); continue; }
                    _ => {}
                }
            }
            match parse_int(arg) {
                Ok(v) => prog.push(v),
                Err(oversized) => {
                    refuse_arg(&mut out, arg, E_PARSE,
                        if oversized { "numeric token is oversized" } else { "numeric token is malformed" });
                    code = E_PARSE;
                    break;
                }
            }
        }
        code
    } else {
        for i in 0..CELLS {
            if !MINES.contains(&i) {
                let r = (i as i32) / N;
                let c = (i as i32) - ((i as i32) / N) * N;
                prog.push(CMD_REVEAL);
                prog.push(r);
                prog.push(c);
            }
        }
        prog.push(CMD_STATUS);
        E_OK
    };

    if parse_code != E_OK {
        out.push_str(&format!("MINESWEEPER_DONE status=REFUSED code={}\n", code_name(parse_code)));
        print!("{}", out);
        std::process::exit(2);
    }
    if prog.is_empty() {
        refuse_line(&mut out, 0, "program=empty", E_FORMAT, "empty move program");
        out.push_str(&format!("MINESWEEPER_DONE status=REFUSED code={}\n", code_name(E_FORMAT)));
        print!("{}", out);
        std::process::exit(2);
    }

    out.push_str(&format!("MINESWEEPER grid=9x9 mines={} safe={} program={}\n",
        NMINES, field.safe_total, prog.len()));
    out.push_str("INITIAL\n");
    out.push_str(&field.render());

    let mut pc: usize = 0;
    let mut step = 0;
    let mut refused = false;
    let mut final_code = E_OK;
    let mut win_step = 0;
    let mut lose_step = 0;

    while pc < prog.len() {
        let cmd = prog[pc];
        pc += 1;

        if cmd == CMD_REVEAL || cmd == CMD_FLAG {
            if pc + 2 > prog.len() {
                refuse_line(&mut out, step + 1,
                    if cmd == CMD_REVEAL { "cmd=R operands=missing" } else { "cmd=F operands=missing" },
                    E_FORMAT, "cell move needs r c");
                refused = true; final_code = E_FORMAT; break;
            }
            let r = prog[pc];
            let c = prog[pc + 1];
            pc += 2;
            step += 1;
            let detail = if cmd == CMD_REVEAL {
                format!("cmd=R r={} c={}", r, c)
            } else {
                format!("cmd=F r={} c={}", r, c)
            };
            let result = if cmd == CMD_REVEAL { field.reveal(r, c) } else { field.flag(r, c) };
            match result {
                Err(code) => {
                    let reason = if code == E_BOUNDS {
                        "cell coordinate out of range (0..8)"
                    } else if code == E_FLAGGED {
                        "cell is flagged; unflag it first"
                    } else {
                        "cell is already revealed"
                    };
                    refuse_line(&mut out, step, &detail, code, reason);
                    refused = true; final_code = code; break;
                }
                Ok(effect) => {
                    let note = if effect == 1 {
                        if cmd == CMD_REVEAL { "revealed" } else { "flagged" }
                    } else if effect == 2 {
                        "unflagged"
                    } else {
                        "noop"
                    };
                    out.push_str(&format!(
                        "STEP {} {} code=E_OK effect={} state={} revealed={} flags={}\n",
                        step, detail, note, state_name(field.state), field.revealed_count, field.flag_count
                    ));
                    if field.state == STATE_WIN && win_step == 0 {
                        win_step = step;
                        out.push_str(&format!("RESULT win step={} revealed={}\n", win_step, field.revealed_count));
                    }
                    if field.state == STATE_LOSE && lose_step == 0 {
                        let li = field.lose_idx;
                        let lr = li / N;
                        let lc = li - (li / N) * N;
                        lose_step = step;
                        out.push_str(&format!("RESULT lose step={} mine=({},{})\n", lose_step, lr, lc));
                    }
                }
            }
        } else if cmd == CMD_STATUS {
            step += 1;
            out.push_str(&format!(
                "STATUS step={} code=E_OK state={} revealed={}/{} flags={}\n",
                step, state_name(field.state), field.revealed_count, field.safe_total, field.flag_count
            ));
        } else {
            refuse_line(&mut out, step + 1, &format!("cmd={} unknown", cmd), E_FORMAT,
                "unknown command token (expected R/F/V)");
            refused = true; final_code = E_FORMAT; break;
        }
    }

    if refused {
        out.push_str(&format!("MINESWEEPER_DONE status=REFUSED code={}\n", code_name(final_code)));
        print!("{}", out);
        std::process::exit(2);
    }

    out.push_str(&format!("FINAL state={} revealed={}/{} flags={} steps={}\n",
        state_name(field.state), field.revealed_count, field.safe_total, field.flag_count, step));
    out.push_str("FINAL_BOARD\n");
    out.push_str(&field.render());
    out.push_str(&format!("MINESWEEPER_DONE status=OK state={}\n", state_name(field.state)));
    print!("{}", out);
}
