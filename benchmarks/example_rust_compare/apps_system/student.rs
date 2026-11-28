//! Safe-Rust behavioral model of examples/apps/student.cp.
//!
//! No `unsafe`, no FFI. Models the observable CRUD/menu behavior of the C+
//! original: prompts, validation, in-memory records, line-based students.db
//! persistence, and all printed output. Compiled with `rustc -O`.

use std::env;
use std::fs;
use std::io::{self, BufRead, Write};
use std::path::PathBuf;

const DATA_FILE: &str = "students.db";
const MAX_STUDENTS: usize = 100;
const INT_MAX: i64 = i32::MAX as i64;

#[derive(Clone, Debug)]
struct Student {
    id: i64,
    name: String,
    age: i64,
    grade: f64,
}

/// printf("%.*g") approximation used by the C+ `print()` runtime (default
/// precision 6) and by save()'s "%.17g". Sufficient for all record values
/// produced by this application.
fn fmt_g(x: f64, precision: usize) -> String {
    if x == 0.0 {
        return "0".to_string();
    }
    if x.is_nan() {
        return "nan".to_string();
    }
    if x.is_infinite() {
        return if x < 0.0 { "-inf" } else { "inf" }.to_string();
    }
    let neg = x < 0.0;
    let a = x.abs();
    let exp = a.log10().floor() as i32;
    let use_exp = exp < -4 || exp >= precision as i32;
    let mut s = if use_exp {
        let mantissa = format!("{:.*e}", precision - 1, a);
        let (m, e) = mantissa.split_once('e').unwrap();
        let mut m = m.trim_end_matches('0').trim_end_matches('.').to_string();
        if m.is_empty() {
            m = "0".to_string();
        }
        let e: i32 = e.parse().unwrap_or(0);
        format!(
            "{}e{}{:02}",
            m,
            if e >= 0 { '+' } else { '-' },
            e.abs()
        )
    } else {
        let decimals = (precision as i32 - 1 - exp).max(0) as usize;
        let mut v = format!("{:.*}", decimals, a);
        if v.contains('.') {
            v = v.trim_end_matches('0').trim_end_matches('.').to_string();
        }
        v
    };
    if neg {
        s.insert(0, '-');
    }
    s
}

struct Input {
    reader: io::BufReader<io::Stdin>,
}

impl Input {
    fn new() -> Self {
        Self {
            reader: io::BufReader::new(io::stdin()),
        }
    }

    /// Mirrors read_line(): print prompt, fgets(size), strip newline, flush
    /// any remainder of an over-long line, then trim leading/trailing space.
    fn read_line(&mut self, prompt: &str, size: usize) -> Option<String> {
        print!("{}", prompt);
        let _ = io::stdout().flush();
        let mut raw = String::new();
        let n = self.reader.read_line(&mut raw).ok()?;
        if n == 0 {
            return None;
        }
        // fgets() reads at most size-1 bytes.
        let mut buf: String = raw.chars().take(size.saturating_sub(1)).collect();
        if let Some(pos) = buf.find('\n') {
            buf.truncate(pos);
        } else if raw.ends_with('\n') {
            // line was longer than the buffer: discard the rest (getchar loop)
            let mut sink = String::new();
            let _ = self.reader.read_line(&mut sink);
        }
        Some(buf.trim().to_string())
    }

    fn read_int(&mut self, prompt: &str, min: i64, max: i64) -> Option<i64> {
        loop {
            let line = self.read_line(prompt, 80)?;
            match line.parse::<i64>() {
                Ok(v) if v >= min && v <= max => return Some(v),
                _ => println!("Enter an integer from {} to {}", min, max),
            }
        }
    }

    fn read_grade(&mut self, prompt: &str) -> Option<f64> {
        loop {
            let line = self.read_line(prompt, 80)?;
            match line.parse::<f64>() {
                Ok(v) if (0.0..=100.0).contains(&v) => return Some(v),
                _ => println!("Enter a grade from 0 to 100."),
            }
        }
    }
}

struct StudentDb {
    rows: Vec<Student>,
    data_path: PathBuf,
}

impl StudentDb {
    fn new(data_path: PathBuf) -> Self {
        Self {
            rows: Vec::new(),
            data_path,
        }
    }

    fn find(&self, id: i64) -> Option<usize> {
        self.rows.iter().position(|s| s.id == id)
    }

    fn list(&self) {
        println!("\nID       NAME                            AGE    GRADE");
        println!("----------------------------------------------------");
        for s in &self.rows {
            println!(
                "{:<8} {:<31} {:<6} {:>5.1}",
                s.id, s.name, s.age, s.grade
            );
        }
        println!("Total students: {}", self.rows.len());
    }

    fn show(&self, input: &mut Input) {
        let id = match input.read_int("Student ID: ", 1, INT_MAX) {
            Some(v) => v,
            None => return,
        };
        let index = match self.find(id) {
            Some(i) => i,
            None => {
                println!("Student not found.");
                return;
            }
        };
        let s = &self.rows[index];
        println!("\nStudent ID: {}", s.id);
        println!("Name: {}", s.name);
        println!("Age: {}", s.age);
        println!("Grade: {}", fmt_g(s.grade, 6));
    }

