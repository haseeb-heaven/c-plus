// Safe-Rust model of examples/expanded/games_3/sudoku.cp
//
// Models ONLY the deterministic observable behavior (stdout + exit status) for
// fixed argv inputs. No unsafe, no FFI, no randomness. The fixed puzzle and
// solution come from static data; all refusals are observed run-time errors.

use std::env;

const N: i32 = 9;
const CELLS: usize = 81;
const CMD_PLACE: i32 = -100;
const CMD_REMOVE: i32 = -101;
const CMD_VALIDATE: i32 = -102;

const E_OK: i32 = 0;
const E_PARSE: i32 = 1;
const E_FORMAT: i32 = 2;
const E_BOUNDS: i32 = 3;
const E_GIVEN: i32 = 4;
const E_CONFLICT: i32 = 5;
const E_OCCUPIED: i32 = 6;
const E_EMPTY: i32 = 7;

const GIVEN: [i32; CELLS] = [
    5, 3, 0, 0, 7, 0, 0, 0, 0,
    6, 0, 0, 1, 9, 5, 0, 0, 0,
    0, 9, 8, 0, 0, 0, 0, 6, 0,
    8, 0, 0, 0, 6, 0, 0, 0, 3,
    4, 0, 0, 8, 0, 3, 0, 0, 1,
    7, 0, 0, 0, 2, 0, 0, 0, 6,
    0, 6, 0, 0, 0, 0, 2, 8, 0,
    0, 0, 0, 4, 1, 9, 0, 0, 5,
    0, 0, 0, 0, 8, 0, 0, 7, 9,
];

const SOLUTION: [i32; CELLS] = [
    5, 3, 4, 6, 7, 8, 9, 1, 2,
    6, 7, 2, 1, 9, 5, 3, 4, 8,
    1, 9, 8, 3, 4, 2, 5, 6, 7,
    8, 5, 9, 7, 6, 1, 4, 2, 3,
    4, 2, 6, 8, 5, 3, 7, 9, 1,
    7, 1, 3, 9, 2, 4, 8, 5, 6,
    9, 6, 1, 5, 3, 7, 2, 8, 4,
    2, 8, 7, 4, 1, 9, 6, 3, 5,
    3, 4, 5, 2, 8, 6, 1, 7, 9,
];

fn code_name(code: i32) -> &'static str {
    match code {
        E_OK => "E_OK",
        E_PARSE => "E_PARSE",
        E_FORMAT => "E_FORMAT",
        E_BOUNDS => "E_BOUNDS",
        E_GIVEN => "E_GIVEN",
        E_CONFLICT => "E_CONFLICT",
        E_OCCUPIED => "E_OCCUPIED",
        _ => "E_EMPTY",
    }
}

// Strict, overflow-aware integer parse: Ok(value), or Err(oversized?).
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

struct Sudoku {
    cells: [i32; CELLS],
    given: [bool; CELLS],
    givens: i32,
    changes: i32,
}

impl Sudoku {
    fn new() -> Self {
        let mut cells = [0i32; CELLS];
        let mut given = [false; CELLS];
        let mut givens = 0;
        for i in 0..CELLS {
            cells[i] = GIVEN[i];
            if GIVEN[i] != 0 {
                given[i] = true;
                givens += 1;
            }
        }
        Sudoku { cells, given, givens, changes: 0 }
    }

    fn index_of(r: i32, c: i32) -> i32 {
        if r < 0 || r >= N || c < 0 || c >= N {
            -1
        } else {
            r * N + c
        }
    }

    fn legal_digit(&self, idx: i32, d: i32) -> bool {
        let r = idx / N;
        let c = idx - (idx / N) * N;
        let br = (r / 3) * 3;
        let bc = (c / 3) * 3;
        for i in 0..N {
            if self.cells[(r * N + i) as usize] == d {
                return false;
            }
            if self.cells[(i * N + c) as usize] == d {
                return false;
            }
        }
        for dr in 0..3 {
            for dc in 0..3 {
                if self.cells[((br + dr) * N + bc + dc) as usize] == d {
                    return false;
                }
            }
        }
        true
    }

