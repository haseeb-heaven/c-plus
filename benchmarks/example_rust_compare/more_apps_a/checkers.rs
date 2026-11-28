// Safe-Rust model of examples/expanded/games_4/checkers.cp
// No unsafe, no FFI, no external crates. Deterministic 8x8 checkers core.
// Build: rustc -O --edition 2021 checkers.rs -o checkers

use std::io::Read;

const SIZE: i32 = 8;
const MAX_MOVE_LEN: usize = 4;

const E_OVERSIZE: i32 = 1;
const E_RANGE: i32 = 2;
const E_ILLEGAL: i32 = 3;
const E_SYNTAX: i32 = 4;
const E_COUNT: i32 = 5;

const EMPTY: i32 = 0;
const RED_MAN: i32 = 1;
const RED_KING: i32 = 2;
const WHITE_MAN: i32 = 3;
const WHITE_KING: i32 = 4;
const PLAYER_RED: i32 = 1;
const PLAYER_WHITE: i32 = 2;

const DEFAULT_MOVES: &str = "2130 5041 1021 4132 2334 3223 0110 2301";

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
        "checkers: error code={} category={} message={}",
        errcode, category, detail
    );
}

// Parse one move token (4 digits) or the next 4-digit group of a compact run.
fn parse_move(bytes: &[u8], cursor: &mut usize) -> Result<[i32; 4], i32> {
    let n = bytes.len();
    let mut i = *cursor;
    while i < n && is_space(bytes[i] as char) {
        i += 1;
    }
    if i >= n {
        *cursor = i;
        return Err(E_COUNT);
    }
    let start = i;
    while i < n && !is_space(bytes[i] as char) {
        i += 1;
    }
    let mut len = i - start;
    if len > MAX_MOVE_LEN {
        if len % MAX_MOVE_LEN != 0 {
            return Err(E_OVERSIZE);
        }
        for &c in &bytes[start..start + len] {
            if c < b'0' || c > b'7' {
                return Err(E_RANGE);
            }
        }
        len = MAX_MOVE_LEN;
    } else if len != MAX_MOVE_LEN {
        return Err(E_SYNTAX);
    }
    let mut coords = [0i32; 4];
    for k in 0..4 {
        let ch = bytes[start + k];
        if ch < b'0' || ch > b'7' {
            return Err(E_RANGE);
        }
        coords[k] = (ch - b'0') as i32;
    }
    *cursor = start + MAX_MOVE_LEN;
    Ok(coords)
}

fn parse_moves(text: &str, out: &mut Vec<i32>) -> Result<(), i32> {
    let bytes = text.as_bytes();
    let mut i = 0usize;
    let mut parsed = 0usize;
    loop {
        let coords = match parse_move(bytes, &mut i) {
            Ok(v) => v,
            Err(E_COUNT) => break,
            Err(rc) => return Err(rc),
        };
        if parsed >= 64 {
            return Err(E_COUNT);
        }
        out.extend_from_slice(&coords);
        parsed += 1;
    }
    if parsed == 0 {
        return Err(E_COUNT);
    }
    Ok(())
}

struct Checkers {
    cells: [[i32; SIZE as usize]; SIZE as usize],
    current: i32,
    red_count: i32,
    white_count: i32,
    ply: i32,
}

impl Checkers {
    fn new() -> Self {
        let mut b = Self {
            cells: [[EMPTY; SIZE as usize]; SIZE as usize],
            current: PLAYER_RED,
            red_count: 0,
            white_count: 0,
            ply: 0,
        };
        for r in 0..SIZE {
            for c in 0..SIZE {
                if ((r + c) & 1) == 1 {
                    if r < 3 {
                        b.cells[r as usize][c as usize] = RED_MAN;
                        b.red_count += 1;
                    } else if r > 4 {
                        b.cells[r as usize][c as usize] = WHITE_MAN;
                        b.white_count += 1;
                    }
                }
            }
        }
        b
    }

