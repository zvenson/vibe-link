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

**The screen shows what you play (0.3).** The VIBE, the Web Player and the VST's window tell the FM-1 the firmware's
page and what every knob is now (VIBE FM SDK 24, a SysEx `F0 7D 56 4C 01 … F7`): the page on top, KNOB 1-4 with name
and value, ALGORITHM and PRESETS below. A knob you turn lights up; a click on the host shows here too. Without a host
the screen keeps the page you last chose (EDIT ENV LFO ARP FX SEQ HOME).

**A converted FM-1 firmware shows its own screen here (0.4).** When the VIBE, the Web Player or the VST runs an FM-1
firmware converted for it (SLOOP, JIANT, FRASIL …), the FM-1 shows that firmware's own 240 x 240 screen, as it would on
an FM-1 by itself (VIBE FM SDK 25: SysEx `F0 7D 56 4C 02 … F7`, only what changed, each piece answered `F0 7D 56 4C 03 F7`;
~75 KB/s). The VIBE's own firmwares (OberVave …) keep the page and the knobs. Three seconds without a picture: the
screen of its own again.

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
([docs/API.md](docs/API.md)). Package identity `FM-1_984N` (N = the minor version; 0.1 reported FM-1_941, a number other firmwares use).

## License

GPL-3.0-only. The platform is Felucca's (Leo Kuroshita, Hügelton Instruments) as SLOOP (isod89) and Dinghy carry it,
the SMK-37 layer by M. Geisler (genkigenki); VIBE FM Link (the app) by zvenson. Details and credits in
[LICENSING.md](LICENSING.md).
