//! `.ves` lake container — 1:1 port of the C++ oracle `src/ves.hpp`
//! (`ves::Hash`, `ves::Parse`, `ves::ParseObjPos`, `ves::ParseGrid`).
//!
//! Game-side routines the oracle was verified against:
//! - `FUN_00430770` — section framing + trailer hash check (reader status
//!   0 = hash OK, 5 = mismatch).
//! - `FUN_004b6d70` — OBJECT_POSITIONS record loader (x/y/z raw game units,
//!   tail u64 split into 3×i16).
//!
//! Grammar: header + sections (`<TAG>` u32-LE len payload u32 hash) + file
//! trailer = hash of all preceding bytes. Hash:
//! `H = XOR_j ((byte_j ^ 0x5A) << (j % 24))` (verified 816/816 section
//! trailers across 48 lakes, `tools/ves_hash.py`).
//!
//! Corrupt input returns the same error strings the C++ throws
//! (`std::runtime_error` messages carried over verbatim).
//!
//! Deviation: the C++ reads the file trailer before the `off + 4 == size`
//! check (OOB read on <4 trailing bytes, then "trailing bytes"); Rust checks
//! bounds first — same observable errors, no UB. Tags are decoded lossy;
//! real files have ASCII tags.

/// `ves::Hash` (`src/ves.hpp`): `XOR_j ((byte_j ^ 0x5A) << (j % 24))`.
pub fn hash(p: &[u8]) -> u32 {
    let mut h: u32 = 0;
    for (j, &b) in p.iter().enumerate() {
        h ^= ((b ^ 0x5A) as u32) << (j % 24);
    }
    h
}

fn u32_at(d: &[u8], o: usize) -> u32 {
    u32::from_le_bytes(d[o..o + 4].try_into().unwrap())  // memcpy, little-endian
}

pub struct Section {
    pub tag: String,
    pub payload: Vec<u8>,  // OBJECT_POSITIONS: raw span (incl ngroups)
    pub trailer: u32,      // hash over payload bytes
    pub checked: bool,
}

/// OBJECT_POSITIONS record (`FUN_004b6d70`, tools/objpos.py 48/48 exact):
/// u8 flag(0), u8 class, u16 pad, i32 x/y/z (raw game units, 600 per grid
/// cell), u64 tail (loader takes 3×i16 of low 48 bits).
/// pad/tail-hi16 raw until decomp pins them.
pub struct ObjRec {
    pub cls: u8,
    pub pad: u16,
    pub x: i32,
    pub y: i32,
    pub z: i32,
    pub s: [i16; 3],
    pub tailhi: [u8; 2],
}

pub type ObjGroup = Vec<ObjRec>;

/// `ves::ParseObjPos` (`src/ves.hpp`): u32 ngroups + groups of
/// (u32 nitems + nitems × 25-byte records), exact-consume.
pub fn parse_obj_pos(p: &[u8]) -> Result<Vec<ObjGroup>, String> {
    let n = p.len();
    if n < 4 {
        return Err("truncated objpos".into());
    }
    let ng = u32_at(p, 0);
    let mut off = 4usize;
    let mut out = Vec::new();
    for _ in 0..ng {
        if off + 4 > n {
            return Err("truncated objpos group".into());
        }
        let ni = u32_at(p, off) as usize;
        off += 4;
        if off + ni * 25 > n {
            return Err("truncated objpos recs".into());
        }
        let mut grp = Vec::new();
        for _ in 0..ni {
            let r = &p[off..off + 25];
            if r[0] != 0 {
                return Err("bad objpos record".into());
            }
            let t = u64::from_le_bytes(r[17..25].try_into().unwrap());
            grp.push(ObjRec {
                cls: r[1],
                pad: u16::from_le_bytes([r[2], r[3]]),
                x: i32::from_le_bytes(r[5..9].try_into().unwrap()),
                y: i32::from_le_bytes(r[9..13].try_into().unwrap()),
                z: i32::from_le_bytes(r[13..17].try_into().unwrap()),
                s: [t as i16, (t >> 16) as i16, (t >> 32) as i16],
                tailhi: [r[23], r[24]],
            });
            off += 25;
        }
        out.push(grp);
    }
    if off != n {
        return Err("objpos size mismatch".into());
    }
    Ok(out)
}

