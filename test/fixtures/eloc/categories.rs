// categories.rs — one instance of each ELOC category, and one of each
// exclusion. Hand-counted in README.md beside this file.

use std::vec::Vec;

const LIMIT: i32 = 3;
static SEEN: i32 = 0;

struct Unused {
    field: i32,
}

fn categories(n: i32) -> i32 {
    let mut total = SEEN;

    for i in 0..LIMIT {
        if i == n {
            total += i;
        } else if i > n {
            break;
        } else {
            continue;
        }
    }

    while total > LIMIT {
        total -= 1;
    }

    loop {
        break;
    }

    let values: Vec<i32> = vec![1, 2, 3];
    match values.len() {
        0 => total = 0,
        _ => total += 1,
    }

    if total > 0 && total < 100 {
        total -= 1;
    }

    total
}