    fn tile(&self, r: i32, c: i32) -> i32 {
        if r < 0 || r >= SIZE || c < 0 || c >= SIZE {
            return -1;
        }
        self.cells[r as usize][c as usize]
    }

    fn is_king(piece: i32) -> bool {
        piece == RED_KING || piece == WHITE_KING
    }

    fn owner(piece: i32) -> i32 {
        match piece {
            RED_MAN | RED_KING => PLAYER_RED,
            WHITE_MAN | WHITE_KING => PLAYER_WHITE,
            _ => 0,
        }
    }

    fn dark(r: i32, c: i32) -> bool {
        r >= 0 && r < SIZE && c >= 0 && c < SIZE && ((r + c) & 1) == 1
    }

    fn forward(piece: i32, dr: i32) -> bool {
        if Self::is_king(piece) {
            dr == 1 || dr == -1
        } else if piece == RED_MAN {
            dr == 1
        } else {
            dr == -1
        }
    }

    fn apply(&mut self, fr: i32, fc: i32, tr: i32, tc: i32) -> Result<(i32, i32), i32> {
        if !Self::dark(fr, fc) || !Self::dark(tr, tc) {
            return Err(E_ILLEGAL);
        }
        let piece = self.tile(fr, fc);
        if piece < 0 || Self::owner(piece) != self.current {
            return Err(E_ILLEGAL);
        }
        if self.tile(tr, tc) != EMPTY {
            return Err(E_ILLEGAL);
        }
        let dr = tr - fr;
        let dc = tc - fc;
        let adr = dr.abs();
        let adc = dc.abs();
        if adr != 1 && adr != 2 {
            return Err(E_ILLEGAL);
        }
        if adc != adr {
            return Err(E_ILLEGAL);
        }
        if !Self::forward(piece, if dr < 0 { -1 } else { 1 }) {
            return Err(E_ILLEGAL);
        }
        let mut captured = 0;
        if adr == 2 {
            let mr = fr + dr / 2;
            let mc = fc + dc / 2;
            let mid = self.tile(mr, mc);
            if mid <= 0 || Self::owner(mid) == self.current {
                return Err(E_ILLEGAL);
            }
            self.cells[mr as usize][mc as usize] = EMPTY;
            if Self::owner(mid) == PLAYER_RED {
                self.red_count -= 1;
            } else {
                self.white_count -= 1;
            }
            captured = 1;
        }
        self.cells[fr as usize][fc as usize] = EMPTY;
        self.cells[tr as usize][tc as usize] = piece;
        let mut promoted = 0;
        if (piece == RED_MAN && tr == SIZE - 1) || (piece == WHITE_MAN && tr == 0) {
            self.cells[tr as usize][tc as usize] = piece + 1;
            promoted = 1;
        }
        self.ply += 1;
        Ok((captured, promoted))
    }

    fn has_move(&self, player: i32) -> bool {
        for r in 0..SIZE {
            for c in 0..SIZE {
                let piece = self.tile(r, c);
                if piece <= 0 || Self::owner(piece) != player {
                    continue;
                }
                let mut dirs = Vec::new();
                if Self::is_king(piece) || piece == RED_MAN {
                    dirs.push(1);
                }
                if Self::is_king(piece) || piece == WHITE_MAN {
                    dirs.push(-1);
                }
                for dr in dirs {
                    for dc in [-1, 1] {
                        let mut nr = r + dr;
                        let mut nc = c + dc;
                        if Self::dark(nr, nc) && self.tile(nr, nc) == EMPTY {
                            return true;
                        }
                        nr = r + 2 * dr;
                        nc = c + 2 * dc;
                        if Self::dark(nr, nc) && self.tile(nr, nc) == EMPTY {
                            let mid = self.tile(r + dr, c + dc);
                            if mid > 0 && Self::owner(mid) != player {
                                return true;
                            }
                        }
                    }
                }
            }
        }
        false
    }

