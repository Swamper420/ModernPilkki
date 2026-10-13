// Results rows builder. Clean-room, per docs/sim-results.md: FUN_004c2ce0
// is the string table (rows below cite its verified ids/VAs); the tally
// formulas are the verified accumulate/review code (0x47845a block).
// Pure, no SDL. Header-only.
#pragma once
#include <vector>

namespace results {

// Per-player tallies: session +0x354 count / +0x358 total / +0x35c
// biggest (grams); bites = bite-ring counter (session +0x730).
struct Tally {
  int count = 0;
  double totalG = 0;
  int biggestG = 0;
  int bites = 0;
};

// One results row: shared string-table id (FUN_004c28e0 records) and the
// Finnish label as shipped. valueG < 0 = label-only row.
struct Row {
  int id;
  const char *text;
  double valueG;
};

inline constexpr int kRowTotalWeight = 0x96;   // "Kalojen yhteispaino:" 0x6f9ccc
inline constexpr int kRowNoBites = 0xa7;       // "No bites."            0x6fa754
inline constexpr int kRowBiteFlag = 0xad;      // "Bite indicator flag is up!" 0x6fa7d0
inline constexpr int kRowBiggest = 0x24c;      // "Isoin kala"           0x6fdd74
inline constexpr int kRowInitialDrill = 0x2bc; // "Initial drill"        0x6ffb50

// Build the rows from tally inputs. 1:1 with what docs/sim-results.md
// section 3 verified: total-weight row, biggest-fish row, and "No bites."
// when there were no bites (condition inferred from the string set; the
// code that tests it lives in the un-dumped MENU_RESULTS_* handlers).
inline std::vector<Row> Build(const Tally &t) {
  std::vector<Row> rows;
  rows.push_back({kRowTotalWeight, "Kalojen yhteispaino:", t.totalG});
  rows.push_back({kRowBiggest, "Isoin kala", (double)t.biggestG});
  if (t.bites == 0) rows.push_back({kRowNoBites, "No bites.", -1});
  return rows;
}

// 5-catch review (0x47845a block, docs/sim.md section 5): after 5 landed
// fish the player may give up when the batch is poor. True = give up.
inline bool GiveUpAfterFive(const Tally &t) {
  return t.count >= 5 && (t.biggestG < 50 || t.totalG / 5.0 < 30.0);
}

}  // namespace results
