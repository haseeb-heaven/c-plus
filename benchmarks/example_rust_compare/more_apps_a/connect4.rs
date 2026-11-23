// Safe-Rust model of examples/expanded/games_4/connect4.cp
// No unsafe, no FFI, no external crates. Deterministic 6x7 Connect-Four core.
// Build: rustc -O --edition 2021 connect4.rs -o connect4

use std::io::Read;

const ROWS: i32 = 6;
const COLS: i32 = 7;
const CELLS: usize = (ROWS * COLS) as usize;
const MAX_LEN: usize = 15;
const MAX_VALUE: i64 = 1_000_000;

const E_OVERSIZE: i32 = 1;
const E_RANGE: i32 = 2;
const E_ILLEGAL: i32 = 3;
const E_SYNTAX: i32 = 4;
const E_COUNT: i32 = 5;

const DEFAULT_MOVES: &str = "3 2 3 2 3 2 3";

fn is_space(ch: char) -> bool {
    ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r'
}

fn refuse_name(code: i32) -> &'static str {
    match code {
        E_OVERSIZE => "E_OVERSIZE",
        E_RANGE => "E_RANGE",
        E_ILLEGAL => "E_ILLEGAL",
        E_SYNTAX => "E_SYNTAX",
        _ => "E_COUNT",
    }
}

fn refuse(code: i32, detail: &str, step: i32) {
    let (errcode, category) = if code == E_OVERSIZE || code == E_RANGE {
        (101, 5)
    } else {
        (108, 1)
    };
    println!(
        "REFUSE step={} code={} name={} errcode={} category={} detail={}",
        step,
        code,
        refuse_name(code),
        errcode,
        category,
        detail
    );
    eprintln!(
        "connect4: error code={} category={} message={}",
        errcode, category, detail
    );
}

// Parse one whitespace-separated token into a signed column value.
fn parse_int_token(bytes: &[u8], cursor: &mut usize) -> Result<i32, i32> {
    let n = bytes.len();
    let mut i = *cursor;
    while i < n && is_space(bytes[i] as char) {
        i += 1;
    }
    if i >= n {
        *cursor = i;
        return Err(E_COUNT);
    }
    let mut negative = false;
    if bytes[i] == b'-' {
        negative = true;
        i += 1;
        if i >= n {
            return Err(E_SYNTAX);
        }
    }
    let mut len = 0usize;
    let mut value: i64 = 0;
    while i < n && !is_space(bytes[i] as char) {
        let ch = bytes[i];
        if ch < b'0' || ch > b'9' {
            return Err(E_SYNTAX);
        }
        if len >= MAX_LEN {
            return Err(E_OVERSIZE);
        }
        let d = (ch - b'0') as i64;
        if value > MAX_VALUE {
            return Err(E_OVERSIZE);
        }
        value = value * 10 + d;
        len += 1;
        i += 1;
    }
    if len == 0 {
        return Err(E_SYNTAX);
    }
    if value > MAX_VALUE {
        return Err(E_OVERSIZE);
    }
    *cursor = i;
    Ok(if negative { -(value as i32) } else { value as i32 })
}

fn is_compact(bytes: &[u8], start: usize, end: usize) -> bool {
    if end <= start + 1 {
        return false;
    }
    bytes[start..end].iter().all(|&c| c >= b'0' && c <= b'6')
}

fn parse_columns(text: &str, out: &mut Vec<i32>) -> Result<(), i32> {
    let bytes = text.as_bytes();
    let n = bytes.len();
    let mut i = 0usize;
    let mut parsed = 0usize;
    while i < n {
        while i < n && is_space(bytes[i] as char) {
            i += 1;
        }
        if i >= n {
            break;
        }
        let start = i;
        while i < n && !is_space(bytes[i] as char) {
            i += 1;
        }
        let end = i;
        if is_compact(bytes, start, end) {
            for &c in &bytes[start..end] {
                if parsed >= CELLS {
                    return Err(E_COUNT);
                }
                out.push((c - b'0') as i32);
                parsed += 1;
            }
        } else {
            let mut cursor = start;
            let value = parse_int_token(bytes, &mut cursor)?;
            if parsed >= CELLS {
                return Err(E_COUNT);
            }
            out.push(value);
            parsed += 1;
        }
    }
    if parsed == 0 {
        return Err(E_COUNT);
    }
    Ok(())
}

struct Connect4 {
    cells: [[i32; COLS as usize]; ROWS as usize],
    played: i32,
}

impl Connect4 {
    fn new() -> Self {
        Self {
            cells: [[0; COLS as usize]; ROWS as usize],
            played: 0,
        }
    }

