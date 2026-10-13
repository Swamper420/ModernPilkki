// Playable loop: lake scene (ves DEPTHMAP + OBJECT_POSITIONS) + input
// bitmap + tick driver + audio init + offline UI states.
// Usage: ./pilkki [lake.ves] [objects.dat] [snow=0|1] [frames=0]
// No lake args: TITLE -> LAKE_SELECT (scans extracted/lakes/*.ves) ->
// PLAY (HUD) -> PAUSE -> RESULTS. Lake args skip menus straight to PLAY.
// Missing/unparseable files: prints reason, continues with empty scene
// (never hard-fails).
// Headless: SDL dummy driver auto-caps frames so bare run exits.
#include <SDL3/SDL.h>

#include <cstdio>
#include <string>
#include <vector>

#include "angler.hpp"
#include "audio.hpp"
#include "audio_play.hpp"
#include "camera.hpp"
#include "game.hpp"
#include "input.hpp"
#include "menu.hpp"
#include "models.hpp"
#include "objects.hpp"
#include "offline.hpp"
#include "render.hpp"
#include "terrain.hpp"
#include "tick.hpp"
#include "ui.hpp"
#include "ves.hpp"

static std::vector<uint8_t> Read(const char *p) {
  FILE *f = fopen(p, "rb");
  if (!f) throw std::runtime_error("no such file");
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  std::vector<uint8_t> d(n > 0 ? (size_t)n : 0);
  if (n > 0 && fread(d.data(), 1, (size_t)n, f) != (size_t)n)
    throw std::runtime_error("read");
  fclose(f);
  return d;
}

// Never hard-fails: missing/unparseable files -> empty scene + defaults.
static bool LoadScene(const char *lakePath, const char *objPath, bool snow,
                      ves::Grid &depth, ves::Grid &typemap, bool &hasType,
                      bool &hasVisual, std::vector<uint8_t> &visual,
                      std::vector<ves::ObjGroup> &groups,
                      std::vector<objects::ObjTable> &tables,
                      const objects::ObjTable *&tab, ui::Rules &rules,
                      std::string &name) {
  try {
    auto lake = Read(lakePath);
    auto od = Read(objPath);
    std::string objdat(od.begin(), od.end());
    auto secs = ves::Parse(lake);
    for (auto &s : secs) {
      if (s.tag == "DEPTHMAP") depth = ves::ParseGrid(s.payload);
      if (s.tag == "TYPEMAP") {
        typemap = ves::ParseGrid(s.payload);
        hasType = true;
      }
      if (s.tag == "VISUAL_MAP") {
        visual = s.payload;
        hasVisual = true;
      }
      if (s.tag == "GAMETYPES")
        rules = ui::ParseGametypes(s.payload.data(), s.payload.size());
      if (s.tag == "OBJECT_POSITIONS")
        groups = ves::ParseObjPos(s.payload.data(), s.payload.size());
    }
    name = ui::LakeName(secs, lakePath);
    tables = objects::Parse(objdat);
    tab = &tables[0];
    for (auto &t : tables)
      if ((snow && t.name == "SNOW") || (!snow && t.name == "NORMAL"))
        tab = &t;
    size_t nobj = 0;
    for (auto &g : groups) nobj += g.size();
    printf("grid %dx%d objs %zu block %s%s%s\n", depth.w, depth.h, nobj,
           tab->name.c_str(), hasType ? " +typemap" : "",
           hasVisual ? " +visual" : "");
    return true;
  } catch (const std::exception &e) {
    printf("scene load fail (%s), empty scene\n", e.what());
    depth = ves::Grid{};
    typemap = ves::Grid{};
    hasType = hasVisual = false;
    visual.clear();
    groups.clear();
    tables.clear();
    tab = nullptr;
    rules = ui::Rules{};
    name = lakePath ? lakePath : "(none)";
    return false;
  }
}

