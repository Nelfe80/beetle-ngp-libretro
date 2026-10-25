# Beetle NGP Link

Two Neo Geo Pocket (Color) consoles joined by the link cable, in a single libretro core
(`mednafen_ngp_link_libretro`). Both screens are shown side by side (320x152). Player 1 plays
console 1 and player 2 plays console 2, so each side of a netplay session has its own console.
Sound comes from console 1.

## How it works

- The Beetle NGP engine is built with `NGPLINK`. Its serial port (`system_comms_read`, `poll`,
  `write`, empty in the original core) is connected to the wrapper through `ngplink_set_comms`.
  `COMRECIVESTATUS` returns the number of bytes waiting on the cable.
- The engine is compiled once as object files. `link/prefixer.sh` then makes two copies, with
  every global symbol prefixed `a_` or `b_`. Both copies are linked into the wrapper, so each
  console keeps its own variables.
- The wrapper (`link/ngp_link_libretro.c`) runs one frame of each console per `retro_run` and
  carries bytes between them through two 4 KB queues. Everything is deterministic: a save state
  holds both consoles and the bytes still on the cable, padded to a fixed size so netplay accepts
  it.
- The real-time clock is frozen at 2000-01-01, because machines with different clocks would
  desync. Console 2 keeps its flash save in `<game>.flash-p2`.
- The receive interrupt is off by default (games that use the BIOS COM calls, such as
  SNK vs. Capcom: Card Fighters' Clash). Set `NGPLINK_RX_IRQ=1` in the environment to turn it on
  for games that drive the serial port themselves.

## Core options

These options change what a machine shows, plays and writes. They have no effect on emulation, so
two machines in a netplay session can set them differently.

| Option | Values | Use |
|---|---|---|
| `ngplink_display` | `both` (default), `console 1`, `console 2` | One console at its own size (160x152), for example the local player's console in netplay |
| `ngplink_audio` | `console 1` (default), `console 2` | The console whose sound is played |
| `ngplink_save` | `on` (default), `off` | `off` keeps the flash save data of both consoles in memory only. In netplay the guest receives the host's data, which must not replace its local save |

## Build (MSYS2, MinGW 64-bit)

```
cmake -S . -B build-link -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-link
```

The output is `build-link/mednafen_ngp_link_libretro.dll`. RetroArch also needs
`mednafen_ngp_link_libretro.info` in its `info` folder (with `systemid = "neo_geo_pocket"`).
Define `NGPLINK_TRACES` to log cable traffic while debugging.