    fn next_turn(&mut self) {
        self.current = if self.current == PLAYER_RED {
            PLAYER_WHITE
        } else {
            PLAYER_RED
        };
    }

    fn render(&self) {
        print!("  ");
        for c in 0..SIZE {
            print!("{}", c);
        }
        println!();
        for r in 0..SIZE {
            print!("{} ", r);
            for c in 0..SIZE {
                let v = self.tile(r, c);
                let ch = if ((r + c) & 1) == 0 {
                    ' '
                } else {
                    match v {
                        RED_MAN => 'r',
                        RED_KING => 'R',
                        WHITE_MAN => 'w',
                        WHITE_KING => 'W',
                        x if x < 0 => '?',
                        _ => '.',
                    }
                };
                print!("{}", ch);
            }
            println!();
        }
    }
}

fn side_name(side: i32) -> &'static str {
    if side == PLAYER_RED {
        "red"
    } else {
        "white"
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
            let line = buf.lines().next().unwrap_or("").to_string();
            if let Err(rc) = parse_moves(&line, &mut moves) {
                refuse(rc, "invalid stdin move sequence", 0);
                std::process::exit(2);
            }
        } else {
            for (idx, arg) in args.iter().skip(1).enumerate() {
                let step = (idx + 1) as i32;
                if let Err(rc) = parse_moves(arg, &mut moves) {
                    refuse(rc, "invalid move token", step);
                    std::process::exit(2);
                }
            }
        }
    } else if let Err(rc) = parse_moves(DEFAULT_MOVES, &mut moves) {
        refuse(rc, "internal default sequence is invalid", 0);
        std::process::exit(2);
    }

    if moves.len() % 4 != 0 {
        refuse(E_SYNTAX, "move sequence is not a whole number of moves", 0);
        std::process::exit(2);
    }

    println!("CHECKERS size={} moves={}", SIZE, moves.len() / 4);
    println!("INITIAL");
    let mut board = Checkers::new();
    board.render();

    let mut step = 0usize;
    let mut result_name = "incomplete";
    let mut result_detail = "none";

    for chunk in moves.chunks_exact(4) {
        step += 1;
        let fr = chunk[0];
        let fc = chunk[1];
        let tr = chunk[2];
        let tc = chunk[3];
        let mover = board.current;
        let (captured, promoted) = match board.apply(fr, fc, tr, tc) {
            Ok(v) => v,
            Err(E_RANGE) => {
                refuse(E_RANGE, "coordinate outside 0..7", step as i32);
                std::process::exit(2);
            }
            Err(_) => {
                refuse(E_ILLEGAL, "illegal checkers move", step as i32);
                std::process::exit(2);
            }
        };
        println!(
            "STEP {} side={} from=({},{}) to=({},{}) capture={} promote={}",
            step,
            side_name(mover),
            fr,
            fc,
            tr,
            tc,
            captured,
            promoted
        );
        board.render();

        if mover == PLAYER_RED {
            if board.white_count == 0 {
                result_name = "win";
                result_detail = "red:white-pieces-exhausted";
                break;
            }
            if !board.has_move(PLAYER_WHITE) {
                result_name = "win";
                result_detail = "red:white-has-no-move";
                break;
            }
        } else {
            if board.red_count == 0 {
                result_name = "win";
                result_detail = "white:red-pieces-exhausted";
                break;
            }
            if !board.has_move(PLAYER_RED) {
                result_name = "win";
                result_detail = "white:red-has-no-move";
                break;
            }
        }
        board.next_turn();
    }

    println!("RESULT {} detail={} step={}", result_name, result_detail, step);
    println!(
        "FINAL result={} plies={} moves={} side={} red={} white={}",
        result_name,
        board.ply,
        moves.len() / 4,
        side_name(board.current),
        board.red_count,
        board.white_count
    );
}
