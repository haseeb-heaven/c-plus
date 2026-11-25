// Safe-Rust model of examples/expanded/data_2/rle.cp
// No unsafe, no FFI, no external crates. Build: rustc -O --edition 2021 rle.rs -o rle
// Deterministic bounded run-length encode/decode with roundtrip; fixed argv.

const MAX_RAW: usize = 64;
const MAGIC: &str = "RLE1;";

type Fail = (&'static str, i32); // (reason, C+ exit code)

fn refuse(reason: &str, code: i32) -> i32 {
    println!("RLE error code={} reason={}", code, reason);
    2
}

fn ok_byte(c: u8) -> bool {
    c >= 0x21 && c <= 0x7e && c != b';'
}

// Returns (encoded string, group count).
fn encode(raw: &[u8]) -> Result<(String, usize), Fail> {
    let n = raw.len();
    if n == 0 || n > MAX_RAW {
        return Err(("raw-too-long", 101));
    }
    let mut out = String::from(MAGIC);
    let mut i = 0usize;
    let mut g = 0usize;
    while i < n {
        let b = raw[i];
        if !ok_byte(b) {
            return Err(("invalid-byte", 108));
        }
        let mut run = 1usize;
        while i + run < n && raw[i + run] == b {
            run += 1;
        }
        if run > 99 {
            return Err(("run-too-long", 101));
        }
        if g > 0 {
            out.push(';');
        }
        out.push(b as char);
        out.push((b'0' + (run / 10) as u8) as char);
        out.push((b'0' + (run % 10) as u8) as char);
        g += 1;
        i += run;
    }
    Ok((out, g))
}

// Returns (decoded bytes, groups as (byte, run)). out_cap = MAX_RAW + 1.
fn decode(encoded: &[u8]) -> Result<(Vec<u8>, Vec<(u8, i32)>), Fail> {
    let out_cap = MAX_RAW + 1;
    let n = encoded.len();
    let magic = MAGIC.as_bytes();
    if n < magic.len() + 3 {
        return Err(("bad-magic", 108));
    }
    for (k, &mc) in magic.iter().enumerate() {
        if encoded[k] != mc {
            return Err(("bad-magic", 108));
        }
    }
    let mut out: Vec<u8> = Vec::new();
    let mut groups: Vec<(u8, i32)> = Vec::new();
    let mut i = magic.len();
    while i < n {
        let b = encoded[i];
        if !ok_byte(b) {
            return Err(("invalid-byte", 108));
        }
        if i + 2 >= n {
            return Err(("malformed-stream", 108));
        }
        let d1 = encoded[i + 1] as i32 - '0' as i32;
        let d2 = encoded[i + 2] as i32 - '0' as i32;
        if d1 < 0 || d1 > 9 || d2 < 0 || d2 > 9 {
            return Err(("malformed-stream", 108));
        }
        let run = d1 * 10 + d2;
        if run < 1 || run > 99 {
            return Err(("malformed-stream", 108));
        }
        if out.len() + run as usize > out_cap - 1 {
            return Err(("decoded-overflow", 101));
        }
        for _ in 0..run {
            out.push(b);
        }
        groups.push((b, run));
        i += 3;
        if i < n {
            if encoded[i] != b';' {
                return Err(("malformed-stream", 108));
            }
            i += 1;
            if i >= n {
                return Err(("malformed-stream", 108));
            }
        }
    }
    if out.is_empty() {
        return Err(("malformed-stream", 108));
    }
    Ok((out, groups))
}

fn print_groups(groups: &[(u8, i32)]) {
    for (i, &(byte, run)) in groups.iter().enumerate() {
        println!("RLE group {} byte={} run={}", i, byte as char, run);
    }
}

fn enc_refuse(f: Fail) -> i32 {
    refuse(f.0, f.1)
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() != 3 {
        std::process::exit(refuse("usage", 108));
    }
    let op = &args[1];
    let payload = &args[2];
    let is_enc = if op == "enc" {
        true
    } else if op == "dec" {
        false
    } else {
        std::process::exit(refuse("bad-op", 108));
    };

    println!("RLE app=rle op={} max_raw={} max_run=99 magic={}", if is_enc { "enc" } else { "dec" }, MAX_RAW, MAGIC);

    if is_enc {
        let raw = payload.as_bytes();
        println!("RLE in_len={} in={}", raw.len(), payload);
        let (encoded, gcount) = match encode(raw) {
            Ok(v) => v,
            Err(f) => std::process::exit(enc_refuse(f)),
        };
        let (decoded, groups) = match decode(encoded.as_bytes()) {
            Ok(v) => v,
            Err(f) => std::process::exit(refuse(f.0, f.1)),
        };
        print_groups(&groups);
        println!("RLE encoded={} groups={} enc_len={}", encoded, gcount, encoded.len());
        if decoded != raw {
            std::process::exit(refuse("roundtrip-failed", 108));
        }
        println!("RLE roundtrip ok=1 decoded_len={}", decoded.len());
    } else {
        println!("RLE enc={}", payload);
        let (decoded, groups) = match decode(payload.as_bytes()) {
            Ok(v) => v,
            Err(f) => std::process::exit(refuse(f.0, f.1)),
        };
        print_groups(&groups);
        let text = String::from_utf8_lossy(&decoded).into_owned();
        println!("RLE decoded_len={} decoded={}", decoded.len(), text);
        let (again, gcount) = match encode(&decoded) {
            Ok(v) => v,
            Err(f) => std::process::exit(enc_refuse(f)),
        };
        if again != *payload {
            std::process::exit(refuse("roundtrip-failed", 108));
        }
        println!("RLE roundtrip ok=1 groups={}", gcount);
    }
    println!("RLE done");
}
