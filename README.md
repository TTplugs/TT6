> **WIP:** TT6 is an early S2400-focused LV2 instrument prototype. Six-operator altered FM voice. Polyphonic 8-voice engine in active development.

# TT6 for S2400

TT6 is an original 6-operator altered FM synthesizer LV2 plugin for the
Isla Instruments S2400 DSP Card. It is not an emulation of any commercial
instrument and uses no third-party samples, branding, GUI framework, JUCE,
VST, CLAP, or trademarked names. Inspired by classic phase-modulation
synthesis (DX7) and modern altered-FM ideas (ring modulation, wave folding).

## S2400 scope

- LV2 instrument plugin.
- aarch64 / ARM64 Linux only.
- MIDI input, stereo audio output.
- MIDI Atom port at index 0, stereo audio outputs at indices 1 and 2.
- No audio input.
- No GUI extension.
- 31 LV2 control parameters, below the S2400 32-parameter limit.
- Fixed 8-voice polyphony with voice stealing.

## Synth architecture

- 6 operators numbered 1-6, each a sine oscillator with its own
  frequency ratio relative to the played MIDI note, output level,
  and exponential decay envelope.
- Fixed 8-voice poly engine. Note-on allocates a free voice first,
  then a quiet released voice, then oldest voice (steal).
- MIDI is processed sample-accurate inside each audio block.
- 16 fixed algorithms select which operators modulate which.
  Operators are processed in DAG order (op1 first, op6 last).
- Operator 6 has a one-sample-delayed self-feedback path
  driven by the FEEDBACK parameter, allowing controlled noisy
  or saw-like tones.
- Three modulator-carrier interaction modes selected globally:
  - FM (classic phase modulation, like DX7)
  - RingMod (carrier amplitude is multiplied by modulator)
  - WaveFold (carrier sine plus modulator goes through an
    iterative triangle folder)
- Envelope: per-op exponential decay toward a shared SUSTAIN level
  while the note is held, then release to zero on note-off.
  A global ATTACK ramp is applied as a master amp envelope.
- Global state-variable lowpass filter at the output of the
  operator mix.
- LFO modulates pitch with up to +/- 2 semitones depth.
- Drive (tanh soft clip) and Gain (dB) at the end of the chain.

## Parameter list (in port-index order)

| Idx | Symbol           | Name              | Default | Range          |
|----:|------------------|-------------------|--------:|----------------|
|  0  | midi_in          | MIDI In           |    -    | (atom port)    |
|  1  | out_l            | Out L             |    -    | (audio port)   |
|  2  | out_r            | Out R             |    -    | (audio port)   |
|  3  | algo             | ALGO              |    0    | 0..15 enum     |
|  4  | op1_ratio        | OP1_Ratio         |   1.00  | 0.25..32 log   |
|  5  | op1_level        | OP1_Level         |   0.80  | 0..1           |
|  6  | op1_decay        | OP1_Decay (ms)    |  1500   | 1..8000 log    |
|  7  | op2_ratio        | OP2_Ratio         |   1.00  | 0.25..32 log   |
|  8  | op2_level        | OP2_Level         |   0.50  | 0..1           |
|  9  | op2_decay        | OP2_Decay (ms)    |  1200   | 1..8000 log    |
| 10  | op3_ratio        | OP3_Ratio         |   2.00  | 0.25..32 log   |
| 11  | op3_level        | OP3_Level         |   0.45  | 0..1           |
| 12  | op3_decay        | OP3_Decay (ms)    |   900   | 1..8000 log    |
| 13  | op4_ratio        | OP4_Ratio         |   3.00  | 0.25..32 log   |
| 14  | op4_level        | OP4_Level         |   0.35  | 0..1           |
| 15  | op4_decay        | OP4_Decay (ms)    |   700   | 1..8000 log    |
| 16  | op5_ratio        | OP5_Ratio         |   5.00  | 0.25..32 log   |
| 17  | op5_level        | OP5_Level         |   0.25  | 0..1           |
| 18  | op5_decay        | OP5_Decay (ms)    |   500   | 1..8000 log    |
| 19  | op6_ratio        | OP6_Ratio         |   7.00  | 0.25..32 log   |
| 20  | op6_level        | OP6_Level         |   0.15  | 0..1           |
| 21  | op6_decay        | OP6_Decay (ms)    |   350   | 1..8000 log    |
| 22  | attack           | ATTACK (ms)       |   1.00  | 0.1..2000 log  |
| 23  | sustain          | SUSTAIN           |   0.00  | 0..1           |
| 24  | release          | RELEASE (ms)      |   200   | 1..8000 log    |
| 25  | feedback         | FEEDBACK          |   0.00  | 0..1           |
| 26  | op_mode          | OP_MODE           |    0    | FM / Ring / Fold |
| 27  | filter_cutoff    | FILTER_Cutoff (Hz)| 18000   | 20..20000 log  |
| 28  | filter_res       | FILTER_Res        |   0.10  | 0..1           |
| 29  | lfo_rate         | LFO_Rate (Hz)     |   4.00  | 0.05..30 log   |
| 30  | lfo_depth        | LFO_Depth         |   0.00  | 0..1           |
| 31  | drive            | DRIVE             |   0.00  | 0..1           |
| 32  | gain             | GAIN (dB)         |  -6.00  | -24..+12       |
| 33  | vel_amount       | VEL_AMOUNT        |   0.70  | 0..1           |

