# Results builder reconstruction (clean-room, partial)

Target: `extracted/ProPilkki2.exe`. VAs are image addresses. Companion to
`docs/sim-timeline.md`. Verified claims cite code; guesses are marked
*(inferred)*.

---

## 1. What `FUN_004c2ce0` actually is (verified — read this first)

The five supplied dumps (`004ca285`, `004cd1d4`, `004cd454`, `004dc7ce`,
`004e4fce`, all labelled `FUN_004c2ce0`) failed with Ghidra's "Flow
exceeded maximum allowable instructions" — but the raw disassembly shows
they are **not** a per-round builder at all. `FUN_004c2ce0` is the game's
**global string-table initializer**: one gigantic function that appends
every UI string of every language through

```
004c28e0(entry, id, text, value)   // 12-byte record:
                                   //   +0 = string id, +4 = strdup(text),
                                   //   +8 = value; then 0059dd20(text, '\\', '\n')
```

6288 such calls span `0x4c2d30 … 0x58618f` (Finnish, English, Russian,
Ukrainian blocks repeating the same ids). Row "id" is the shared string
id; the store `mov %eax, (%base, id*4)` indexes the table by id. Labels
may contain `\\` which `0059dd20` turns into newlines.

So the five named strings are **string-table entries**, not builder code.
The per-round results composition lives in the `MENU_RESULTS_*` screens
(RTTI `.?AVMENU_RESULTS_START@@` … at `0x3a6b4c..`), whose handlers are in
the screen-class cluster `006489c0/00648d80/00648f60/00649370/00649540/
00649a30` — **not yet decompiled** (open item, see §5).

## 2. Verified results strings (ids, VAs, text)

From the ctor calls inside `FUN_004c2ce0` (Finnish/English blocks; the
same id exists per language block):

| id | VA | text | role |
|---|---|---|---|
| 0x94 (148) | `0x6f9c94` | `Kilpailuaikaa jäljellä:` | "competition time left" (status board) |
| 0x95 (149) | `0x6f9cb0` | `Tuloksesi tällä hetkellä` | "your results right now" (header) |
| 0x96 (150) | `0x6f9ccc` | `Kalojen yhteispaino:` | **total catch weight** row |
| 0x97 (151) | `0x6f9ce4` | `Sijoitus:` | placing row |
| 0xa7 (167) | `0x6fa754` | `No bites.` | shown when there were no bites |
| 0xad (173) | `0x6fa7d0` | `Bite indicator flag is up!` | flag-rod bite alert |
| 0xae (174) | `0x6fa7ec` | `Bite indicator flag up` | (cont. line) |
| 0x24b (587) | `0x6fdd68` | `Voittaja` | "Winner" (results screen) |
| 0x24c (588) | `0x6fdd74` | `Isoin kala` | **biggest fish** row |
| 0x24d (589) | `0x6fdd80` | `Eniten lajeja` | "most species" |
| 0x2bc (700) | `0x6ffb50` | `Initial drill` | drill-stat row label |
| 0x244..0x257 | `0x6fdd0c..` | `Sijoitukset ja pisteet`, `Muuta`, `Kilpailun cup pisteet`, `Palkinnot`, `Pilkki`, `Sijat`, `Sija`, `Sääennuste`, `Tuuli`, `Osakilpailu`, `Järven tiedot`, `Onneksi olkoon`, … | RESULTS_* screen chrome |

Game-type winner-criterion descriptions (ids 498–522, `0x70a178..`) fix
the *semantics* of the scoring rows per game type (e.g. id 498
`Kalojen yhteispaino määrittää voittajan.\Kaikki muut paitsi kiisket
hyväksytään.` = "total weight decides the winner, all but ruffe
count"; id 500 = total weight, all species; id 501 = fish *count*,
all species; 521/522 = top-3/top-5 total). Results rows therefore carry
either a **weight** (g) or a **count** depending on the game type.

## 3. Verified tally inputs and formulas