    // Returns E_* code.
    fn place(&mut self, r: i32, c: i32, d: i32) -> i32 {
        let idx = Self::index_of(r, c);
        if idx < 0 {
            return E_BOUNDS;
        }
        if d < 1 || d > 9 {
            return E_BOUNDS;
        }
        if self.given[idx as usize] {
            return E_GIVEN;
        }
        if self.cells[idx as usize] != 0 {
            return E_OCCUPIED;
        }
        if !self.legal_digit(idx, d) {
            return E_CONFLICT;
        }
        self.cells[idx as usize] = d;
        self.changes += 1;
        E_OK
    }

    fn remove(&mut self, r: i32, c: i32) -> i32 {
        let idx = Self::index_of(r, c);
        if idx < 0 {
            return E_BOUNDS;
        }
        if self.given[idx as usize] {
            return E_GIVEN;
        }
        if self.cells[idx as usize] == 0 {
            return E_EMPTY;
        }
        self.cells[idx as usize] = 0;
        self.changes += 1;
        E_OK
    }

    fn solved(&self) -> bool {
        for i in 0..CELLS {
            if self.cells[i] < 1 || self.cells[i] > 9 {
                return false;
            }
        }
        let mut seen = [0i32; 10];
        for r in 0..N {
            seen.iter_mut().for_each(|x| *x = 0);
            for c in 0..N {
                seen[self.cells[(r * N + c) as usize] as usize] += 1;
            }
            for i in 1..=9 {
                if seen[i as usize] != 1 {
                    return false;
                }
            }
        }
        for c in 0..N {
            seen.iter_mut().for_each(|x| *x = 0);
            for r in 0..N {
                seen[self.cells[(r * N + c) as usize] as usize] += 1;
            }
            for i in 1..=9 {
                if seen[i as usize] != 1 {
                    return false;
                }
            }
        }
        for dr in 0..3 {
            for dc in 0..3 {
                seen.iter_mut().for_each(|x| *x = 0);
                for r in 0..3 {
                    for c in 0..3 {
                        let idx = (dr * 3 + r) * N + dc * 3 + c;
                        seen[self.cells[idx as usize] as usize] += 1;
                    }
                }
                for i in 1..=9 {
                    if seen[i as usize] != 1 {
                        return false;
                    }
                }
            }
        }
        true
    }