static void SetBgTexture(SDL_Renderer *r, SDL_Texture *&t,
                         const render::Bg &bg) {
  if (t) {
    SDL_DestroyTexture(t);
    t = nullptr;
  }
  if (r && bg.w > 0 && bg.h > 0 && !bg.rgb.empty()) {
    t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STATIC,
                          bg.w, bg.h);
    if (t) SDL_UpdateTexture(t, nullptr, bg.rgb.data(), 3 * bg.w);
  }
}

// Screen -> port +-128 world (game::Click takes original units, so main
// converts via game::FromPortX/Y). ponytail: the inverse skips the PitchY
// tilt (small y error when zoomed); exact inverse lands with the click
// semantics walk of 0063ca80 (delta 20 open).
static bool ScreenToWorld(float mx, float my, int bgW, int bgH,
                          const render::Camera &cam, int winW, int winH,
                          float &wx, float &wy) {
  if (bgW > 0 && bgH > 0) {
    SDL_FRect src = render::SrcRect(cam, bgW, bgH);
    SDL_FRect dst = render::DstRect(bgW, bgH, winW, winH);
    if (src.w <= 0 || src.h <= 0 || dst.w <= 0 || dst.h <= 0) return false;
    float bx = src.x + (mx - dst.x) / dst.w * src.w;
    float by = src.y + (my - dst.y) / dst.h * src.h;
    wx = bx / (float)bgW * 256.f - 128.f;
    wy = by / (float)bgH * 256.f - 128.f;
  } else {
    wx = mx / (winW > 0 ? (float)winW : 1.f) * 256.f - 128.f;
    wy = my / (winH > 0 ? (float)winH : 1.f) * 256.f - 128.f;
  }
  return true;
}

