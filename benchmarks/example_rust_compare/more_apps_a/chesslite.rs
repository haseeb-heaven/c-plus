// Safe-Rust model of examples/expanded/games_4/chesslite.cp
// No unsafe, no FFI, no external crates. Deterministic chess-subset core.
// Build: rustc -O --edition 2021 chesslite.rs -o chesslite

use std::io::Read;

const SIZE: i32 = 8;
const MAX_MOVE_LEN: usize = 4;

const E_OVERSIZE: i32 = 1;
const E_RANGE: i32 = 2;
const E_ILLEGAL: i32 = 3;
const E_SYNTAX: i32 = 4;
const E_COUNT: i32 = 5;

const EMPTY: i32 = 0;
const W_PAWN: i32 = 1;
const W_ROOK: i32 = 2;
const W_KNIGHT: i32 = 3;
const B_PAWN: i32 = 4;
const B_ROOK: i32 = 5;
const B_KNIGHT: i32 = 6;
const PLAYER_WHITE: i32 = 1;
const PLAYER_BLACK: i32 = 2;

const DEFAULT_MOVES: &str = "0120 6343 1323 6151 0625 6757 2544 6656 1434 4334";

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
        "chesslite: error code={} category={} message={}",
        errcode, category, detail
    );
}

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

struct ChessLite {
    cells: [[i32; SIZE as usize]; SIZE as usize],
    current: i32,
    white_count: i32,
    black_count: i32,
    ply: i32,
    captures: i32,
}

impl ChessLite {
    fn new() -> Self {
        let mut b = Self {
            cells: [[EMPTY; SIZE as usize]; SIZE as usize],
            current: PLAYER_WHITE,
            white_count: 16,
            black_count: 16,
            ply: 0,
            captures: 0,
        };
        b.cells[0][0] = W_ROOK;
        b.cells[0][1] = W_KNIGHT;
        b.cells[0][6] = W_KNIGHT;
        b.cells[0][7] = W_ROOK;
        b.cells[7][0] = B_ROOK;
        b.cells[7][1] = B_KNIGHT;
        b.cells[7][6] = B_KNIGHT;
        b.cells[7][7] = B_ROOK;
        for c in 0..SIZE as usize {
            b.cells[1][c] = W_PAWN;
            b.cells[6][c] = B_PAWN;
        }
        b
    }

    fn tile(&self, r: i32, c: i32) -> i32 {
        if r < 0 || r >= SIZE || c < 0 || c >= SIZE {
            return -1;
        }
        self.cells[r as usize][c as usize]
    }

    fn owner(piece: i32) -> i32 {
        match piece {
            W_PAWN | W_ROOK | W_KNIGHT => PLAYER_WHITE,
            B_PAWN | B_ROOK | B_KNIGHT => PLAYER_BLACK,
            _ => 0,
        }
    }

    fn probe(&self, player: i32, fr: i32, fc: i32, tr: i32, tc: i32) -> Result<i32, i32> {
        let piece = self.tile(fr, fc);
        let dest = self.tile(tr, tc);
        if piece <= 0 || Self::owner(piece) != player {
            return Err(E_ILLEGAL);
        }
        if dest > 0 && Self::owner(dest) == player {
            return Err(E_ILLEGAL);
        }
        let capture = if dest > 0 { 1 } else { 0 };
        let dr = tr - fr;
        let dc = tc - fc;
        let ar = dr.abs();
        let ac = dc.abs();

        if piece == W_PAWN || piece == B_PAWN {
            let forward = if piece == W_PAWN { 1 } else { -1 };
            let home = if piece == W_PAWN { 1 } else { 6 };
            if dr == forward && dc == 0 {
                return if dest == EMPTY { Ok(capture) } else { Err(E_ILLEGAL) };
            }
            if dr == forward && ac == 1 {
                return if capture != 0 { Ok(capture) } else { Err(E_ILLEGAL) };
            }
            if dr == 2 * forward
                && dc == 0
                && fr == home
                && self.tile(fr + forward, fc) == EMPTY
                && dest == EMPTY
            {
                return Ok(capture);
            }
            return Err(E_ILLEGAL);
        }
        if piece == W_ROOK || piece == B_ROOK {
            if !((dr == 0 && dc != 0) || (dc == 0 && dr != 0)) {
                return Err(E_ILLEGAL);
            }
            let sr = if dr > 0 { 1 } else { -1 };
            let sc = if dc > 0 { 1 } else { -1 };
            let mut r = fr + sr;
            let mut c = fc + sc;
            let mut steps = 0;
            while r != tr || c != tc {
                if steps >= SIZE || self.tile(r, c) != EMPTY {
                    return Err(E_ILLEGAL);
                }
                r += sr;
                c += sc;
                steps += 1;
            }
            return Ok(capture);
        }
        if piece == W_KNIGHT || piece == B_KNIGHT {
            if (ar == 1 && ac == 2) || (ar == 2 && ac == 1) {
                return Ok(capture);
            }
            return Err(E_ILLEGAL);
        }
        Err(E_ILLEGAL)
    }

