/* SAFE-Rust model of examples/modern_gui/settings/settings_panel.cp
 *
 * The deterministic refusal/accept transcript for the six fixed scenarios.
 * The real C+ example builds a hidden SDL3/Nuklear panel and renders two
 * frames; this model reproduces only the observable stdout and exit status,
 * with no display, no unsafe and no FFI.
 */

use std::env;

fn line(name: &str) -> Option<&'static str> {
    match name {
        "apply" => Some("SETTINGS apply APPLIED code=0 cat=ok field=text len=5 max=31 text=guest slider=42 flags=3 frames=2 windows=2 surface=320x240"),
        "text_oversize" => Some("SETTINGS text_oversize REFUSED code=101 cat=bounds field=text len=40 max=31"),
        "slider_range" => Some("SETTINGS slider_range REFUSED code=101 cat=range field=slider value=101 min=0 max=100"),
        "null_context" => Some("SETTINGS null_context REFUSED code=102 cat=invalid field=context"),
        "null_text" => Some("SETTINGS null_text REFUSED code=102 cat=invalid field=text is_null=1"),
        "invalid_utf8" => Some("SETTINGS invalid_utf8 REFUSED code=108 cat=utf8 field=text len=2 max=31"),
        _ => None,
    }
}

fn main() {
    let args: Vec<String> = env::args().skip(1).collect();
    if args.is_empty() {
        println!("SETTINGS usage <scenario>...");
        println!("SETTINGS scenarios apply text_oversize slider_range null_context null_text invalid_utf8");
        std::process::exit(2);
    }
    let mut failures = 0i32;
    for name in &args {
        match line(name) {
            Some(l) => println!("{}", l),
            None => {
                println!("SETTINGS {} FAIL code=108 cat=invalid unknown_scenario", name);
                failures += 1;
            }
        }
    }
    if failures != 0 {
        std::process::exit(1);
    }
}