Total control ports: 31 (within S2400 32 max).

## Algorithm list

```
00 Stack         op1 -> op2 -> op3 -> op4 -> op5 -> op6 (carrier)
01 DoubleStack   op1->op2->op3 (c)  +  op4->op5->op6 (c)
02 Triple2       (op1->op2 c) + (op3->op4 c) + (op5->op6 c)
03 YStack        (op1,op2) -> op3 -> op4 -> op5 -> op6 (c)
04 Branch        op1 -> {op2,op3,op4} all carriers, op5->op6 (c)
05 Fan3          op1 -> {op2,op3,op4}, ops 2..5 carriers
06 Diamond       (op1,op2)->op3, (op3,op4)->op5, ops 5,6 carriers
07 Pair3         (op1->op2 c) + (op3->op4 c) + op5 c + op6 c
08 PyramidA      op1 -> {op2,op3}, (op2,op3)->op4 -> op5 -> op6 (c)
09 PyramidB      op1->op3, op2->op4, (op3,op4)->op5 -> op6 (c)
10 AdditiveAll   6 carriers, no FM
11 OneMod5       op1 modulates ops 2..6 (all carriers)
12 TwoMod4       (op1,op2) modulate ops 3..6 (all carriers)
13 Bell          op1->op2 -> op3 c, op4->op5 c, op6 c
14 EPiano        (op1,op2)->op3 c, (op4,op5)->op6 c
15 Drone         6 carriers, op5 -> op6 (subtle)
```

## Starter patches

A few combinations that work well as starting points:

- **Plucked Tine**: ALGO=07, OP1 ratio=1.0 level=0.9 decay=1500,
  OP3 ratio=14.0 level=0.5 decay=300, OP_MODE=FM, SUSTAIN=0,
  FILTER_Cutoff=12000.

- **Bell**: ALGO=13, OP2 ratio=3.5 level=0.8 decay=2000,
  OP5 ratio=7.0 level=0.7 decay=1500, OP_MODE=FM, SUSTAIN=0,
  FILTER_Cutoff=18000.

- **Warm Pad**: ALGO=10 (additive), all OP levels 0.3-0.6, all decays >5000,
  SUSTAIN=0.7, ATTACK=400, RELEASE=1500, LFO_Rate=0.5, LFO_Depth=0.2.

- **Metallic Lead**: ALGO=11, OP1 ratio=4.99 level=0.6 decay=8000,
  OP_MODE=RingMod, FEEDBACK=0.3, FILTER_Cutoff=8000, FILTER_Res=0.4.

- **Wavefolded Bass**: ALGO=04, OP1 ratio=1.0 level=1.0 decay=8000,
  OP_MODE=WaveFold, OP2-OP4 ratios 0.5/1.0/1.5 levels 0.7/0.5/0.3,
  DRIVE=0.3, FILTER_Cutoff=2000.

