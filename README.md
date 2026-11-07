<p align="center"><img src="assets/logo.svg" width="96" alt="VIBE FM Link"></p>

# VIBE FM Link

**Your M-VAVE FM-1 as a controller for the [VIBE FM](https://thefirmwarehub.com/vibe), the VIBE FM Web Player and the
VIBE FM VST – and for anything else that takes MIDI.**

Install it on the FM-1 like any firmware from the [FM-1 Firmware Hub](https://thefirmwarehub.com), plug the FM-1 into
the VIBE (or your computer), done: the keys play, the seven encoders are the VIBE's encoders, every button is a VIBE
button. The FM-1 makes no sound of its own in this firmware; the VIBE does. Back to any other firmware: the hub, as
always.

Built on [Dinghy](https://github.com/zvenson/dinghy), the smallest FM-1 firmware to build your own on. GPL-3.0.

## What the FM-1 becomes

| FM-1 | VIBE FM | MIDI (channel 1) |
| --- | --- | --- |
| keys | the keys | notes, velocity 100 |
| OCT- / OCT+ | (the FM-1's own octave) | – |
| KNOB 1-4 | encoders 1-4 | CC 20-23, relative |
| SELECT | DATA | CC 24, relative |
| ALGORITHM | encoder A | CC 25, relative |
| PRESETS | encoder B | CC 26, relative |
| MASTER | the volume | CC 7 |
| EDIT ENV LFO ARP FX SEQ | the six keys SOUND ENV LFO ARP FX SEQ | CC 46 44 45 50 42 51 |
| SEL | ENTER | CC 43 |
| HOME | HOME | CC 48 |
| PLAY, REC | PLAY, REC | CC 52, 53 |
| **SAVE held** | **SHIFT** | CC 49 |
| SAVE + PLAY | STOP | CC 54 |
| SAVE + OCT- / OCT+ | LEFT / RIGHT | CC 56 / 57 |
| **GLO held** + EDIT ENV LFO ARP FX SEQ | **F1-F6** (the soft keys under the display) | CC 60-65 |
| GLO tapped alone | MENU | CC 47 |

Buttons send 127 pressed and 0 let go. Relative encoders send two's complement: +n as n, -n as 128 - n; a fast turn
counts more (2, 3, 6 a detent). A MIDI clock coming in blinks PLAY on the beat, START / STOP light it. The screen shows
the last thing sent.

The VIBE, the Web Player and the VST know this firmware by its USB name, "VIBE FM Link", and map it with the profile
[`controllers/vibe-fm-link.json`](https://github.com/zvenson/vibe-sdk) of the VIBE FM SDK. In a DAW, choose
"VIBE FM Link" under Controller in the plugin window once. Every CC is plain MIDI: any synth or DAW can learn it too.

OberVave 3.4 on the FM-1 has the same thing built in (OUTPUT page, SELECT pressed: VIBE LINK off / on / control), so
the FM-1 can play and control at once.

## Build it yourself

As Dinghy: Linux x86-64 (or Docker), Python 3 with Pillow, the JieLi toolchain (`tools/get_toolchain.sh`) and the
JieLi AC79 SDK checkout (see [Dinghy's README](https://github.com/zvenson/dinghy#2-set-up-once)).

```sh
./build.sh                                           # -> build/vibe-fm-link.fwsc
python3 tools/fm1_install.py build/vibe-fm-link.fwsc # onto the FM-1 over USB (needs mido + python-rtmidi)
tests/run_host.sh                                    # the app on your computer: the screen as PNG
```

The app is one file: [firmware/src/link.c](firmware/src/link.c). Everything under it is Dinghy's platform
([docs/API.md](docs/API.md)). Package identity `FM-1_94N` (N = the minor version).

## License

GPL-3.0-only. The platform is Felucca's (Leo Kuroshita, Hügelton Instruments) as SLOOP (isod89) and Dinghy carry it,
the SMK-37 layer by M. Geisler (genkigenki); VIBE FM Link (the app) by zvenson. Details and credits in
[LICENSING.md](LICENSING.md).
