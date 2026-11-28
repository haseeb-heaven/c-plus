// Safe-Rust model of examples/expanded/math_2/base64.cp
// No unsafe, no FFI, no external crates. Build: rustc -O --edition 2021 base64.rs -o base64
// RFC 4648 base64 with bounded input and byte-exact roundtrip. C+ `print()`
// joins args with one space and appends a newline.

const CAP: usize = 96;
const ENC_CAP: usize = 128;
const ALPHA: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

type Fail = (&'static str, i32); // (reason, C+ exit code)

fn cprint(parts: &[String]) {
    println!("{}", parts.join(" "));
}

fn refused(f: Fail) -> i32 {
    cprint(&[
        "BASE64 REFUSED code=".to_string(),
        f.1.to_string(),
        f.0.to_string(),
    ]);
    1
}

fn b64_val(c: u8) -> i32 {
    if c >= b'A' && c <= b'Z' { (c - b'A') as i32 }
    else if c >= b'a' && c <= b'z' { (c - b'a') as i32 + 26 }
    else if c >= b'0' && c <= b'9' { (c - b'0') as i32 + 52 }
    else if c == b'+' { 62 }
    else if c == b'/' { 63 }
    else { -1 }
}

fn encode(input: &[u8]) -> Result<Vec<u8>, Fail> {
    let n = input.len();
    let mut out: Vec<u8> = Vec::new();
    let mut i = 0usize;
    while i < n {
        let b0 = input[i] as u32;
        let b1 = if i + 1 < n { input[i + 1] as u32 } else { 0 };
        let b2 = if i + 2 < n { input[i + 2] as u32 } else { 0 };
        let t = (b0 << 16) | (b1 << 8) | b2;
        if out.len() + 4 > ENC_CAP + 2 {
            return Err(("encoded-too-long", 101));
        }
        out.push(ALPHA[((t >> 18) & 0x3f) as usize]);
        out.push(ALPHA[((t >> 12) & 0x3f) as usize]);
        out.push(if i + 1 < n { ALPHA[((t >> 6) & 0x3f) as usize] } else { b'=' });
        out.push(if i + 2 < n { ALPHA[(t & 0x3f) as usize] } else { b'=' });
        i += 3;
    }
    Ok(out)
}

fn decode(input: &[u8], out_cap: usize) -> Result<Vec<u8>, Fail> {
    let n = input.len();
    if n == 0 {
        return Ok(Vec::new());
    }
    if (n & 3) != 0 {
        return Err(("malformed-length", 108));
    }
    if n > ENC_CAP {
        return Err(("encoded-too-long", 101));
    }
    let mut out: Vec<u8> = Vec::new();
    let mut i = 0usize;
    while i < n {
        let c2 = input[i + 2];
        let c3 = input[i + 3];
        let is_last = i + 4 == n;
        let pad2 = c2 == b'=';
        let pad3 = c3 == b'=';
        let v0 = b64_val(input[i]);
        let v1 = b64_val(input[i + 1]);
        if v0 < 0 || v1 < 0 {
            return Err(("malformed-encoding", 108));
        }
        if (pad2 && (!is_last || !pad3)) || (pad3 && !is_last) {
            return Err(("malformed-padding", 108));
        }
        let mut t = ((v0 as u32) << 18) | ((v1 as u32) << 12);
        if !pad2 {
            let v2 = b64_val(c2);
            if v2 < 0 {
                return Err(("malformed-encoding", 108));
            }
            t |= (v2 as u32) << 6;
        }
        if !pad3 {
            let v3 = b64_val(c3);
            if v3 < 0 {
                return Err(("malformed-encoding", 108));
            }
            t |= v3 as u32;
        }
        if out.len() >= out_cap { return Err(("decoded-too-long", 101)); }
        out.push(((t >> 16) & 0xff) as u8);
        if !pad2 {
            if out.len() >= out_cap { return Err(("decoded-too-long", 101)); }
            out.push(((t >> 8) & 0xff) as u8);
        }
        if !pad2 && !pad3 {
            if out.len() >= out_cap { return Err(("decoded-too-long", 101)); }
            out.push((t & 0xff) as u8);
        }
        if !is_last && (pad2 || pad3) {
            return Err(("malformed-padding", 108));
        }
        i += 4;
    }
    Ok(out)
}

fn hex(bytes: &[u8]) -> String {
    const H: &[u8; 16] = b"0123456789abcdef";
    let mut s = String::with_capacity(bytes.len() * 2);
    for &b in bytes {
        s.push(H[(b >> 4) as usize] as char);
        s.push(H[(b & 0x0f) as usize] as char);
    }
    s
}

// Encode, decode, roundtrip-check. Prints the transcript line.
fn b64_case(plain: &str) -> Result<(), Fail> {
    let n = plain.as_bytes().len();
    if n > CAP {
        return Err(("input-too-long", 101));
    }
    let enc = encode(plain.as_bytes())?;
    let back = decode(&enc, CAP + 2)?;
    if back.len() != n {
        return Err(("roundtrip-length", 108));
    }
    if back != plain.as_bytes() {
        return Err(("roundtrip-mismatch", 108));
    }
    cprint(&[format!(
        "BASE64 enc \"{}\" -> {} rt=OK len={}",
        plain,
        String::from_utf8_lossy(&enc),
        n
    )]);
    Ok(())
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() == 1 {
        let inputs = ["", "f", "fo", "foo", "foob", "fooba", "foobar"];
        for t in &inputs {
            if let Err(f) = b64_case(t) {
                std::process::exit(refused(f));
            }
        }
        cprint(&["BASE64 OK count=7 cap=96 n=".to_string(), "7".to_string()]);
        return;
    }

    let result: Result<(), Fail> = if args.len() == 2 {
        let mode = &args[1];
        if mode.as_bytes().len() > 24 || mode.bytes().any(|b| b == 0) {
            // C+ prints this variant as a single literal (no injected spaces).
            cprint(&["BASE64 REFUSED code=108 bad-mode".to_string()]);
            std::process::exit(1);
        } else if mode == "oversized" {
            b64_case(&"x".repeat(CAP + 1))
        } else if mode == "encoversized" {
            decode(&vec![b'A'; ENC_CAP + 4], CAP + 2).map(|_| {})
        } else {
            Err(("bad-mode", 108))
        }
    } else if args.len() == 3 {
        let mode = &args[1];
        let val = &args[2];
        if mode == "enc" {
            b64_case(val)
        } else if mode == "dec" {
            match decode(val.as_bytes(), CAP + 2) {
                Ok(bytes) => {
                    cprint(&[format!(
                        "BASE64 dec {} len={} hex={}",
                        val, bytes.len(), hex(&bytes)
                    )]);
                    Ok(())
                }
                Err(f) => Err(f),
            }
        } else if mode == "malformed" || mode == "badlen" {
            decode(val.as_bytes(), CAP + 2).map(|_| {})
        } else {
            Err(("bad-mode", 108))
        }
    } else {
        Err(("usage", 108))
    };

    match result {
        Ok(()) => {}
        Err(f) => std::process::exit(refused(f)),
    }
}
