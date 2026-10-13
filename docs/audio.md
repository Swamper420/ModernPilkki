# Audio routing — ProPilkki 2 original → ModernPilkki SDL port

All addresses `extracted/ProPilkki2.exe` (x86 VS2022). Ghidra project
`/tmp/opencode/ghidra-audio`. Anchors cross-check `docs/decomp-notes.md`.

## Function table

| addr | role |
|---|---|
| 00605070 | engine init calls **0062c820** (sound manager singleton, `DAT_007c8398`, 0x19c8 B) |
| 0062c820 | `Sound::Create`: once-only, allocs manager, calls ctor `00624ea0` |
| 00624ea0 | **DirectSound init**: `Ordinal_1`(=`DirectSoundCreate`, 0 args), `SetCooperativeLevel(hwnd, DSSCL_PRIORITY=2)` via vtable +0x18, primary buffer via `CreateSoundBuffer` (+0xc) with WAVEFORMATEX `0x20001 / 44100 / 176400 (0x2b110) / block 4 / 16 bit` (PCM 44.1 kHz 16-bit stereo; `SetFormat` +0x38; failure = warning only), then builds clip table via 006267f0 |
| 006267f0 | **clip registry**: 1000 slots at `mgr+8+4*i`; each a 0x128-B Clip: path[0x100], +0x100 pan-bias double (0.6/0.7/0.8/0.85/0.9/0.916/0.995), +0x108 src, +0x10c loop mode, +0x110/+0x114 volL/volR permille, +0x118 crossfade param, +0x120 loopStart PCM byte offset |
| 0059feb0 | **Ogg loader** (WAV path is `CWaveSoundRead::vftable` 00660450, open `00660590`/`006606d0`): `ov_open` (00662a80) on a file handle from 0059c040; `ov_info`+`ov_pcm_total` -> builds WAVEFORMATEX (PCM16), byte size = freq × blocks × 2; fills decode object +0x3dc=1. Used by .sfo, .mus AND the temp-WAV transcoder |
| 005a0030 | "restart decoder" wrapper (close + reopen same path); logs `File not an ogg-format:` on failure. Both 0059feb0 and 005a0030 are **one loader**, per the vtable pair at 73fe14/73fe18 |
| 00401870 | **OSND page demuxer**: pulls next page from a stream buffer; accepts magic `OSND` (0x444e534f) or plain `OggS` (DAT_00777888 at the compare), verifies CRC (004016a0), resyncs via `memchr('O')` on mismatch. This is why the 2 non-OSND `.sfo` (Tuuli, Tuuli2 — plain `OggS`) route through the same 0059feb0 loader: same vorbisfile, page magic simply differs |
| 00661f70 / 006622e0 | vorbisfile read/seek callbacks that call 00401870 — the OSND-vs-Ogg difference is fully absorbed here |
| 006258b0 | Ogg→WAV temp transcoder: decode whole clip via 0059feb0, write RIFF file, `Sleep(1)` per chunk + progress (00650d20) — the original pre-decodes Ogg to temp WAV on first run (called from 00625b50 for a fixed slot list, then the clip table points at the temp file). Streaming decode exists too but the game prefers PCM temp files |
| 0062d020 | **PlayOneShot** = `0062ccb0(slot, panL, panR, vol, flags, 0, 0)` wrapper |
| 0062ccb0 | **play request**: guard `voices < 0x40` (64-voice cap), alloc voice (0x78 B) + channel (0x70 B), `00624d20` picks decoder by clip+0x108 (0/other=`CWaveSoundRead` 0x2c B, 1=`COggSoundRead` 0x3e0 B), `0062ca80` creates secondary DS buffer (DSCAPS CTRLVOLUME|CTRLPAN|CTRLNOTIFY 0x180c0, size = PCM bytes ×2 or full if smaller, loop flag -> `Play(0,0,1)` static loop else notify-driven streaming), channel state 0x18/0x19 = clip vols 2000/2000, +0x68 loop flag. Clip loop mode 2 + caller loop flag -> dual-voice crossfade setup (channel +0x1a=1, vols from clip +0x110/+0x114) |
| 006251e0 | **per-frame mixer pump** (called from 0062dbd0; also direct from 00642d70/005d9480): for each active voice: getstatus (+0x24), lock half-buffer (+0x2c), pull PCM via decoder vtable +0xc (read) until half full, silence-fill on end (0x80 for 8-bit else 0), loop rewind via decoder +8/+0x18 (seek loop point = voice +0x10 = clip loopStart), stop+release voice on done (`0062d280`, notify events at mgr+0x13bc) |
| 0062dbd0 | **frame update**: calls 006251e0, then per channel recomputes volume = fade(permille 0..1000 at +0x1c) × clip volL/R × userVol(+0x30) × 1e-6 (DAT_006dd2a0) → `SetVolume`/`SetPan` (vtable +0x3c/+0x40, cB scale ±10000, master at mgr+0x19c0). Called from the frame tick 00605eb0 update stage (00605fde) — this is the whole per-frame audio work; DirectSound hardware mixes |
| 006257f0 | **FadeOut(handle, ms)**: channel state 1, timer "Sound" (00650fe0, driven per-frame by 00651620) ramps permille 1000→0, then `0062d6a0` stops/releases |
| 0062c9f0 | **FadeTo(handle, vol, sec)** (state 3) — menu→game music duck |
| 0062d410 | **FadeIn/SeekStart** (state 2: permille→0 then restart) |
| 006256e0 | **Restart** (Play from buffer start, state 0) |
| 0062c900 | **StopHard** (Stop + permille=0) |
| 0062d320 | **Kill** (release voices + channel + timers immediately) |
| 0062c890 / 00626730 / 006266b0 | IsPlaying / current position (ms) |
| 0062d0b0 / 0062d0e0 / 0062d170 | master volume (mgr+0x19c0, clamped) / channel volume / channel volume + immediate apply |
| 00626630 | shutdown: 00626450 teardown + release DirectSound (called from 006062d0, app exit) |
| 005c8270 | **music track select** (table `DAT_007a60d0 = {-1, 90, 93, 94}`): per-lake style = `*(obj+0xf0)` via 005916a0; style 0 -> `rand(3)` (0059db20) over {P2Menu, P2Menu2, P2Menu3}; style 1..3 -> fixed; else default 90 |
| 005c0d50 | results screen: plays slot 0x5b (91 P2Results), fades out old music via 0062c9f0(handle, 2000, 0) — 2000 ms duck |
| 005dcb10 / 005dece0 | load screen: plays slot 0x5c (92 P2Load), 2000 ms fades both directions (006257f0 on re-entry) |
| 004c1770 | **3D positional play** (entity sounds): listener distance → pan/vol via 004c0c60, then 0062ccb0 + optional fade 006257f0 |
| 004c1390 / 004c15a0 / 004c19f0 | sound-group system: group → 100-variant slot table at `obj+0x24+(i+g*100)*4`, random variant (0059db20), voice steal when full (stop nearest, 0062d320/0062d410) |
| 004c0a70 | positional-queue pump (every 2nd call), picks loudest pending event |
| 005a94e0 / 005d90f0 | menu music enter/exit: play/stop slot from 005c8270, handle cached at `DAT_007c3f3c` |
| 00595220 | test-harness play (mouse-driven, not gameplay) |