int main(int argc, char **argv) {
  // Flags filtered out so positional CLI still works.
  bool useBots = true;
  std::vector<const char *> pos;
  for (int i = 1; i < argc; i++)
    if (std::string(argv[i]) == "--no-bots")
      useBots = false;
    else
      pos.push_back(argv[i]);
  const char *lakePath = pos.size() > 0 ? pos[0] : nullptr;
  const char *objPath = pos.size() > 1 ? pos[1] : nullptr;
  bool snow = pos.size() > 2 && atoi(pos[2]) != 0;
  int cap = pos.size() > 3 ? atoi(pos[3]) : 0;

  ves::Grid depth, typemap;
  bool hasType = false, hasVisual = false;
  std::vector<uint8_t> visual;  // raw VISUAL_MAP payload
  std::vector<ves::ObjGroup> groups;
  const objects::ObjTable *tab = nullptr;
  std::vector<objects::ObjTable> tables;
  ui::Rules rules;
  std::string lakeName = "(none)";
  bool cli = lakePath && objPath;
  if (cli) LoadScene(lakePath, objPath, snow, depth, typemap, hasType,
                     hasVisual, visual, groups, tables, tab, rules, lakeName);

  // Background: terrain height-shade over VISUAL_MAP-as-PCX when
  // decodable, else DEPTHMAP palette (+TYPEMAP tint). See terrain.hpp.
  render::Bg bg = terrain::BuildBackground(
      depth, hasType ? &typemap : nullptr, hasVisual ? &visual : nullptr);

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
    printf("sdl init fail: %s (headless ok)\n", SDL_GetError());
    return 0;
  }
  printf("audio driver: %s\n",
         SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "none");
  audio::play::Player snd;
  bool haveAudio = snd.Init();
  int nclip = snd.Scan();
  printf("audio: %s clips %d [click/Arrows move Enter drill Space strike PgUp/PgDn/1-3 line M mute]\n",
         haveAudio ? "on" : "off", nclip);

  int winw = 800, winh = 600;
  if (bg.w && bg.h) {
    winw = 960;
    winh = 960 * bg.h / bg.w;
    if (winh > 960) {
      winh = 960;
      winw = 960 * bg.w / bg.h;
    }
  }
  SDL_Window *w = SDL_CreateWindow("ModernPilkki", winw, winh,
                                   SDL_WINDOW_RESIZABLE);
  if (!w) {
    printf("window fail: %s (headless ok)\n", SDL_GetError());
    SDL_Quit();
    return 0;
  }
  // Headless dummy driver with no explicit cap: run a few tick-only
  // frames then exit instead of hanging forever.
  const char *drv = SDL_GetCurrentVideoDriver();
  if (!cap && drv && std::string(drv) == "dummy") cap = 5;

  SDL_Renderer *r = SDL_CreateRenderer(w, nullptr);
  SDL_Texture *t = nullptr;
  SetBgTexture(r, t, bg);
  // Runtime model bank (extracted/ only, lazy per-class load, dots fallback).
  models::Bank mBank{models::RootFromObjPath(
      objPath ? objPath : "extracted/data/objects.dat")};
  models::TexCache mTex;
  angler::Assets ang;  // HUMSHAD/rod/catch sprites, drawn fallback if missing
  angler::Load(ang, r);

  input::Keys keys;
  game::Game fish;  // offline loop: sim in game.hpp (docs/sim.md deltas)
  // Default = original-exact. newMechanics gates port extras only
  // (wandering fish dots); the old jig speedup + depth-scaled weight were
  // deleted with deltas 8/9.
  fish.newMechanics = false;
  fish.Seed(0x1234);
  fish.SetLake(depth.cells.empty() ? nullptr : &depth,
               hasType ? &typemap : nullptr, &groups);
  ui::Game uim;  // TITLE->LAKE_SELECT->PLAY->PAUSE->RESULTS; CLI skips menus
  ui::Init(uim, ui::ScanLakes("extracted/lakes"));
  menu::Setup setup;  // LakeSelect comp row: time limit + bots
  menu::Back mback;   // title art, gradient fallback when missing
  printf("lakes %zu\n", uim.lakes.size());
  if (cli) ui::BeginPlay(uim, lakeName, rules);
  offline::Comp comp;  // bots + countdown from setup row; --no-bots disables
  size_t compLogSeen = 0;
  if (useBots) {
    auto chat = offline::ParseScript(offline::LoadScriptText());
    comp.Init(offline::Roster(chat), chat, setup.bots, setup.timeSec * 60,
              0xB07);
    printf("bots %zu chat %zu\n", comp.bots.size(), comp.chat.size());
  }
  tick::Driver driver;  // quit protocol; game updates via direct calls
  tick::Clock clock;
  tick::Governor gov;   // 00640370 adaptive frame limiter
  render::Camera cam;
  camera::Fx cfx;
  render::Player pl;
  std::vector<render::Hole> holes;
  bool lineOut = false;
  float lineX = 0, lineY = 0;
  bool run = true;
  int frames = 0;
  float endHoldMs = 0;  // round-end hold before Results (00650fe0 2000ms)
  while (run && (!cap || frames < cap)) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT) {
        run = false;
        driver.quitRequested = true;
      }
      if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
        bool down = e.type == SDL_EVENT_KEY_DOWN;
        int id = input::SdlToId(e.key.key);
        int ext = id;
        if (ext < 0) {  // extras SdlToId lacks: WASD move, E drill, Space jig
          switch (e.key.scancode) {
            case SDL_SCANCODE_W: ext = 0; break;
            case SDL_SCANCODE_S: ext = 1; break;
            case SDL_SCANCODE_A: ext = 2; break;
            case SDL_SCANCODE_D: ext = 3; break;
            case SDL_SCANCODE_E: ext = 6; break;
            case SDL_SCANCODE_SPACE: ext = 8; break;
            default: break;
          }
        }
        // Sim bitmap only in Play; key-up always clears (no stuck bits).
        // Menus read arrows/Enter/Esc/R directly (WASD/E/Space stay in-game).
        if (!down || uim.state == ui::State::Play) {
          if (ext >= 0) input::Set(keys, (uint32_t)ext, down);
        }
        if (down && !e.key.repeat) {
          if (id >= 0 && id <= 7) menu::MenuPress(uim, setup, (uint32_t)id);
          if (e.key.key == SDLK_R) ui::Press(uim, ui::kRestart);
          if (e.key.scancode == SDL_SCANCODE_M) {
            snd.ToggleMute();
            printf("mute %d\n", snd.muted);
          }
          // rod slots 1/2/3 -> line buckets 10/20/30 m (docs/sim.md 4.3)
          if (uim.state == ui::State::Play) {
            if (e.key.key == SDLK_1) fish.SetSlot(0);
            if (e.key.key == SDLK_2) fish.SetSlot(1);
            if (e.key.key == SDLK_3) fish.SetSlot(2);
          }
        }
        if (down) {  // +/- zoom (repeat ok); PgUp/PgDn stay line depth
          switch (e.key.key) {
            case SDLK_MINUS:
            case SDLK_KP_MINUS: camera::Zoom(cam, -1); break;
            case SDLK_EQUALS:
            case SDLK_PLUS:
            case SDLK_KP_PLUS: camera::Zoom(cam, +1); break;
            default: break;
          }
        }
      }
      if (e.type == SDL_EVENT_MOUSE_WHEEL) camera::Zoom(cam, e.wheel.y);
      if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
          e.button.button == SDL_BUTTON_LEFT) {
        menu::Click(uim, setup, (int)e.button.x, (int)e.button.y, winw, winh);
        if (uim.state == ui::State::Play) {  // 0047f530 click-to-move
          float pwx, pwy;
          if (ScreenToWorld((float)e.button.x, (float)e.button.y, bg.w, bg.h,
                            cam, winw, winh, pwx, pwy))
            fish.Click(fish.FromPortX(pwx), fish.FromPortY(pwy));
        }
      }
    }
    if (uim.quit) {
      run = false;
      driver.quitRequested = true;
    }
    clock.Update(SDL_GetTicks());
    snd.Update();
    ui::Step(uim);  // 00640370 pass: run driver phases (steady = no-op)
    if (uim.state == ui::State::Play && uim.timeLeft <= 0) {
      // Round-end hold (game::kEndHoldMs, 00650fe0 end timer) -> Results.
      endHoldMs += (float)clock.delta;
      if (endHoldMs >= (float)game::kEndHoldMs) ui::To(uim, ui::State::Results);
    } else {
      ui::Update(uim, clock.delta / 1000.f);  // Play countdown -> Results
    }
    bool simLive = uim.state == ui::State::Play;  // Pause/menus freeze sim
    if (simLive)  // sim runs in <=999ms slices (00471810), stall-safe
      tick::Slices(clock.delta, [&](uint32_t ms) {
        fish.Update(keys, ms / 1000.f);
        // Flags are per-slice (cleared at Update start), so sfx/tallies
        // are consumed inside the slice loop.
        if (fish.justDrilled) {  // drill sfx; line drops at bore end
          camera::Kick(cfx, 0.35f);
          snd.Drill();
          snd.Splash();
          printf("drilled hole #%zu at (%.0f,%.0f) depth %dcm line %dm\n",
                 fish.holes.size(), fish.x, fish.y,
                 (int)fish.DepthCm(fish.x, fish.y), fish.lineDepth);
        }
        if (fish.justBite) {
          snd.Jig();
          printf("BITE! press SPACE\n");
        }
        if (fish.justCaught) {
          camera::Kick(cfx, 0.5f);
          snd.Catch();
          printf("caught %.2fkg total %d (%.2fkg)\n", fish.lastKg,
                 fish.catches, fish.totalKg);
          if (useBots) comp.AddPlayerCatch((int)(fish.lastKg * 1000));
        }
      });
    // 00488060: line stays out while at a hole with an active lure.
    lineOut = fish.LineOut();
    if (lineOut) {
      double hx, hy;
      if (fish.ActiveHole(hx, hy)) {
        lineX = fish.PortX(hx);
        lineY = fish.PortY(hy);
      }
    }
    if (simLive && useBots) {
      comp.Tick();
      for (; compLogSeen < comp.log.size(); compLogSeen++) {
        auto &c = comp.log[compLogSeen];
        if (c.bot == -2) continue;
        const auto &b = comp.bots[(size_t)c.bot];
        if (!b.bubble.empty()) printf("bot %s\n", b.bubble.c_str());
      }
    }
    if (uim.needLoad && uim.pendIdx >= 0 &&
        (size_t)uim.pendIdx < uim.lakes.size()) {
      std::string lp = uim.lakes[(size_t)uim.pendIdx].path;
      LoadScene(lp.c_str(), objPath ? objPath : "extracted/data/objects.dat",
                snow, depth, typemap, hasType, hasVisual, visual, groups,
                tables, tab, rules, lakeName);
      bg = terrain::BuildBackground(depth, hasType ? &typemap : nullptr,
                                   hasVisual ? &visual : nullptr);
      SetBgTexture(r, t, bg);
      fish.Seed(0x1234);
      fish.SetLake(depth.cells.empty() ? nullptr : &depth,
                   hasType ? &typemap : nullptr, &groups);
      cam = render::Camera{};
      cfx = camera::Fx{};
      pl = render::Player{};
      holes.clear();
      lineOut = false;
      endHoldMs = 0;
      if (useBots) {
        auto names = offline::Roster(comp.chat);
        comp.Init(names, comp.chat, setup.bots, setup.timeSec * 60, 0xB07);
        compLogSeen = 0;
      }
      rules.timeSec = setup.timeSec;  // menu setup row wins over file default
      ui::BeginPlay(uim, lakeName, rules);
    }
    if (uim.needRestart) {  // Results R: same lake, fresh run
      fish.Seed(0x1234);
      fish.SetLake(depth.cells.empty() ? nullptr : &depth,
                   hasType ? &typemap : nullptr, &groups);
      cam = render::Camera{};
      cfx = camera::Fx{};
      pl = render::Player{};
      holes.clear();
      lineOut = false;
      endHoldMs = 0;
      if (useBots) {
        auto names = offline::Roster(comp.chat);
        comp.Init(names, comp.chat, setup.bots, setup.timeSec * 60, 0xB07);
        compLogSeen = 0;
      }
      ui::BeginPlay(uim, uim.lakeName, uim.rules);
    }
    if (driver.Step() == tick::Action::Quit) break;
    // game owns movement/drill (click-to-move, original units); camera
    // lerps in the port +-128 frame via game::PortX/PortY.
    // Zoom: wheel / +/- keys. PgUp/PgDn step the line bucket (game.hpp).
    pl.x = fish.PortX(fish.x);
    pl.y = fish.PortY(fish.y);
    camera::Update(cam, pl.x, pl.y, clock.delta / 1000.f);
    cam.pitch = camera::PitchForZoom(cam.zoom);
    float shx = 0, shy = 0;
    camera::StepShake(cfx, clock.delta / 1000.f, shx, shy);
    render::Camera vc = cam;  // shaken view for this frame
    vc.x += shx;
    vc.y += shy;
    SDL_GetWindowSize(w, &winw, &winh);  // resize: aspect from bg in Draw
    holes.clear();  // render mirrors game holes (single authority)
    for (auto &h : fish.holes)
      holes.push_back({fish.PortX(h.x), fish.PortY(h.y)});
    if (r) {
      terrain::Draw(r, t, bg.w, bg.h, groups, tab, vc, pl, holes, false,
                    0, 0, winw, winh, &mBank, &mTex);
      {  // angler + holes + rod/line + bite + catch (dots fallback inside)
        angler::DrawOverlay(r, ang, vc, bg.w, bg.h, winw, winh, fish, pl.x,
                            pl.y, lineOut, lineX, lineY, simLive,
                            clock.delta / 1000.f, mBank.Human().ok);
      }
      {  // daylight tint + snow sparkle, then HUD on top
        camera::DrawTint(r, winw, winh, uim.timeLeft,
                         (float)uim.rules.timeSec,
                         camera::DaylightAvg(tab));
        camera::Sparkle(r, winw, winh, frames, snow);
      }
      {  // HUD overlay: lake, depth at player, tally, time, hints
        ui::Hud hud;
        hud.holes = (int)fish.holes.size();
        hud.catches = fish.catches;
        hud.kg = fish.totalKg;
        if (!depth.cells.empty())
          hud.depthAt = (int)fish.DepthAt(fish.x, fish.y);  // bilinear cell
        if (uim.state == ui::State::Title ||
            uim.state == ui::State::LakeSelect)
          menu::Draw(r, mback, uim, setup, winw, winh);
        else
          ui::Render(uim, r, hud);
        if (uim.state == ui::State::Results) {  // 004c2ce0 result rows
          char rr[64];
          if (fish.catches == 0) {
            SDL_RenderDebugText(r, 16, winh - 96, "No bites.");
          } else {
            snprintf(rr, sizeof rr, "Kalojen yhteispaino %.1f kg",
                     fish.totalKg);
            SDL_RenderDebugText(r, 16, winh - 96, rr);
            snprintf(rr, sizeof rr, "Isoin kala %.1f kg", fish.biggest);
            SDL_RenderDebugText(r, 16, winh - 84, rr);
          }
        }
        if (useBots && (uim.state == ui::State::Play ||
                        uim.state == ui::State::Results)) {
          auto board = comp.Board();
          char b[96];
          for (size_t i = 0; i < board.size() && i < 4; i++) {
            snprintf(b, sizeof b, "%zu. %s %.1fkg", i + 1,
                     board[i].first.substr(0, 20).c_str(),
                     board[i].second / 1000.f);
            SDL_RenderDebugText(r, 16, winh - 60 + 12 * (float)i, b);
          }
          for (auto &bt : comp.bots)
            if (!bt.bubble.empty()) {
              SDL_RenderDebugText(r, 16, winh - 12,
                                  bt.bubble.substr(0, 80).c_str());
              break;
            }
        }
      }
      SDL_RenderPresent(r);
    }
    if ((frames % 30) == 0) {  // text HUD via title (no font dep)
      char title[192];
      snprintf(title, sizeof title,
               "ModernPilkki (%d,%d) holes %zu line %dm caught %d "
               "(%.1fkg)%s",
               (int)fish.x, (int)fish.y, fish.holes.size(), fish.lineDepth,
               fish.catches, fish.totalKg,
               fish.Biting() ? " BITE-space!" : "");
      SDL_SetWindowTitle(w, title);
    }
    frames++;
    gov.Count();  // 00640370 limiter: adaptive sleep, mode 1
    SDL_Delay((Uint32)gov.Frame((uint32_t)SDL_GetTicks(), 1));
  }
  printf("frames %d keys 0x%x tick-delta %u holes %zu catches %d (%.2fkg) zoom %.2f\n",
         frames, keys.bits, clock.delta, fish.holes.size(), fish.catches,
         fish.totalKg, cam.zoom);
  if (useBots) {
    for (auto &e : comp.Board())
      printf("board %s %d\n", e.first.c_str(), e.second);
    printf("log %s\n", comp.Save() ? "saved" : "save-fail");
  }
  printf("models %zu meshes %zu tris cached\n", mBank.meshes.size(),
         mBank.nTri);
  if (t) SDL_DestroyTexture(t);
  angler::Free(ang);  // before renderer: sprite textures die with it
  mTex.Destroy();  // before renderer: textures die with it
  menu::FreeBack(mback);
  if (r) SDL_DestroyRenderer(r);
  SDL_DestroyWindow(w);
  snd.Close();  // before SDL_Quit: stream destroy after quit corrupts heap
  SDL_Quit();
  return 0;
}
