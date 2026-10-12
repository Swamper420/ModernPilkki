# Triage — ProPilkki 2 v2.0.7

SHA256 (installer): `e9558e76...f3393a` verified. Extracted 2026-10-12:
`7z x -aoa original/P2Installer_v2.0.7.exe -oextracted/` → 5229 files, 268 MB.

## Binaries

| file | size | subsystem | linker | entry | renderer | audio |
|---|---|---|---|---|---|---|
| ProPilkki2.exe | 4050608 | Win6.0 GUI | 14.44 (VS2022) | 0x0066f076 | d3d11 (`D3D11CreateDeviceAndSwapChain` only import) | DSOUND (ord 1 = DirectSoundCreate) |
| ProPilkki2_dx7.exe | 3668160 | Win5.1 GUI | 12.0 | 0x0063601f | DDRAW (`DirectDrawCreateEx`, `DirectDrawEnumerateExA`) | same |
| RecMergerP2.exe | 2044600 | Win4.0 GUI | 2.25 (ancient, stamp 2055 = bogus) | 0x00401000 | DDRAW | DSOUND |

No SDL. No bundled DLLs (all system-linked). Both game EXEs also import:
WS2_32 (sockets: `getaddrinfo`/`freeaddrinfo`/`WSAAddressToStringA` + ordinals),
IPHLPAPI (`GetAdaptersAddresses`), HID + SETUPAPI (Wiimote support —
strings confirm `WIIMOTE Connected`), WINMM (`timeGetTime/Begin/EndPeriod`).

Embedded HLSL: `Microsoft (R) HLSL Shader Compiler 6.3.9600.16384` (Win 8.1 SDK).
Failure string: `onnistui, ohjelma suljetaan. (D3D11CreateDeviceAndSwapChain FAILED [0x%x])`.

## Subsystems (from strings)

- Netplay: `GameClient` state machine
  (`GAME_CLIENT_{DISCONNECTED,REGISTERING,CONNECTED,RECONNECTING,...}`),
  host/client, `/minver`, `/iphide`, reconnect, deserialize errors.
- Host info: built-in HTTP page (`<title>Propilkki 2 - Host info page</title>`).
- Versions: min client gating (`1.2` …​ current), lake-version compat checks.
- Autoscript: `data/autoscript_example.txt` documents post-competition hooks.

## File formats (magic bytes verified)

| ext | count | magic | notes |
|---|---|---|---|
| .ves (lakes/) | 48 | `Pro Pilkki datafile v002\nCopyright Procyon Products\n<OBJECT_POSI…` | tagged-text, easy |
| .dat (data/) | 2310 | text (`********\r\nNORMAL…`, `Angle Scale Daylight…`) | text configs |
| data/botinfo.dat | — | `plhplkhkmdmdmd…` | obfuscated/xor binary |
| .spr | 1185+28 | `01 02 01 00 3d 00 50 00…` | custom sprite |
| .obj | 843 | `Pro Pilkki Object\0` + texture path (`gfx\txt\hum\HUMSHAD.PCX`) | custom model |
| .pcx | 348+114 | `0A 05` | standard PCX |
| .3ds | 111 | `//   Copyright Procyon Products` | custom-wrapped, not raw 3DS (`4D4D`) |
| .sfo | 196 (2 Ogg + 194 OSND) | `OggS` or `OSND\0` | voice: 2 plain Vorbis, rest chunked like .mus |
| .sfx | 18 | `RIFF/WAVE` | plain PCM16 WAV |
| .mus | 5 | `OSND\0…pmusic` | Ogg payload, custom header — needs strip/parse |
| .txt/.rtf | — | docs | release notes (EN/RU/UA) |

## Decomp target

Primary: `ProPilkki2.exe` (D3D11). Ghidra headless analysis running on it;
`_dx7` is fallback renderer, `RecMergerP2` (replay tool) out of scope.
See `docs/decomp-notes.md`.