## Routing spec

One DirectSound device, one clip registry, three loaders:

```
.sfx  RIFF/WAVE  -> CWaveSoundRead  (00660590)  -> PCM16 -> DS secondary buffer
.sfo  OSND/Ogg   -> COggSoundRead   (0059feb0)  -> ^ same path; 00401870 demux
                                              accepts OSND and OggS magics
.mus  OSND       -> COggSoundRead               -> ^ (music slots 90..94)
```

- **The 2 non-OSND .sfo** (Ambient/Tuuli.sfo, Tuuli2.sfo = plain `OggS`)
  need no special case: 00401870's magic check accepts `OggS` alongside
  `OSND`, everything downstream is identical. Slot table marks them src=1
  (Ogg) just like the 194 OSND ones.
- Music = slots 90 (P2Menu), 91 (P2Results), 92 (P2Load), 93 (P2Menu2),
  94 (P2Menu3). Ambient loops: 23/24 wind, 25 pulse, 27 kuikka, 28 bassloop.
  Drill = 50 (KairausPlain.sfo, crossfade loop from byte 308700),
  reel-in = 51 (NostoPlain.sfx WAV), bottom-hit = 52 (Pohja.sfx),
  cast/splash = 54..59 (pilkki_veteen_01..06.sfx, random variant),
  footsteps = 70..73. Human talk = slots 100+ (voice lines, same Ogg loader).
- Loop modes: 0 one-shot, 1 static loop (`DSBPLAY_LOOPING`, whole clip in
  buffer), 2 **crossfade loop** — two voices per channel, second voice's
  loopStart (+0x120) spliced with per-ear vols (+0x110/+0x114) and pan
  (+0x118); used by wind/drill/menu-music so the loop seam is inaudible.
- Volumes are permille (2000 = nominal), fades ramp the channel permille
  0..1000 through the 00651620 timer/fade driver ("Sound" timers), applied
  per-frame by 0062dbd0 as DirectSound cB SetVolume/SetPan.
- 64 concurrent voices max; sound groups (004c1390) steal by listener
  distance when full.

## Per-frame pump (original)

Frame tick 00605eb0 update stage calls **0062dbd0** once: stream-refill
every active voice's half-buffer (decoder read + silence fill + loop
seek), then recompute every channel's volume/pan and push SetVolume/SetPan.
No audio thread; DirectSound mixes on its own hardware/wave-rt path. The
Ogg decode is pull-based from the frame (or from 006258b0's one-time temp
transcode, which the game prefers for clips in its fixed list).

## SDL port mapping (src/audio.hpp, src/audio_play.hpp)

Public `Player` API unchanged (Init/Scan/Update/Drill/Splash/Jig/Catch/
ToggleMute/Close) — `src/main.cpp` untouched.

| original | port |
|---|---|
| clip registry 006267f0 | `audio::kClips[]` + `ClipBySlot()` — slot id, path, src, loop mode, vols, loopStart (verified vs decompile dump in test_audio) |
| music select 005c8270 | `audio::MusicTrackForStyle(style, rng)` (fixed 1..3, random 0) |
| cast variants 54..59 | `audio::SplashVariant(rng)`; `Player::Splash` picks one of 6 loaded WAVs |
| reel-in slot 51 | `Player::Catch` plays NostoPlain.sfx, jingle fallback |
| drill slot 50 (Ogg) | `Player::Drill` synth growl — **no Vorbis decoder without new deps**; OSND rebuild (`OsndToOgg`) exists for external tools |
| per-frame pump 0062dbd0 | `Player::Update` (re-queue ambient loop when stream runs low) |
| fades/pan/multi-voice | not ported (SDL3 mixer would be a new dep); one mono stream, no crossfade |
| Ogg→WAV temp transcode 006258b0 | not needed (no in-game Ogg playback yet) |

Skipped: voice/talk playback, crossfade loops, 3D positional panning — add
when a Vorbis decoder is accepted as a dep (stb_vorbis single header is the
obvious candidate; then OSND .sfo/.mus play through `OsndToOgg`+decode and
`kClips` slot numbers plug straight into trigger sites).
