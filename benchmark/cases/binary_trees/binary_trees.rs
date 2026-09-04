use std::time::Instant;

struct Node {
    l: Option<Box<Node>>,
    r: Option<Box<Node>>,
}

fn make(d: i32) -> Box<Node> {
    if d == 0 {
        Box::new(Node { l: None, r: None })
    } else {
        Box::new(Node {
            l: Some(make(d - 1)),
            r: Some(make(d - 1)),
        })
    }
}

fn check(n: &Node) -> i64 {
    match &n.l {
        None => 1,
        Some(l) => 1 + check(l) + check(n.r.as_ref().unwrap()),
    }
}

fn main() {
    let depth: i32 = std::env::var("BENCH_SCALE").unwrap().parse().unwrap();
    let t0 = Instant::now();
    let root = make(depth);
    let count = check(&root);
    let ns = t0.elapsed().as_nanos();
    println!("bintree: nodes = {}", count);
    println!("__bench_ns: {}", ns);
}
