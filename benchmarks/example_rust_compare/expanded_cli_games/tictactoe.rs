// Safe-Rust model of examples/expanded/games/cores/tictactoe.cp
// No unsafe, no FFI. Deterministic 3x3 tic-tac-toe logic core with CLI driver.
// Build: rustc -O --edition 2021 tictactoe.rs -o tictactoe

use std::io::{self, Read};

const X_PLAN: [usize; 5] = [0, 4, 8, 2, 6];
const O_PLAN: [usize; 4] = [1, 5, 3, 7];
const LINES: [[usize; 3]; 8] = [
    [0, 1, 2], [3, 4, 5], [6, 7, 8],
    [0, 3, 6], [1, 4, 7], [2, 5, 8],
    [0, 4, 8], [2, 4, 6],
];
const LINE_NAMES: [&str; 8] = [
    "row0", "row1", "row2", "col0", "col1", "col2", "diag-main", "diag-anti",
];

macro_rules! cprint {
    () => { println!() };
    ($($arg:expr),+ $(,)?) => {
        println!("{}", [$($arg.to_string()),+].join(" "))
    };
}

fn mark(player: i32) -> &'static str {
    if player == 1 { "X" } else { "O" }
}

fn parse_moves(text: &str, out: &mut Vec<usize>) -> bool {
    for ch in text.chars() {
        if ch == '\n' || ch == '\r' || ch == ' ' || ch == '\t' { continue; }
        if !('0'..='8').contains(&ch) { return false; }
        out.push((ch as u8 - b'0') as usize);
    }
    true
}

struct Board {
    cells: [i32; 9],
    played: i32,
}

impl Board {
    fn new() -> Self { Self { cells: [0; 9], played: 0 } }

    fn place(&mut self, index: usize, player: i32) -> bool {
        if index >= 9 || self.cells[index] != 0 { return false; }
        self.cells[index] = player;
        self.played += 1;
        true
    }

    fn winner_line(&self) -> Option<usize> {
        for (l, line) in LINES.iter().enumerate() {
            let a = self.cells[line[0]];
            if a != 0 && a == self.cells[line[1]] && a == self.cells[line[2]] {
                return Some(l);
            }
        }
        None
    }

    fn draw(&self) -> bool {
        self.played == 9 && self.winner_line().is_none()
    }

    fn render(&self) {
        for r in 0..3 {
            print!("  ");
            for c in 0..3 {
                if c != 0 { print!(" | "); }
                let v = self.cells[r * 3 + c];
                print!("{}", if v == 0 { '.' } else if v == 1 { 'X' } else { 'O' });
            }
            println!();
            if r != 2 { println!("  --+---+--"); }
        }
    }
}

fn fail(message: &str) {
    eprintln!("tictactoe: {}", message);
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let mut moves: Vec<usize> = Vec::new();

    if args.len() > 1 {
        if args[1] == "-" {
            if args.len() != 2 { fail("' - ' must be the only argument when reading stdin"); std::process::exit(2); }
            let mut buf = String::new();
            if io::stdin().read_to_string(&mut buf).is_err() || !parse_moves(&buf, &mut moves) {
                fail("invalid cell index in stdin input (expected 0-8)");
                std::process::exit(2);
            }
        } else {
            for a in args.iter().skip(1) {
                if !parse_moves(a, &mut moves) {
                    fail("invalid cell token (expected digits 0-8)");
                    std::process::exit(2);
                }
            }
        }
    } else {
        for i in 0..9 {
            moves.push(if i % 2 == 0 { X_PLAN[i >> 1] } else { O_PLAN[i >> 1] });
        }
    }
    if moves.is_empty() { fail("empty move sequence"); std::process::exit(2); }
    if moves.len() > 9 { fail("too many moves (board has only 9 cells)"); std::process::exit(2); }

    cprint!("TICTACTOE players=X:fixed,O:fixed moves=", moves.len());
    cprint!("INITIAL");
    let mut board = Board::new();
    board.render();

    let mut end_step = 0;
    let mut result = "incomplete";
    let mut detail = "none";

    for (i, &idx) in moves.iter().enumerate() {
        let player = if i % 2 == 0 { 1 } else { 2 };
        if !board.place(idx, player) {
            println!("ILLEGAL step={} player={} cell={}", i + 1, mark(player), idx);
            fail("illegal move: cell out of range or already occupied");
            std::process::exit(2);
        }
        println!("STEP {} {} cell={} pos=({},{})",
                 i + 1, mark(player), idx, idx / 3, idx % 3);
        board.render();

        if let Some(line) = board.winner_line() {
            result = "win";
            detail = LINE_NAMES[line];
            end_step = i + 1;
            break;
        }
        if board.draw() {
            result = "draw";
            detail = "board-full";
            end_step = i + 1;
            break;
        }
    }

    println!("RESULT {} detail={} step={}", result, detail, end_step);
    println!("FINAL result={} moves_played={} x={} o={}",
             result, board.played, (board.played + 1) >> 1, board.played >> 1);
}