    fn apply(&mut self, fr: i32, fc: i32, tr: i32, tc: i32) -> Result<(i32, i32), i32> {
        if fr < 0 || fr >= SIZE || fc < 0 || fc >= SIZE || tr < 0 || tr >= SIZE || tc < 0 || tc >= SIZE
        {
            return Err(E_RANGE);
        }
        let capture = self.probe(self.current, fr, fc, tr, tc)?;
        let piece = self.cells[fr as usize][fc as usize];
        if capture != 0 {
            let dest_owner = Self::owner(self.cells[tr as usize][tc as usize]);
            if dest_owner == PLAYER_WHITE {
                self.white_count -= 1;
            } else {
                self.black_count -= 1;
            }
            self.captures += 1;
        }
        self.cells[fr as usize][fc as usize] = EMPTY;
        self.cells[tr as usize][tc as usize] = piece;
        self.ply += 1;
        Ok((capture, piece))
    }

    fn has_move(&self, player: i32) -> bool {
        for fr in 0..SIZE {
            for fc in 0..SIZE {
                if Self::owner(self.tile(fr, fc)) != player {
                    continue;
                }
                for tr in 0..SIZE {
                    for tc in 0..SIZE {
                        if self.probe(player, fr, fc, tr, tc).is_ok() {
                            return true;
                        }
                    }
                }
            }
        }
        false
    }

    fn next_turn(&mut self) {
        self.current = if self.current == PLAYER_WHITE {
            PLAYER_BLACK
        } else {
            PLAYER_WHITE
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
                let ch = match v {
                    W_PAWN => 'P',
                    W_ROOK => 'R',
                    W_KNIGHT => 'N',
                    B_PAWN => 'p',
                    B_ROOK => 'r',
                    B_KNIGHT => 'n',
                    x if x < 0 => '?',
                    _ => '.',
                };
                print!("{}", ch);
            }
            println!();
        }
    }
}

fn side_name(side: i32) -> &'static str {
    if side == PLAYER_WHITE {
        "white"
    } else {
        "black"
    }
}

fn piece_name(piece: i32) -> &'static str {
    match piece {
        W_PAWN => "P",
        W_ROOK => "R",
        W_KNIGHT => "N",
        B_PAWN => "p",
        B_ROOK => "r",
        _ => "n",
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

    println!(
        "CHESSLITE subset=pawn,rook,knight size={} moves={}",
        SIZE,
        moves.len() / 4
    );
    println!("INITIAL");
    let mut board = ChessLite::new();
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
        let (capture, mover) = match board.apply(fr, fc, tr, tc) {
            Ok(v) => v,
            Err(E_RANGE) => {
                refuse(E_RANGE, "coordinate outside 0..7", step as i32);
                std::process::exit(2);
            }
            Err(_) => {
                refuse(E_ILLEGAL, "illegal chesslite move", step as i32);
                std::process::exit(2);
            }
        };
        println!(
            "STEP {} side={} piece={} from=({},{}) to=({},{}) capture={}",
            step,
            side_name(board.current),
            piece_name(mover),
            fr,
            fc,
            tr,
            tc,
            capture
        );
        board.render();

        let opponent = if board.current == PLAYER_WHITE {
            PLAYER_BLACK
        } else {
            PLAYER_WHITE
        };
        if opponent == PLAYER_WHITE {
            if board.white_count == 0 {
                result_name = "win";
                result_detail = "black:white-pieces-exhausted";
                break;
            }
            if !board.has_move(PLAYER_WHITE) {
                result_name = "win";
                result_detail = "black:white-has-no-move";
                break;
            }
        } else {
            if board.black_count == 0 {
                result_name = "win";
                result_detail = "white:black-pieces-exhausted";
                break;
            }
            if !board.has_move(PLAYER_BLACK) {
                result_name = "win";
                result_detail = "white:black-has-no-move";
                break;
            }
        }
        board.next_turn();
    }

    println!("RESULT {} detail={} step={}", result_name, result_detail, step);
    println!(
        "FINAL result={} plies={} moves={} side={} white={} black={} captures={}",
        result_name,
        board.ply,
        moves.len() / 4,
        side_name(board.current),
        board.white_count,
        board.black_count,
        board.captures
    );
}
