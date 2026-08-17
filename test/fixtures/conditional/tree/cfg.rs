// The same mechanism through a different syntax (HLR-134).
//
// An attribute has no #else, so #[cfg(X)] can only be removed. It is
// #[cfg(not(X))] with -DX that prunes.

#[cfg(feature_a)]
fn only_a() -> i32 {
    1
}

#[cfg(not(feature_b))]
fn without_b() -> i32 {
    2
}

fn always_rust() -> i32 {
    0
}
