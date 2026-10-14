//! MSVC LCG RNG — bit-exact port of the C++ oracle (`src/bite.hpp` `bite::Rng`,
//! aliased as `game::MsvcRand`; `ctrl::MsvcRand` is the same LCG).
//!
//! - `next` — `00676d2c`: `s = s * 214013u + 2531011u`, return `(s >> 16) & 0x7fff`.
//! - `rand_max` — `0059db20`: two draws form `u = (r2 << 15) | r1` (30 bits),
//!   result `(int)(u * 2^-30 * n)` clamped to `n-1`; `n <= 0` yields 0.

pub struct MsvcRand {
    s: u32,
}

impl MsvcRand {
    pub fn new(seed: u32) -> Self {
        MsvcRand { s: seed }
    }

    /// `00676d2c`: one 15-bit MSVC `rand()` draw.
    pub fn next(&mut self) -> u32 {
        self.s = self.s.wrapping_mul(214013).wrapping_add(2531011);
        (self.s >> 16) & 0x7fff
    }

    /// `0059db20`: `rand(n)`, `(int)(u * 2^-30 * n)` clamped to `n-1`.
    pub fn rand_max(&mut self, n: i32) -> i32 {
        if n <= 0 {
            return 0;
        }
        let r1 = self.next() & 0x7fff;
        let r2 = self.next() & 0x7fff;
        let u = (r2 << 15) | r1;
        // evaluation order matches C++: (u * 2^-30) * n
        let r = (u as f64 * 9.313225746154785e-10 * n as f64) as i32;
        if r > n - 1 {
            n - 1
        } else {
            r
        }
    }
}

#[cfg(test)]
mod tests {
    use super::MsvcRand;

    #[test]
    fn golden_sequence_srand_0x1234() {
        // Frozen golden vector from the C++ oracle, src/test_game.cpp:74-82.
        let mut r = MsvcRand::new(0x1234);
        let expect = [
            15256, 25789, 4789, 5383, 4116, 25814, 28931, 22672, 15197, 19848,
        ];
        for (i, &e) in expect.iter().enumerate() {
            assert_eq!(r.next(), e, "draw {}", i);
        }
    }

    #[test]
    fn rand_max_matches_oracle() {
        // rand(1000) values for srand(0x1234), src/test_game.cpp:80-82.
        let mut r = MsvcRand::new(0x1234);
        let expect = [787, 164, 787, 691, 605];
        for (i, &e) in expect.iter().enumerate() {
            assert_eq!(r.rand_max(1000), e, "rand(1000) #{}", i);
        }
        assert_eq!(r.rand_max(0), 0);
        assert_eq!(r.rand_max(1), 0);
        let v = r.rand_max(7);
        assert!((0..7).contains(&v), "rand(7) out of range: {}", v);
    }
}