    fn tile(&self, r: i32, c: i32) -> i32 {
        if r < 0 || r >= ROWS || c < 0 || c >= COLS {
            return -1;
        }
        self.cells[r as usize][c as usize]
    }

    fn drop(&mut self, col: i32, player: i32) -> Result<i32, i32> {
        if col < 0 || col >= COLS {
            return Err(E_RANGE);
        }
        let mut r = ROWS - 1;
        while r >= 0 {
            if self.cells[r as usize][col as usize] == 0 {
                self.cells[r as usize][col as usize] = player;
                self.played += 1;
                return Ok(r);
            }
            r -= 1;
        }
        Err(E_ILLEGAL)
    }

    fn win_dir(&self, row: i32, col: i32, player: i32) -> i32 {
        let drs: [i32; 4] = [0, 1, 1, 1];
        let dcs: [i32; 4] = [1, 0, 1, -1];
        for d in 0..4 {
            let mut count = 1;
            for step in 1..4 {
                if self.tile(row + drs[d] * step, col + dcs[d] * step) != player {
                    break;
                }
                count += 1;
            }
            for step in 1..4 {
                if self.tile(row - drs[d] * step, col - dcs[d] * step) != player {
                    break;
                }
                count += 1;
            }
            if count >= 4 {
                return (d + 1) as i32;
            }
        }
        0
    }

    fn full(&self) -> bool {
        self.played == ROWS * COLS
    }

    fn render(&self) {
        print!("  ");
        for c in 0..COLS {
            print!("{}", c);
        }
        println!();
        for r in 0..ROWS {
            print!("  ");
            for c in 0..COLS {
                let v = self.tile(r, c);
                let ch = if v < 0 {
                    '?'
                } else if v == 0 {
                    '.'
                } else if v == 1 {
                    'R'
                } else {
                    'Y'
                };
                print!("{}", ch);
            }
            println!();
        }
    }
}

fn win_name(dir: i32) -> &'static str {
    match dir {
        1 => "horizontal",
        2 => "vertical",
        3 => "diag-down-right",
        _ => "diag-down-left",
    }
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let mut moves: Vec<i32> = Vec::new();

    if args.len() > 1 {
        if args[1] == "-" {
            if args.len() != 2 {
                refuse(E_COUNT, "stdin sentinel '-' must be the only argument", 0);
                std::process::exit(2);
            }
            let mut buf = String::new();
            let _ = std::io::stdin().read_to_string(&mut buf);
            // fgets reads a single line; model only the first line.
            let line = buf.lines().next().unwrap_or("").to_string();
            if let Err(rc) = parse_columns(&line, &mut moves) {
                refuse(rc, "invalid stdin move sequence", 0);
                std::process::exit(2);
            }
        } else {
            for (idx, arg) in args.iter().skip(1).enumerate() {
                let step = (idx + 1) as i32;
                if let Err(rc) = parse_columns(arg, &mut moves) {
                    refuse(rc, "invalid column token", step);
                    std::process::exit(2);
                }
            }
        }
    } else if let Err(rc) = parse_columns(DEFAULT_MOVES, &mut moves) {
        refuse(rc, "internal default sequence is invalid", 0);
        std::process::exit(2);
    }

    println!("CONNECT4 rows={} cols={} moves={}", ROWS, COLS, moves.len());
    println!("INITIAL");
    let mut board = Connect4::new();
    board.render();

    let mut step = 0usize;
    let mut result_name = "incomplete";
    let mut result_detail = "none";

    for (i, &col) in moves.iter().enumerate() {
        step = i + 1;
        let player = if i % 2 == 0 { 1 } else { 2 };
        let row = match board.drop(col, player) {
            Ok(r) => r,
            Err(E_RANGE) => {
                refuse(E_RANGE, "column outside 0..6", step as i32);
                std::process::exit(2);
            }
            Err(_) => {
                refuse(E_ILLEGAL, "column is full", step as i32);
                std::process::exit(2);
            }
        };
        println!(
            "STEP {} player={} col={} row={}",
            step,
            if player == 1 { "R" } else { "Y" },
            col,
            row
        );
        board.render();

        let result = board.win_dir(row, col, player);
        if result != 0 {
            result_name = "win";
            result_detail = win_name(result);
            break;
        }
        if board.full() {
            result_name = "draw";
            result_detail = "board-full";
            break;
        }
    }

    println!("RESULT {} detail={} step={}", result_name, result_detail, step);
    let played = board.played;
    println!(
        "FINAL result={} played={} moves={} r={} y={}",
        result_name,
        played,
        moves.len(),
        (played + 1) >> 1,
        played >> 1
    );
}