Per-session (`0x2340` session object) tallies (verified uses):

| field | meaning | verified sites |
|---|---|---|
| `+0x354` | catch count | `incl` at `0x47845a`; zeroed `0x472833` / `0x474df3` |
| `+0x358` | total weight (int) | `+= w` `0x47846a..0x47847a`; zeroed `0x472840` |
| `+0x35c` | biggest weight (int) | max-clamp `0x47848f..0x4784ab`; zeroed `0x47284d` |
| `+0x730 / +0x734` | bite-ring counter / index (64 × `0x70` ring at `+0x738`) | `00471940` allocator |

Accumulate formula (landed fish, weight `w`, AI path `00478210` action 4
mirrors the player path — `0x47845a` block):

```
count += 1;
total += w;
biggest = max(biggest, w);
```

**5-catch review** (verified constants `0x32 = 50`, docs/sim.md §5):
when `count == 5`: if `biggest < 50 || total / 5 < 30` → give up (HUD
events `0xf/0x10`) else reset `count = total = biggest = 0`
(`0x478542..0x47855a`).

Score readback `00438a00(client) = (double)*(client+0x150)` (verified).

Round reset `00436a30(match, 1, 0)` zeroes, per bag/player entry of
`match+0x6c` (entry id at `+0xa8`, lookup `00437f60`): `+0xb4, +0xb8,
+0x94, +0x98, +4, +8, +0xc` (verified — see sim-timeline.md §5).

Row formulas reconstructed1:1 from the strings + tally semantics:

| row | value | condition |
|---|---|---|
| 0x96 `Kalojen yhteispaino:` | `tally.total` (g) | always (current-results board) |
| 0x24c `Isoin kala` | `tally.biggest` (g) | results screen |
| 0xa7 `No bites.` | — | when bite count == 0 *(condition inferred: bite ring counter `+0x730 == 0`; the code that tests it is in the un-dumped screen handlers)* |
| 0xad `Bite indicator flag is up!` | — | flag-rod bite latch (net `+0x2339`-style flag) *(trigger inferred)* |
| 0x2bc `Initial drill` | drill stat | *(value source open — the drill outcome fields `7bcc94/95`, `+0x414/+0x418` are the likely inputs)* |

`src/results.hpp` implements exactly these rows (total, biggest,
`No bites.` at bites == 0) from a `Tally{count, totalG, biggestG, bites}`
plus the 5-catch review predicate; nothing else is claimed.

## 4. Results flow (verified at the state level)

Round end → `DAT_007c6904 = 0x15` (state 21; see sim-timeline.md §5).
Screen registry ids (decomp-notes): `0x16 RESULTS_START` (out fade 500),
`0x17 RESULTS_TOTAL`, `0x18 RESULTS_PLAYER` (in 500),
`0x19 RESULTS_OWN_RECORDS`, `0x1a RESULTS_OWN_PRIZES`,
`0x1b RESULTS_CUP_FINNISHED` (in 500), `0x1c RESULTS_CUP_PRIZES`.
Net play exchanges per-player results via `handle_INTERMEDIATE_RESULTS_MSG`
(string xref) and validates host/client fish bags
(`Player %s (%s) reported fish bag mismatch:` at `0x6d66cb`).

## 5. Open items

1. The `MENU_RESULTS_*` screen handlers (cluster `006489c0/00648d80/
   00648f60/00649370/00649540/00649a30`) — the real row *layout* code
   (which rows appear per game type, formatting of weights, placement
   sort). Not decompiled; Ghidra dump of that cluster is the next step.
2. The `No bites.` / `Initial drill` predicates (likely tiny once (1) is
   dumped).
3. `handle_INTERMEDIATE_RESULTS_MSG` deserializer + bag-mismatch compare
   (entry layout: id `+0xa8`, others open).
4. Weight display unit: tallies are grams (FishEntry `+0x14 − s16(+0x24)`);
   the rows print kg-with-decimals *(formatting open)*.