## Build (Ubuntu ARM64 VM)

```bash
cd ~/TT6
make clean
make
make check
```

If you are already on the VM and the repo is in `~/TT6`:

```bash
cd ~/TT6
git pull --ff-only
make clean
make
make check
```

## DX7 to TT6 converter

The converter script is included at:

```bash
scripts/dx7_to_tt6.py
```

It reads DX7 SysEx (single voice or 32-voice bank) and outputs TT6
parameters as JSON and/or FXP.

Basic usage:

```bash
python3 scripts/dx7_to_tt6.py INPUT.syx --patch-index 0 --print
python3 scripts/dx7_to_tt6.py INPUT.syx --patch-index 0 \
  --output-json tt6_patch.json \
  --output-fxp TT6_patch.fxp
```

Use a reference FXP (for header fields as saved by S2400 host flow):

```bash
python3 scripts/dx7_to_tt6.py INPUT.syx --patch-index 0 \
  --template-fxp kick01.FXP \
  --output-fxp TT6_patch.fxp \
  --output-json TT6_patch.json
```

Notes:

- Converter keeps TT6 control order exactly as ports 3..33.
- Mapping from DX7 envelope/ratio space to TT6 is heuristic and musical,
  not a bit-identical emulation of DX7 engine behavior.
- `--fxid` and `--fx-version` are available if you want fixed values
  without using `--template-fxp`.

The expected output of `make check`:

- `file` reports `ELF 64-bit LSB shared object, ARM aarch64`
- `nm -D` shows `T lv2_descriptor` exported
- `readelf -d` shows only `libm.so.6` and `libc.so.6` as NEEDED
- `strings` shows only `GLIBC_2.17` and `GLIBC_2.27` symbol versions

If anything else appears (libstdc++, GLIBC_2.38, `__stack_chk_fail`, etc.)
the plugin will likely fail to insert on the S2400.

## Deploy to S2400

Replace `[IP]` with the Unraid box address.

```bash
ssh root@[IP] 'rm -rf "/mnt/user/Musica/Desarrollo LV2/TT6.lv2"'
scp -r TT6.lv2 root@[IP]:'/mnt/user/Musica/Desarrollo LV2/'
```

Alternatively, mount the share from the VM and copy locally:

```bash
sudo apt install -y cifs-utils
sudo mkdir -p /mnt/unraid_musica
sudo mount -t cifs //[IP]/Musica /mnt/unraid_musica \
    -o username=root,uid=$(id -u),gid=$(id -g)
cp -r ~/TT6/TT6.lv2 "/mnt/unraid_musica/Desarrollo LV2/"
sudo umount /mnt/unraid_musica
```

Then on the S2400, copy the bundle from the Unraid share to the device's
LV2 search path through the usual workflow and re-scan plugins.

## Constraints respected (S2400 ABI safety)

- No `fmodf`, no `sqrt`/`sqrtf`, no `fmod`, no exceptions, no RTTI.
- No `libstdc++`, no `pthreads`, no networking, no filesystem.
- Only `sinf`, `tanhf`, `expf`, `powf`, `fabsf` from libm.
- Only `malloc`, `free`, `memset`, `__cxa_finalize` from libc.
- No file I/O in `run()`. No allocation in `run()`. No locks.
- Saturation, NaN/Inf guards (`zap`, `finite_or`, `clamp`)
  applied at every DSP stage.
- Phase wraparound is manual (loop subtract), never `fmodf`.
- Parameter smoothing prevents zipper noise.
- Floats are internally quantized to 0.01 for clean edit feel.
- TTL has no `/* */` comments and ports are single-line for
  conservative RDF parsers.

## Known limitations / roadmap

- DX7 -> TT6 conversion is intentionally approximate and tuned for usable
  starting presets, not strict 1:1 timbral equivalence.
- No mod matrix yet; LFO routes to pitch only.
- No effects after the synth (reverb/delay) intentionally; use a
  separate LV2 effect plugin on the S2400.
- Algorithm definitions are fixed; no user algorithm editor.
- Wave folding is a fixed-depth iterative fold; could be made
  more aggressive in future versions.

## License

GPL-2.0-only. See `LICENSE`.