    fn render(&self) -> String {
        let mut out = String::new();
        for r in 0..N {
            out.push_str("  ");
            for c in 0..N {
                let v = self.cells[(r * N + c) as usize];
                if c == 3 || c == 6 {
                    out.push_str("| ");
                }
                if v == 0 {
                    out.push('.');
                } else {
                    out.push(char::from(b'0' + v as u8));
                }
                out.push(' ');
            }
            out.push('\n');
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
    let mut board = Sudoku::new();
    let args: Vec<String> = env::args().skip(1).collect();
    let mut prog: Vec<i32> = Vec::new();
    let mut out = String::new();

    let parse_code = if !args.is_empty() {
        let mut code = E_OK;
        for arg in &args {
            if arg.len() == 1 {
                match arg.as_bytes()[0] {
                    b'P' | b'p' => { prog.push(CMD_PLACE); continue; }
                    b'R' | b'r' => { prog.push(CMD_REMOVE); continue; }
                    b'V' | b'v' => { prog.push(CMD_VALIDATE); continue; }
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
            if GIVEN[i] == 0 {
                let r = (i as i32) / N;
                let c = (i as i32) - ((i as i32) / N) * N;
                prog.push(CMD_PLACE);
                prog.push(r);
                prog.push(c);
                prog.push(SOLUTION[i]);
            }
        }
        prog.push(CMD_VALIDATE);
        E_OK
    };

    if parse_code != E_OK {
        out.push_str(&format!("SUDOKU_DONE status=REFUSED code={}\n", code_name(parse_code)));
        print!("{}", out);
        std::process::exit(2);
    }
    if prog.is_empty() {
        refuse_line(&mut out, 0, "program=empty", E_FORMAT, "empty move program");
        out.push_str(&format!("SUDOKU_DONE status=REFUSED code={}\n", code_name(E_FORMAT)));
        print!("{}", out);
        std::process::exit(2);
    }

    out.push_str(&format!("SUDOKU grid=9x9 givens={} program={}\n", board.givens, prog.len()));
    out.push_str("INITIAL\n");
    out.push_str(&board.render());

    let mut pc: usize = 0;
    let mut step = 0;
    let mut refused = false;
    let mut final_code = E_OK;

    while pc < prog.len() {
        let cmd = prog[pc];
        pc += 1;

        if cmd == CMD_PLACE {
            if pc + 3 > prog.len() {
                refuse_line(&mut out, step + 1, "cmd=P operands=missing", E_FORMAT, "place needs r c");
                refused = true; final_code = E_FORMAT; break;
            }
            let r = prog[pc];
            let c = prog[pc + 1];
            let d = prog[pc + 2];
            pc += 3;
            step += 1;
            let detail = format!("cmd=P r={} c={} d={}", r, c, d);
            let code = board.place(r, c, d);
            if code != E_OK {
                let reason = if code == E_BOUNDS {
                    "cell index or digit out of range (0..8 / 1..9)"
                } else if code == E_GIVEN {
                    "cell is a fixed given"
                } else if code == E_OCCUPIED {
                    "cell already filled"
                } else {
                    "digit conflicts with row, column or box"
                };
                refuse_line(&mut out, step, &detail, code, reason);
                refused = true; final_code = code; break;
            }
            out.push_str(&format!("STEP {} {} code=E_OK\n", step, detail));
        } else if cmd == CMD_REMOVE {
            if pc + 2 > prog.len() {
                refuse_line(&mut out, step + 1, "cmd=R operands=missing", E_FORMAT, "remove needs r c");
                refused = true; final_code = E_FORMAT; break;
            }
            let r = prog[pc];
            let c = prog[pc + 1];
            pc += 2;
            step += 1;
            let detail = format!("cmd=R r={} c={}", r, c);
            let code = board.remove(r, c);
            if code != E_OK {
                let reason = if code == E_BOUNDS {
                    "cell index out of range (0..8)"
                } else if code == E_GIVEN {
                    "cell is a fixed given"
                } else {
                    "cell is already empty"
                };
                refuse_line(&mut out, step, &detail, code, reason);
                refused = true; final_code = code; break;
            }
            out.push_str(&format!("STEP {} {} code=E_OK\n", step, detail));
        } else if cmd == CMD_VALIDATE {
            step += 1;
            let solved_now = board.solved();
            out.push_str(&format!("VALIDATE step={} code=E_OK solved={}\n", step, if solved_now {1} else {0}));
        } else {
            refuse_line(&mut out, step + 1, &format!("cmd={} unknown", cmd), E_FORMAT,
                "unknown command token (expected P/R/V)");
            refused = true; final_code = E_FORMAT; break;
        }
    }

    if refused {
        out.push_str(&format!("SUDOKU_DONE status=REFUSED code={}\n", code_name(final_code)));
        print!("{}", out);
        std::process::exit(2);
    }

    let solved_final = board.solved();
    out.push_str(&format!("FINAL solved={} changes={} steps={}\n",
        if solved_final {1} else {0}, board.changes, step));
    out.push_str("FINAL_BOARD\n");
    out.push_str(&board.render());
    out.push_str(&format!("SUDOKU_DONE status=OK solved={}\n", if solved_final {1} else {0}));
    print!("{}", out);
}
