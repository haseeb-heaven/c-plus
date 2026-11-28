/* Safe-Rust equivalent of examples/expanded/engine/ecs.cp.
 *
 * Models the same deterministic fixed-seed ECS simulation using owned
 * Vec/HashMap containers. No unsafe, no FFI, no raw pointers. C+ checked
 * container accessors (vector_at_checked / intmap_get) are modelled with
 * Vec::get / HashMap::get; the recoverable error* API is modelled with a
 * Result carrying (category, code, message).
 */

use std::collections::HashMap;

const ENTITY_COUNT: usize = 6;
const TICK_COUNT: i32 = 12;
const WORLD_MIN: i32 = -9;
const WORLD_MAX: i32 = 9;

struct Rng {
    state: u64,
}

impl Rng {
    fn new() -> Self {
        Rng { state: 0x1234_5678 }
    }

    fn next(&mut self, lo: i32, hi: i32) -> i32 {
        if hi <= lo {
            return lo;
        }
        self.state = self
            .state
            .wrapping_mul(6_364_136_223_846_793_005)
            .wrapping_add(1_442_695_040_888_963_407);
        let bits = ((self.state >> 33) & 0xffff_ffff) as u32;
        let span = (hi - lo + 1) as u32;
        if span == 0 {
            return lo;
        }
        lo + (bits % span) as i32
    }
}

#[derive(Default)]
struct World {
    ids: Vec<i32>,
    posx: HashMap<i32, i32>,
    posy: HashMap<i32, i32>,
    velx: HashMap<i32, i32>,
    vely: HashMap<i32, i32>,
    health: HashMap<i32, i32>,
    alive: HashMap<i32, i32>,
}

impl World {
    fn spawn(&mut self, id: i32, px: i32, py: i32, vx: i32, vy: i32, hp: i32) {
        self.ids.push(id);
        self.posx.insert(id, px);
        self.posy.insert(id, py);
        self.velx.insert(id, vx);
        self.vely.insert(id, vy);
        self.health.insert(id, hp);
        self.alive.insert(id, 1);
    }

    fn alive_value(&self, id: i32) -> i32 {
        *self.alive.get(&id).unwrap_or(&0)
    }

    fn system_move(&mut self) {
        let n = self.ids.len();
        for i in 0..n {
            let id = match self.ids.get(i) {
                Some(v) => *v,
                None => continue,
            };
            if self.alive_value(id) == 0 {
                continue;
            }
            let (x, y, vx, vy) = match (
                self.posx.get(&id),
                self.posy.get(&id),
                self.velx.get(&id),
                self.vely.get(&id),
            ) {
                (Some(x), Some(y), Some(vx), Some(vy)) => (*x, *y, *vx, *vy),
                _ => continue,
            };
            let mut x = x + vx;
            let mut y = y + vy;
            if x < WORLD_MIN {
                x = WORLD_MAX;
            }
            if x > WORLD_MAX {
                x = WORLD_MIN;
            }
            if y < WORLD_MIN {
                y = WORLD_MAX;
            }
            if y > WORLD_MAX {
                y = WORLD_MIN;
            }
            self.posx.insert(id, x);
            self.posy.insert(id, y);
        }
    }

    fn system_damage(&mut self, tick: i32, rng: &mut Rng) {
        let mut alive_ids: Vec<i32> = Vec::new();
        for &id in &self.ids {
            if alive_ids.len() >= ENTITY_COUNT {
                break;
            }
            if self.alive_value(id) != 0 {
                alive_ids.push(id);
            }
        }
        if alive_ids.is_empty() {
            println!("DAMAGE tick= {} no living targets", tick);
            return;
        }
        let victim = alive_ids[rng.next(0, alive_ids.len() as i32 - 1) as usize];
        let amount = rng.next(2, 4);
        let hp = match self.health.get(&victim) {
            Some(v) => *v,
            None => return,
        };
        let hp = hp - amount;
        self.health.insert(victim, hp);
        println!(
            "DAMAGE tick= {} id= {} amount= {} hp= {}",
            tick, victim, amount, hp
        );
        if hp <= 0 {
            self.alive.insert(victim, 0);
            println!("DEATH tick= {} id= {}", tick, victim);
        }
    }