/// `ves::Parse` (`src/ves.hpp`): header + sections + file trailer check.
pub fn parse(d: &[u8]) -> Result<Vec<Section>, String> {
    const HDR: &[u8] = b"Pro Pilkki datafile v002\nCopyright Procyon Products\n";
    if d.len() < HDR.len() || &d[..HDR.len()] != HDR {
        return Err("bad ves header".into());
    }
    let mut off = HDR.len();
    while off < d.len() && (d[off] == b'\n' || d[off] == b'\r') {
        off += 1;
    }
    let mut out = Vec::new();
    while d.len() - off > 4 {
        if d[off] != b'<' {
            return Err(format!("expected < at {}", off));
        }
        let mut end = off;
        while end < d.len() && d[end] != b'>' {
            end += 1;
        }
        if end + 9 > d.len() {
            return Err("truncated tag".into());
        }
        let tag = String::from_utf8_lossy(&d[off + 1..end]).into_owned();
        let len = u32_at(d, end + 1) as usize;
        if end + 9 + len > d.len() {
            return Err(format!("truncated {}", tag));
        }
        let payload = d[end + 5..end + 5 + len].to_vec();
        let trailer = u32_at(d, end + 5 + len);
        let checked = hash(&payload) == trailer;
        out.push(Section {
            tag,
            payload,
            trailer,
            checked,
        });
        off = end + 9 + len;
    }
    // file trailer = hash of everything before it
    if off + 4 != d.len() {
        return Err("trailing bytes".into());
    }
    let ft = u32_at(d, off);
    if hash(&d[..off]) != ft {
        return Err("file hash mismatch".into());
    }
    Ok(out)
}

// ---- DEPTHMAP/TYPEMAP grids ----

/// Payload: `0a 05 01 08` + u32 0 + u16 w + u16 h + u16 200 + u16 200
/// + RLE runs + 768 B palette (256 RGB triplets; gray ramp, or EGA-ish
/// on 16 DEPTHMAPs). Runs: byte < 0xc0 = 1 literal cell; else (count =
/// byte & 0x3f) cells of next byte. Encoder overruns the last run;
/// decode clips to w*h (verified 96/96 maps, exact-consume of cells).
/// Cell semantics open (depth 0..190 vs 17 DEPTH_TABLE thresholds;
/// typemap enum?) and palette use unconfirmed in decomp.
pub struct Grid {
    pub w: u16,
    pub h: u16,
    pub cells: Vec<u8>,  // w*h
    pub pal: Vec<u8>,    // 768 B tail as stored
}