    fn add(&mut self, input: &mut Input) {
        if self.rows.len() >= MAX_STUDENTS {
            println!("Database is full.");
            return;
        }
        let id = match input.read_int("Student ID: ", 1, INT_MAX) {
            Some(v) => v,
            None => return,
        };
        if self.find(id).is_some() {
            println!("That ID already exists.");
            return;
        }
        let name = match input.read_line("Name: ", 64) {
            Some(v) => v,
            None => return,
        };
        if name.is_empty() || name.contains('|') {
            println!("Name must not be empty or contain '|'.");
            return;
        }
        let age = match input.read_int("Age (1-120): ", 1, 120) {
            Some(v) => v,
            None => return,
        };
        let grade = match input.read_grade("Grade (0-100): ") {
            Some(v) => v,
            None => return,
        };
        self.rows.push(Student {
            id,
            name,
            age,
            grade,
        });
        println!("Student created.");
    }

    fn update(&mut self, input: &mut Input) {
        let id = match input.read_int("Student ID to update: ", 1, INT_MAX) {
            Some(v) => v,
            None => return,
        };
        let index = match self.find(id) {
            Some(i) => i,
            None => {
                println!("Student not found.");
                return;
            }
        };
        let name = match input.read_line("New name: ", 64) {
            Some(v) => v,
            None => return,
        };
        if name.is_empty() || name.contains('|') {
            println!("Invalid name; record unchanged.");
            return;
        }
        let age = match input.read_int("New age (1-120): ", 1, 120) {
            Some(v) => v,
            None => return,
        };
        let grade = match input.read_grade("New grade (0-100): ") {
            Some(v) => v,
            None => return,
        };
        self.rows[index].name = name;
        self.rows[index].age = age;
        self.rows[index].grade = grade;
        println!("Student updated.");
    }

    fn remove(&mut self, id: i64) {
        let index = match self.find(id) {
            Some(i) => i,
            None => {
                println!("Student not found.");
                return;
            }
        };
        self.rows.remove(index);
        println!("Student deleted.");
    }

    fn load(&mut self) {
        let text = match fs::read_to_string(&self.data_path) {
            Ok(t) => t,
            Err(_) => return,
        };
        for line in text.lines() {
            if self.rows.len() >= MAX_STUDENTS {
                break;
            }
            let parts: Vec<&str> = line.splitn(4, '|').collect();
            if parts.len() != 4 {
                continue;
            }
            let id = match parts[0].parse::<i64>() {
                Ok(v) => v,
                Err(_) => continue,
            };
            let name = &parts[1];
            if name.len() > 63 {
                continue;
            }
            let age = match parts[2].parse::<i64>() {
                Ok(v) => v,
                Err(_) => continue,
            };
            let grade = match parts[3].parse::<f64>() {
                Ok(v) => v,
                Err(_) => continue,
            };
            if id < 1 || !(1..=120).contains(&age) || !(0.0..=100.0).contains(&grade) {
                continue;
            }
            if self.find(id).is_some() {
                continue;
            }
            self.rows.push(Student {
                id,
                name: name.to_string(),
                age,
                grade,
            });
        }
    }

    fn save(&self) -> bool {
        let tmp = self.data_path.with_extension("db.tmp");
        let mut out = String::new();
        for s in &self.rows {
            out.push_str(&format!(
                "{}|{}|{}|{}\n",
                s.id,
                s.name,
                s.age,
                fmt_g(s.grade, 17)
            ));
        }
        if fs::write(&tmp, out).is_err() {
            eprintln!("Saving database: cannot write temporary file");
            return false;
        }
        if fs::rename(&tmp, &self.data_path).is_err() {
            eprintln!("Replacing database: cannot rename temporary file");
            return false;
        }
        true
    }
}

fn main() {
    let data_path = env::current_dir()
        .unwrap_or_else(|_| PathBuf::from("."))
        .join(DATA_FILE);
    let mut db = StudentDb::new(data_path);
    db.load();
    let mut input = Input::new();

    loop {
        println!("\n========== STUDENT RECORDS | C+ ==========");
        println!("1. Add student");
        println!("2. List students");
        println!("3. Find student");
        println!("4. Update student");
        println!("5. Delete student");
        println!("0. Save and exit");
        let choice = match input.read_int("Choose: ", 0, 5) {
            Some(v) => v as i32,
            None => break,
        };
        if choice == 0 {
            break;
        }
        match choice {
            1 => db.add(&mut input),
            2 => db.list(),
            3 => db.show(&mut input),
            4 => db.update(&mut input),
            5 => {
                if let Some(id) = input.read_int("Student ID to delete: ", 1, INT_MAX) {
                    db.remove(id);
                }
            }
            _ => {}
        }
        if matches!(choice, 1 | 4 | 5) && !db.save() {
            println!("Warning: changes not saved.");
        }
    }
    if !db.save() {
        std::process::exit(1);
    }
    println!("Records saved. Goodbye!");
}