    fn system_remove_dead(&mut self) {
        let mut i = self.ids.len();
        while i > 0 {
            i -= 1;
            let id = match self.ids.get(i) {
                Some(v) => *v,
                None => continue,
            };
            if self.alive_value(id) != 0 {
                continue;
            }
            self.posx.remove(&id);
            self.posy.remove(&id);
            self.velx.remove(&id);
            self.vely.remove(&id);
            self.health.remove(&id);
            self.alive.remove(&id);
            self.ids.remove(i);
            println!("REMOVE id= {}", id);
        }
    }

    fn print_world(&self, tick: i32) {
        println!("TICK {} entities= {}", tick, self.ids.len());
        for &id in &self.ids {
            if self.alive_value(id) == 0 {
                continue;
            }
            let (x, y, hp) = match (
                self.posx.get(&id),
                self.posy.get(&id),
                self.health.get(&id),
            ) {
                (Some(x), Some(y), Some(hp)) => (*x, *y, *hp),
                _ => continue,
            };
            println!("  ENTITY id= {} x= {} y= {} hp= {}", id, x, y, hp);
        }
    }

    fn checksum(&self) -> i32 {
        let mut sum = 0;
        for &id in &self.ids {
            if self.alive_value(id) == 0 {
                continue;
            }
            let (x, y, hp) = match (
                self.posx.get(&id),
                self.posy.get(&id),
                self.health.get(&id),
            ) {
                (Some(x), Some(y), Some(hp)) => (*x, *y, *hp),
                _ => continue,
            };
            sum += id + x * 3 + y * 5 + hp * 7;
        }
        sum
    }

    /// Modelled recoverable error*: Ok(value) or Err((category, code, msg)).
    fn component_get(&self, id: i32, name: &str) -> Result<i32, (&'static str, i32, &'static str)> {
        let _value = match self.health.get(&id) {
            Some(v) => *v,
            None => {
                return Err((
                    "invalid",
                    106,
                    "missing entity: no component record for id",
                ));
            }
        };
        if self.alive_value(id) == 0 {
            return Err((
                "lifecycle",
                103,
                "missing entity: entity is dead or removed",
            ));
        }
        let map = match name {
            "posx" => &self.posx,
            "posy" => &self.posy,
            "velx" => &self.velx,
            "vely" => &self.vely,
            "health" => &self.health,
            _ => {
                return Err((
                    "invalid",
                    108,
                    "missing component: unknown component name",
                ));
            }
        };
        match map.get(&id) {
            Some(v) => Ok(*v),
            None => Err(("bounds", 101, "missing component: checked read failed")),
        }
    }
}

fn report_refusal(label: &str, err: (&'static str, i32, &'static str)) {
    let (cat, code, msg) = err;
    println!(
        "REFUSE {} ok= 0 category= {} code= {} msg= {}",
        label, cat, code, msg
    );
}

fn main() {
    let mut w = World::default();
    let mut rng = Rng::new();

    println!("ECS seed=0x12345678 ticks= 12 entities= 6");
    w.spawn(101, -4, 2, 1, 0, 9);
    w.spawn(102, 3, -3, -1, 1, 7);
    w.spawn(103, 0, 0, 0, -1, 8);
    w.spawn(104, 5, 5, -1, -1, 6);
    w.spawn(105, -6, -2, 2, 1, 10);
    w.spawn(106, 1, 6, 0, -2, 5);
    w.print_world(-1);

    for tick in 0..TICK_COUNT {
        w.system_move();
        w.system_damage(tick, &mut rng);
        w.system_remove_dead();
        w.print_world(tick);
    }

    let first_removed = (101..=106)
        .find(|id| w.health.get(&id).is_none())
        .unwrap_or(-1);

    if let Err(e) = w.component_get(999, "posx") {
        report_refusal("unknown-entity", e);
    }
    if let Err(e) = w.component_get(101, "mass") {
        report_refusal("unknown-component", e);
    }
    if first_removed >= 0 {
        if let Err(e) = w.component_get(first_removed, "posx") {
            report_refusal("removed-entity", e);
        }
    }

    println!(
        "SUMMARY survivors= {} checksum= {}",
        w.ids.len(),
        w.checksum()
    );
}
