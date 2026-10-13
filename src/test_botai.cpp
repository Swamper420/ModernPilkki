// Smallest asserts for botai: action-queue order + one drain per tick,
// one deterministic WALKING→DRILLING→FISHING→FISH_TAKE_UP flow through the
// hooks, the 5-catch review rule, the AI_korjaus formula, and one
// deterministic fight-roll outcome. Deterministic: fixed seeds only.
#include <cassert>
#include <cstdio>
#include <vector>

#include "botai.hpp"

using namespace botai;

namespace {

struct TestHooks : Hooks {
  int mode = 5;
  int clockSecs = 600;  // 004383c0 round clock
  bool shallow = false, areaOk = true, lake = true, lakeEdge = false;
  int drillRet = 1;
  int landRet = 1;
  Bite nextBite{3, 0, 0.0, 0.0, 60};
  bool biteOk = true;
  std::vector<int> hud, log;
  int DrillStart() override {
    log.push_back(100);
    return drillRet;
  }
  void BoreBegin() override { log.push_back(101); }
  void BoreEnd() override { log.push_back(102); }
  void LineStage() override { log.push_back(103); }
  void Jig() override { log.push_back(104); }
  int LandFish(Bot &, const Bite &) override {
    log.push_back(105);
    return landRet;
  }
  void FishSuccess(Bot &, const Bite &) override { log.push_back(106); }
  void FishEscaped(Bot &, const Bite &) override { log.push_back(107); }
  bool BiteRoll(Bot &, Bite &out) override {
    out = nextBite;
    return biteOk;
  }
  bool ShallowAtDrillPoint() override { return shallow; }
  bool DrillAreaOk() override { return areaOk; }
  int ClockSeconds() override { return clockSecs; }
  int TravelCost() override { return 0; }
  bool ModeIs(int m) override { return m == mode; }
  bool LakeModel() override { return lake; }
  bool LakeEdge() override { return lakeEdge; }
  void HudEvent(int id) override { hud.push_back(id); }
  int ActivityQuality(int) override { return 100; }
};

}  // namespace

int main() {
  // RNG: identical seeds give identical streams; Below(0) == 0.
  {
    MsvcRand a{12345}, b{12345};
    for (int i = 0; i < 10; i++) assert(a.Raw() == b.Raw());
    assert(a.Below(0) == 0);
    assert(a.Below(1) == 0);
  }
  // AI_korjaus = (14 - skill) * 1000 (00478210).
  assert(AIKorjausMs(10) == 4000);
  assert(AIKorjausMs(0) == 14000);

  // Queue: FIFO push (006303c0), one action drained per Tick (006307c0).
  {
    Bot b;
    TestHooks h;
    h.mode = -1;  // no mode-edge events
    Post(b, kEvCancel);  // 0xc -> GoTo(9)
    Post(b, kEvAbort);   // 0xd -> GoTo(10) if it were seen first
    assert(b.queue.size() == 2);
    Tick(b, h, 16);
    assert(b.queue.size() == 1);
    assert(b.state == 9 && !b.hasPending);  // first action won => FIFO
    Tick(b, h, 16);
    assert(b.queue.size() == 0);
    assert(b.state == 9);  // 0xd drained by the unported state 9 (base)
  }

  // One deterministic flow: WALKING -> DRILLING -> FISHING -> TAKE_UP.
  {
    Bot b;
    TestHooks h;
    b.skill = 10;
    b.rng.s = 777;
    b.team = 1;
    assert(b.state == kStWalking);
    // WALKING ev 4 (arrival): deep point, area ok, drill start returns 1.
    Post(b, kEvAct);
    Tick(b, h, 16);
    assert(b.state == kStDrilling);
    assert(!h.log.empty() && h.log.front() == 100);  // DrillStart called
    // DRILLING ev 2 (bore mode 0x18 entered) via the pump's mode edge.
    h.mode = 0x18;
    Tick(b, h, 16);  // posts ev 2, drains the leftover ev 3 first
    Tick(b, h, 16);
    assert(b.state == kStFishing);
    // FISHING: syoko bite roll strikes (skill 10, quality 100 => always).
    Arm(b, kTmrSyoko, 1000);
    Tick(b, h, 1000);
    assert(b.state == kStTakeUp);
    // TAKE_UP: nosto (5 s) elapsed -> fight resolves to a landed fish.
    h.nextBite = Bite{3, 0, 0.0, 0.0, 60};  // no rolls can fire
    Tick(b, h, AINostoMs());
    assert(b.count == 1 && b.total == 60 && b.biggest == 60);
    assert(b.state == kStTakeUp && b.s8 == 1);  // phase 1 after resolve
    assert(h.log.back() == 102);                // BoreEnd (00477220)
  }

  // 5-catch review (00478210): max < 50 or avg < 30 => give up + chat,
  // else silent counter reset.
  {
    Bot b;
    TestHooks h;
    b.bite.weight = 40;
    for (int i = 0; i < 5; i++) ApplyLand(b, h, 1);
    assert(b.count == 0 && b.total == 0 && b.biggest == 0);  // given up
    assert(h.hud.size() == 1 && h.hud[0] == 0x10);
    assert(b.hasPending && b.pending == kStWalking);
    Bot b2;
    TestHooks h2;
    b2.bite.weight = 60;
    for (int i = 0; i < 5; i++) ApplyLand(b2, h2, 1);
    assert(b2.count == 0 && b2.total == 0 && b2.biggest == 0);  // reset
    assert(h2.hud.empty() && !b2.hasPending);
  }

  // Fight rolls (00478210): p_catch = 1.0 catches on any seed; the two
  // draws keep their order.
  {
    Bot b;
    TestHooks h;
    h.mode = -1;  // no mode-edge events
    b.rng.s = 42;
    b.state = kStTakeUp;
    b.bite = Bite{3, 0, 1.0, 0.0, 60};
    Post(b, kEvAct);
    Tick(b, h, 16);
    // FishSuccess first, then the phase-0 tail's BoreEnd (00477220).
    assert(h.log.size() == 2 && h.log[0] == 106 && h.log[1] == 102);
    assert(b.miss == 1);  // no land => miss path
    assert(b.s8 == 1);    // phase advanced
  }
  std::puts("test_botai ok");
  return 0;
}
