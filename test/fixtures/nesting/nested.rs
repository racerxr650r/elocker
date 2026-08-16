// nested.rs — statements and decision points attributed to the innermost
// *named* function. Hand-counted in README.md beside this file.

fn outer(seed: i32) -> i32 {
    let mut total = seed;

    fn inner(x: i32) -> i32 {
        if x > 0 {
            x * 2
        } else {
            0
        }
    }

    let double = |x: i32| if x > 0 { x * 2 } else { 0 };

    if total > 0 && total < 100 {
        total = inner(total) + double(total);
    }
    total
}