/// `ves::ParseGrid` (`src/ves.hpp`).
pub fn parse_grid(p: &[u8]) -> Result<Grid, String> {
    const MAGIC: [u8; 4] = [0x0a, 0x05, 0x01, 0x08];
    if p.len() < 16 + 768 || p[..4] != MAGIC {
        return Err("bad grid header".into());
    }
    if u32_at(p, 4) != 0 || u32_at(p, 12) != 0x00c8_00c8 {
        return Err("bad grid header fields".into());
    }
    let w = u16::from_le_bytes([p[8], p[9]]);
    let h = u16::from_le_bytes([p[10], p[11]]);
    let need = w as usize * h as usize;
    let mut cells = Vec::with_capacity(need);
    let mut i = 16usize;
    while cells.len() < need {
        if i >= p.len() - 768 {
            return Err("truncated grid runs".into());
        }
        let b = p[i];
        if b < 0xc0 {
            cells.push(b);
            i += 1;
        } else {
            if i + 1 >= p.len() - 768 {
                return Err("truncated grid run".into());
            }
            let mut n = (b & 0x3f) as usize;
            // Clip encoder overrun at grid end.
            if cells.len() + n > need {
                n = need - cells.len();
            }
            cells.resize(cells.len() + n, p[i + 1]);
            i += 2;
        }
    }
    let pal = p[p.len() - 768..].to_vec();
    Ok(Grid { w, h, cells, pal })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn put_u32(v: &mut Vec<u8>, x: u32) {
        v.extend_from_slice(&x.to_le_bytes());
    }

    const DEPTHS: [u32; 17] = [
        0, 30, 60, 130, 160, 200, 250, 300, 350, 400, 500, 700, 900, 1000, 1500, 2000, 2500,
    ];

    fn depths_bytes() -> Vec<u8> {
        DEPTHS.iter().flat_map(|x| x.to_le_bytes()).collect()
    }

    // Synthetic file of test_ves.cpp item 2: header + OBJECT_POSITIONS +
    // DEPTH_TABLE + file trailer.
    fn synthetic() -> Vec<u8> {
        let mut f = Vec::new();
        f.extend_from_slice(b"Pro Pilkki datafile v002\nCopyright Procyon Products\n\n");
        f.extend_from_slice(b"<OBJECT_POSITIONS>");
        put_u32(&mut f, 4); // slen: 4 + count + 0 record bytes + trailer
        put_u32(&mut f, 0); // ngroups = 0
        put_u32(&mut f, 0x1234_5678); // OBJECT_POSITIONS trailer (unchecked by game)
        f.extend_from_slice(b"<DEPTH_TABLE>");
        put_u32(&mut f, (DEPTHS.len() * 4) as u32);
        f.extend_from_slice(&depths_bytes());
        put_u32(&mut f, 0x04EC_5BAE);
        let ft = hash(&f);
        put_u32(&mut f, ft);
        f
    }

    #[test]
    fn hash_matches_game_depth_table() {
        // DEPTH_TABLE payload -> 0x04EC5BAE (meri.ves), test_ves.cpp:1.
        assert_eq!(hash(&depths_bytes()), 0x04EC_5BAE);
    }

    #[test]
    fn synthetic_file_parses() {
        // test_ves.cpp:2.
        let secs = parse(&synthetic()).expect("synthetic parses");
        assert_eq!(secs.len(), 2);
        assert_eq!(secs[0].tag, "OBJECT_POSITIONS");
        assert_eq!(secs[1].tag, "DEPTH_TABLE");
        assert!(secs[1].checked);
        assert_eq!(secs[0].trailer, 0x1234_5678);
        assert!(!secs[0].checked); // hash of 4 zero bytes is 0x5A, not the sentinel
    }

    #[test]
    fn corrupt_byte_fails_file_hash() {
        // test_ves.cpp:3: f[100] lands in the DEPTH_TABLE payload; the file
        // trailer no longer matches -> same throw as C++.
        let mut f = synthetic();
        f[100] += 1;
        assert_eq!(parse(&f).map(drop).unwrap_err(), "file hash mismatch");
    }

    #[test]
    fn objpos_roundtrip_and_truncation() {
        // test_ves.cpp:4.
        let mut ob = Vec::new();
        put_u32(&mut ob, 1); // ngroups
        put_u32(&mut ob, 2); // nitems
        // rec0: cls 79, x = 89.0 in 8.24, y = -77*2^24+316 (real meri bytes)
        let mut r0 = [0u8; 25];
        r0[..18].copy_from_slice(&[
            0, 79, 0, 0, 0, 0, 0, 0, 0x59, 0x3c, 1, 0, 0xB3, 0, 0, 0, 0, 0,
        ]); // rest zero
        let mut r1 = [0u8; 25];
        r1[1] = 5;
        ob.extend_from_slice(&r0);
        ob.extend_from_slice(&r1);
        let grps = parse_obj_pos(&ob).expect("objpos parses");
        assert_eq!(grps.len(), 1);
        assert_eq!(grps[0].len(), 2);
        assert_eq!(grps[0][0].cls, 79);
        assert_eq!(grps[0][0].x, 89 * 16777216);
        assert_eq!(grps[0][0].y, -77 * 16777216 + 316); // low bits kept raw
        assert_eq!(grps[0][1].cls, 5);
        assert_eq!(grps[0][1].z, 0);
        ob.pop();
        assert_eq!(
            parse_obj_pos(&ob).map(drop).unwrap_err(),
            "truncated objpos recs"
        );
    }

    #[test]
    fn grid_literals_run_clip_palette() {
        // test_ves.cpp:5.
        let mut gp = vec![
            0x0a, 0x05, 0x01, 0x08, 0, 0, 0, 0, 4, 0, 2, 0, 0xc8, 0, 0xc8, 0,
            7, 0xc2, 9, 0xc6, 3, // 1+2+6=9, clip to 8
        ];
        gp.extend(std::iter::repeat(0x7e).take(768)); // fake palette
        let gr = parse_grid(&gp).expect("grid parses");
        assert_eq!(gr.w, 4);
        assert_eq!(gr.h, 2);
        assert_eq!(gr.cells.len(), 8);
        assert_eq!(gr.cells, vec![7, 9, 9, 3, 3, 3, 3, 3]); // last run clipped 6->5
        assert_eq!(gr.pal.len(), 768);
        assert_eq!(gr.pal[0], 0x7e);
    }

    #[test]
    fn real_lakes_match_golden_counts() {
        // Same key counts the C++ asserts on synthetic data, over the real
        // 48 lakes (doc goldens: tools/ves_hash.py 816/816, tools/objpos.py
        // 290431 items/54 classes, tools/ves.py 96/96 grids). Skipped when
        // extracted/ is absent (gitignored, never committed).
        let dir = std::path::Path::new("../extracted/lakes");
        if !dir.is_dir() {
            return;
        }
        let mut paths: Vec<_> = std::fs::read_dir(dir)
            .unwrap()
            .map(|e| e.unwrap().path())
            .filter(|p| p.extension().map_or(false, |e| e == "ves"))
            .collect();
        paths.sort();
        assert_eq!(paths.len(), 48);
        let mut sections = 0usize;
        let mut items = 0usize;
        let mut classes = [false; 256];
        let mut grids = 0usize;
        for path in &paths {
            let data = std::fs::read(path).unwrap();
            let secs = parse(&data).unwrap_or_else(|e| panic!("{:?}: {}", path, e));
            sections += secs.len();
            for s in &secs {
                assert!(s.checked, "{:?} {} trailer", path, s.tag);
            }
            let op = secs.iter().find(|s| s.tag == "OBJECT_POSITIONS").unwrap();
            let grps = parse_obj_pos(&op.payload).expect("objpos");
            for g in &grps {
                for r in g {
                    classes[r.cls as usize] = true;
                    items += 1;
                }
            }
            for tag in ["DEPTHMAP", "TYPEMAP"] {
                let s = secs.iter().find(|s| s.tag == tag).unwrap();
                let g = parse_grid(&s.payload).expect("grid");
                assert_eq!(g.cells.len(), g.w as usize * g.h as usize);
                assert_eq!(g.pal.len(), 768);
                grids += 1;
            }
        }
        assert_eq!(sections, 816);
        assert_eq!(items, 290431);
        assert_eq!(classes.iter().filter(|&&c| c).count(), 54);
        assert_eq!(grids, 96);

        // meri.ves spot check (test_ves.cpp goldens come from it).
        let data = std::fs::read("../extracted/lakes/meri.ves").unwrap();
        let secs = parse(&data).unwrap();
        assert_eq!(secs.len(), 17);
        assert_eq!(secs[1].tag, "DEPTH_TABLE");
        assert_eq!(secs[1].payload.len(), 68);
        assert_eq!(secs[1].trailer, 0x04EC_5BAE);
        assert!(secs[1].checked);
        for tag in ["DEPTHMAP", "TYPEMAP"] {
            let s = secs.iter().find(|s| s.tag == tag).unwrap();
            let g = parse_grid(&s.payload).unwrap();
            assert_eq!((g.w, g.h), (159, 159));
        }
    }
}
