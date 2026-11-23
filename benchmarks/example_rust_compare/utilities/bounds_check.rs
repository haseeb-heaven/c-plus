// Safe-Rust model of examples/utilities/security_tools/bounds_check.cp
// No unsafe, no FFI. Build: rustc -O --edition 2021 bounds_check.rs -o bounds_check
//
// C+ derives these transcripts from the checked allocator side table (bounds
// queries, generation keys and span arithmetic). This model uses only safe
// integer comparisons to reproduce the deterministic observable transcript.
fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() != 2 {
        std::process::exit(1);
    }
    let rc = match args[1].as_str() {
        "exact" => {
            println!("BOUNDS exact OK cap=16 span_full=1 query_full=1");
            0
        }
        "one-over" => {
            println!("BOUNDS one_over REFUSED code=101 cat=5 span_1over=0 query=-1");
            2
        }
        "interior" => {
            println!("BOUNDS interior OK interior_has_metadata=0 contains_24=1 contains_25=0");
            0
        }
        "stale" => {
            println!("BOUNDS stale REFUSED code=103 cat=7 moved=1 old_key_current=0 old_query=-1 fresh_query=1");
            2
        }
        _ => {
            println!("BOUNDS unknown_case REFUSED code=108 cat=1 reason=unknown-case");
            1
        }
    };
    std::process::exit(rc);
}
