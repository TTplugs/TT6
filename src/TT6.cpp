/*
 * TT6 - 6-operator altered FM synthesizer for Isla S2400
 * TTLab - GPL-2.0-only
 *
 * Original DSP. Inspired by classic 6-operator FM synthesis
 * (phase modulation, ring modulation, wave folding).
 * Not affiliated with or endorsed by Yamaha or Korg.
 * No trademarked names, GUI, or samples are used.
 */

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lv2/core/lv2.h"
#include "lv2/atom/atom.h"
#include "lv2/atom/util.h"

static const char* kUri = "urn:asier:lv2:tt6";

enum PortIndex : uint32_t {
  MIDI_IN = 0,
  OUT_L,
  OUT_R,
  ALGO,
  OP1_RATIO, OP1_LEVEL, OP1_DECAY,
  OP2_RATIO, OP2_LEVEL, OP2_DECAY,
  OP3_RATIO, OP3_LEVEL, OP3_DECAY,
  OP4_RATIO, OP4_LEVEL, OP4_DECAY,
  OP5_RATIO, OP5_LEVEL, OP5_DECAY,
  OP6_RATIO, OP6_LEVEL, OP6_DECAY,
  ATTACK,
  SUSTAIN,
  RELEASE,
  FEEDBACK,
  OP_MODE,
  FILTER_CUTOFF,
  FILTER_RES,
  LFO_RATE,
  LFO_DEPTH,
  DRIVE,
  GAIN,
  VEL_AMOUNT
};

enum ParamIndex : uint32_t {
  P_ALGO = 0,
  P_OP1_RATIO, P_OP1_LEVEL, P_OP1_DECAY,
  P_OP2_RATIO, P_OP2_LEVEL, P_OP2_DECAY,
  P_OP3_RATIO, P_OP3_LEVEL, P_OP3_DECAY,
  P_OP4_RATIO, P_OP4_LEVEL, P_OP4_DECAY,
  P_OP5_RATIO, P_OP5_LEVEL, P_OP5_DECAY,
  P_OP6_RATIO, P_OP6_LEVEL, P_OP6_DECAY,
  P_ATTACK,
  P_SUSTAIN,
  P_RELEASE,
  P_FEEDBACK,
  P_OP_MODE,
  P_FILTER_CUTOFF,
  P_FILTER_RES,
  P_LFO_RATE,
  P_LFO_DEPTH,
  P_DRIVE,
  P_GAIN,
  P_VEL_AMOUNT,
  kParamCount
};

struct ParamDef {
  float def;
  float min;
  float max;
};

static const ParamDef kParamDefs[kParamCount] = {
  {0.0f, 0.0f, 15.0f},          // ALGO
  {1.0f, 0.25f, 32.0f},         // OP1_Ratio
  {0.80f, 0.0f, 1.0f},          // OP1_Level
  {1500.0f, 1.0f, 8000.0f},     // OP1_Decay
  {1.0f, 0.25f, 32.0f},         // OP2_Ratio
  {0.50f, 0.0f, 1.0f},          // OP2_Level
  {1200.0f, 1.0f, 8000.0f},     // OP2_Decay
  {2.0f, 0.25f, 32.0f},         // OP3_Ratio
  {0.45f, 0.0f, 1.0f},          // OP3_Level
  {900.0f, 1.0f, 8000.0f},      // OP3_Decay
  {3.0f, 0.25f, 32.0f},         // OP4_Ratio
  {0.35f, 0.0f, 1.0f},          // OP4_Level
  {700.0f, 1.0f, 8000.0f},      // OP4_Decay
  {5.0f, 0.25f, 32.0f},         // OP5_Ratio
  {0.25f, 0.0f, 1.0f},          // OP5_Level
  {500.0f, 1.0f, 8000.0f},      // OP5_Decay
  {7.0f, 0.25f, 32.0f},         // OP6_Ratio
  {0.15f, 0.0f, 1.0f},          // OP6_Level
  {350.0f, 1.0f, 8000.0f},      // OP6_Decay
  {1.0f, 0.10f, 2000.0f},       // ATTACK
  {0.0f, 0.0f, 1.0f},           // SUSTAIN
  {200.0f, 1.0f, 8000.0f},      // RELEASE
  {0.0f, 0.0f, 1.0f},           // FEEDBACK
  {0.0f, 0.0f, 2.0f},           // OP_MODE
  {18000.0f, 20.0f, 20000.0f},  // FILTER_Cutoff
  {0.10f, 0.0f, 1.0f},          // FILTER_Res
  {4.0f, 0.05f, 30.0f},         // LFO_Rate
  {0.0f, 0.0f, 1.0f},           // LFO_Depth
  {0.0f, 0.0f, 1.0f},           // DRIVE
  {-6.0f, -24.0f, 12.0f},       // GAIN
  {0.70f, 0.0f, 1.0f}           // VEL_AMOUNT
};

/* Algorithm table: 16 classic 6-operator FM topologies.
 * carriers: bit i set means op i is a carrier (its output enters the mix).
 * mod_in[i]: bitmask of which ops modulate op i. Only bits j < i should be
 * set to maintain DAG processing order. Op 5 supports self-feedback via
 * the FEEDBACK parameter (using the previous sample). */
struct AlgoSpec {
  uint8_t carriers;
  uint8_t mod_in[6];
};

static const AlgoSpec kAlgos[16] = {
  /* 0  Stack          */ { 0x20, { 0, 0x01, 0x02, 0x04, 0x08, 0x10 } },
  /* 1  DoubleStack    */ { 0x24, { 0, 0x01, 0x02, 0,    0x08, 0x10 } },
  /* 2  Triple2        */ { 0x2A, { 0, 0x01, 0,    0x04, 0,    0x10 } },
  /* 3  YStack         */ { 0x20, { 0, 0,    0x03, 0x04, 0x08, 0x10 } },
  /* 4  Branch         */ { 0x2E, { 0, 0x01, 0x01, 0x01, 0,    0x10 } },
  /* 5  Fan3           */ { 0x3E, { 0, 0x01, 0x01, 0x01, 0,    0    } },
  /* 6  Diamond        */ { 0x30, { 0, 0,    0x03, 0,    0x0C, 0    } },
  /* 7  Pair3          */ { 0x3A, { 0, 0x01, 0,    0x04, 0,    0    } },
  /* 8  PyramidA       */ { 0x20, { 0, 0x01, 0x01, 0x06, 0x08, 0x10 } },
  /* 9  PyramidB       */ { 0x20, { 0, 0,    0x01, 0x02, 0x0C, 0x10 } },
  /* 10 AdditiveAll    */ { 0x3F, { 0, 0,    0,    0,    0,    0    } },
  /* 11 OneMod5        */ { 0x3E, { 0, 0x01, 0x01, 0x01, 0x01, 0x01 } },
  /* 12 TwoMod4        */ { 0x3C, { 0, 0,    0x03, 0x03, 0x03, 0x03 } },
  /* 13 Bell           */ { 0x34, { 0, 0x01, 0x02, 0,    0x08, 0    } },
  /* 14 EPiano         */ { 0x24, { 0, 0,    0x03, 0,    0,    0x18 } },
  /* 15 Drone          */ { 0x3F, { 0, 0,    0,    0,    0,    0x10 } }
};

struct Plugin {
  float sr;
  float* out_l;
  float* out_r;
  const float* controls[kParamCount];
  const LV2_Atom_Sequence* midi_in;

  float smooth[kParamCount];

  /* per-op state */
  float phase[6];
  float op_env[6];
  float op_prev[6];

  /* envelope state */
  float attack_phase;
  bool gate;
  int held_note;

  /* base pitch from MIDI */
  float base_hz;

  /* velocity */
  float velocity_gain;

  /* filter state (state-variable LP) */
  float svf_lp;
  float svf_bp;

  /* LFO */
  float lfo_phase;
};

static constexpr float kPi = 3.14159265358979323846f;
static constexpr float kTwoPi = 6.28318530717958647692f;
static constexpr float kRefSampleRate = 48000.0f;
static constexpr float kModIndexScale = 6.2831853f;  /* 2pi for one cycle per unit modulator */

static bool finite_float(float x) {
  return __builtin_isfinite(x);
}

static bool finite_double(double x) {
  return __builtin_isfinite(x);
}

static float finite_or(float x, float fallback) {
  return finite_float(x) ? x : fallback;
}

static float clamp(float x, float lo, float hi) {
  x = finite_or(x, lo);
  return x < lo ? lo : (x > hi ? hi : x);
}

static float sample_rate(const Plugin* p) {
  return (p && finite_float(p->sr) && p->sr >= 1000.0f && p->sr <= 384000.0f) ? p->sr : kRefSampleRate;
}

static float zap(float x) {
  x = finite_or(x, 0.0f);
  return fabsf(x) < 1.0e-20f ? 0.0f : x;
}

static float wrap_phase(float x) {
  if (!finite_float(x)) return 0.0f;
  while (x >= kTwoPi) x -= kTwoPi;
  while (x < 0.0f) x += kTwoPi;
  return x;
}

static float semitone_ratio(float semitones) {
  return finite_or(powf(2.0f, semitones / 12.0f), 1.0f);
}

static float db_to_gain(float db) {
  return finite_or(powf(10.0f, db / 20.0f), 1.0f);
}

static float quantize_2(float x) {
  x = finite_or(x, 0.0f);
  const float scaled = x * 100.0f;
  const int32_t rounded = static_cast<int32_t>(scaled + (scaled >= 0.0f ? 0.5f : -0.5f));
  return static_cast<float>(rounded) * 0.01f;
}

static int nearest_int(float x) {
  x = finite_or(x, 0.0f);
  return static_cast<int>(x + (x >= 0.0f ? 0.5f : -0.5f));
}

static float time_coef_ms(const Plugin* p, float ms) {
  const float sr = sample_rate(p);
  ms = clamp(ms, 0.1f, 10000.0f);
  return finite_or(expf(-1.0f / (0.001f * ms * sr)), 0.0f);
}

static float soft_clip(float x) {
  x = finite_or(x, 0.0f);
  return finite_or(tanhf(x), 0.0f);
}

static float wavefold(float x) {
  x = finite_or(x, 0.0f);
  /* Iterative triangle fold around +/-1.0. Bounded loop for safety. */
  for (int k = 0; k < 6; ++k) {
    if (x > 1.0f) x = 2.0f - x;
    else if (x < -1.0f) x = -2.0f - x;
    else break;
  }
  return clamp(x, -1.0f, 1.0f);
}

static float note_to_hz(int note) {
  if (note < 0) note = 0;
  if (note > 127) note = 127;
  return finite_or(440.0f * semitone_ratio(static_cast<float>(note - 69)), 440.0f);
}

static void init_plugin(Plugin* p, float sr) {
  memset(p, 0, sizeof(Plugin));
  p->sr = sr;
  p->velocity_gain = 1.0f;
  p->base_hz = 220.0f;
  p->held_note = -1;
  p->gate = false;
  for (uint32_t i = 0; i < kParamCount; ++i) {
    p->controls[i] = nullptr;
    p->smooth[i] = kParamDefs[i].def;
  }
}

static void reset_dsp_state(Plugin* p) {
  if (!p) return;
  for (int i = 0; i < 6; ++i) {
    p->phase[i] = 0.0f;
    p->op_env[i] = 0.0f;
    p->op_prev[i] = 0.0f;
  }
  p->attack_phase = 0.0f;
  p->gate = false;
  p->held_note = -1;
  p->velocity_gain = 1.0f;
  p->svf_lp = 0.0f;
  p->svf_bp = 0.0f;
  p->lfo_phase = 0.0f;
}

static void trigger(Plugin* p, int note, float velocity) {
  if (!p) return;
  velocity = clamp(velocity, 0.0f, 1.0f);
  const float vel_amt = clamp(p->smooth[P_VEL_AMOUNT], 0.0f, 1.0f);
  p->velocity_gain = clamp((1.0f - vel_amt) + velocity * vel_amt, 0.0f, 1.0f);
  p->held_note = note;
  p->base_hz = note_to_hz(note);
  p->gate = true;
  p->attack_phase = 0.0f;
  for (int i = 0; i < 6; ++i) {
    p->op_env[i] = 1.0f;
    p->phase[i] = 0.0f;
  }
  /* Do NOT zero op_prev to avoid clicks if a previous note was decaying;
   * feedback path will simply continue smoothly. */
}

static void release_note(Plugin* p, int note) {
  if (!p) return;
  /* Only release if the released note matches the held one (mono) */
  if (note == p->held_note || note < 0) {
    p->gate = false;
    p->held_note = -1;
  }
}

static void handle_midi(Plugin* p) {
  if (!p || !p->midi_in) return;
  if (p->midi_in->atom.size < 8u) return;

  LV2_ATOM_SEQUENCE_FOREACH(p->midi_in, ev) {
    const uint8_t* msg = reinterpret_cast<const uint8_t*>(ev + 1);
    if (ev->body.size < 3u) continue;
    const uint8_t status = msg[0] & 0xF0u;
    if (status == 0x90u && msg[2] > 0u) {
      trigger(p, static_cast<int>(msg[1]),
              clamp(static_cast<float>(msg[2]) / 127.0f, 0.0f, 1.0f));
    } else if (status == 0x80u || (status == 0x90u && msg[2] == 0u)) {
      release_note(p, static_cast<int>(msg[1]));
    }
  }
}

/* State-variable filter, LP output only. Stable and cheap. */
static float process_filter(Plugin* p, float x, float cutoff_hz, float resonance) {
  const float sr = sample_rate(p);
  x = finite_or(x, 0.0f);
  cutoff_hz = clamp(cutoff_hz, 10.0f, sr * 0.45f);
  resonance = clamp(resonance, 0.0f, 1.0f);

  const float f = clamp(2.0f * sinf(kPi * cutoff_hz / sr), 0.00001f, 0.95f);
  const float damping = clamp(1.55f - resonance * 1.45f, 0.10f, 1.55f);

  p->svf_lp += f * p->svf_bp;
  const float hp = x - p->svf_lp - damping * p->svf_bp;
  p->svf_bp += f * hp;

  p->svf_lp = zap(clamp(p->svf_lp, -8.0f, 8.0f));
  p->svf_bp = zap(clamp(p->svf_bp, -8.0f, 8.0f));

  return finite_or(p->svf_lp, 0.0f);
}

static LV2_Handle instantiate(const LV2_Descriptor*, double rate, const char*, const LV2_Feature* const*) {
  Plugin* p = static_cast<Plugin*>(calloc(1, sizeof(Plugin)));
  if (!p) return nullptr;
  const float sr = (finite_double(rate) && rate >= 1000.0 && rate <= 384000.0) ? static_cast<float>(rate) : kRefSampleRate;
  init_plugin(p, sr);
  return p;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
  Plugin* p = static_cast<Plugin*>(instance);
  if (!p) return;

  switch (port) {
    case MIDI_IN: p->midi_in = static_cast<const LV2_Atom_Sequence*>(data); break;
    case OUT_L: p->out_l = static_cast<float*>(data); break;
    case OUT_R: p->out_r = static_cast<float*>(data); break;
    case ALGO: p->controls[P_ALGO] = static_cast<const float*>(data); break;
    case OP1_RATIO: p->controls[P_OP1_RATIO] = static_cast<const float*>(data); break;
    case OP1_LEVEL: p->controls[P_OP1_LEVEL] = static_cast<const float*>(data); break;
    case OP1_DECAY: p->controls[P_OP1_DECAY] = static_cast<const float*>(data); break;
    case OP2_RATIO: p->controls[P_OP2_RATIO] = static_cast<const float*>(data); break;
    case OP2_LEVEL: p->controls[P_OP2_LEVEL] = static_cast<const float*>(data); break;
    case OP2_DECAY: p->controls[P_OP2_DECAY] = static_cast<const float*>(data); break;
    case OP3_RATIO: p->controls[P_OP3_RATIO] = static_cast<const float*>(data); break;
    case OP3_LEVEL: p->controls[P_OP3_LEVEL] = static_cast<const float*>(data); break;
    case OP3_DECAY: p->controls[P_OP3_DECAY] = static_cast<const float*>(data); break;
    case OP4_RATIO: p->controls[P_OP4_RATIO] = static_cast<const float*>(data); break;
    case OP4_LEVEL: p->controls[P_OP4_LEVEL] = static_cast<const float*>(data); break;
    case OP4_DECAY: p->controls[P_OP4_DECAY] = static_cast<const float*>(data); break;
    case OP5_RATIO: p->controls[P_OP5_RATIO] = static_cast<const float*>(data); break;
    case OP5_LEVEL: p->controls[P_OP5_LEVEL] = static_cast<const float*>(data); break;
    case OP5_DECAY: p->controls[P_OP5_DECAY] = static_cast<const float*>(data); break;
    case OP6_RATIO: p->controls[P_OP6_RATIO] = static_cast<const float*>(data); break;
    case OP6_LEVEL: p->controls[P_OP6_LEVEL] = static_cast<const float*>(data); break;
    case OP6_DECAY: p->controls[P_OP6_DECAY] = static_cast<const float*>(data); break;
    case ATTACK: p->controls[P_ATTACK] = static_cast<const float*>(data); break;
    case SUSTAIN: p->controls[P_SUSTAIN] = static_cast<const float*>(data); break;
    case RELEASE: p->controls[P_RELEASE] = static_cast<const float*>(data); break;
    case FEEDBACK: p->controls[P_FEEDBACK] = static_cast<const float*>(data); break;
    case OP_MODE: p->controls[P_OP_MODE] = static_cast<const float*>(data); break;
    case FILTER_CUTOFF: p->controls[P_FILTER_CUTOFF] = static_cast<const float*>(data); break;
    case FILTER_RES: p->controls[P_FILTER_RES] = static_cast<const float*>(data); break;
    case LFO_RATE: p->controls[P_LFO_RATE] = static_cast<const float*>(data); break;
    case LFO_DEPTH: p->controls[P_LFO_DEPTH] = static_cast<const float*>(data); break;
    case DRIVE: p->controls[P_DRIVE] = static_cast<const float*>(data); break;
    case GAIN: p->controls[P_GAIN] = static_cast<const float*>(data); break;
    case VEL_AMOUNT: p->controls[P_VEL_AMOUNT] = static_cast<const float*>(data); break;
  }
}

static void activate(LV2_Handle instance) {
  reset_dsp_state(static_cast<Plugin*>(instance));
}

static void run(LV2_Handle instance, uint32_t n) {
  Plugin* p = static_cast<Plugin*>(instance);
  if (!p || !p->out_l || !p->out_r) return;

  handle_midi(p);

  const float sr = sample_rate(p);
  const float smooth_coeff = 1.0f - time_coef_ms(p, 8.0f);

  /* Per-op decay coefficients computed once per block */
  const float decay_coef[6] = {
    time_coef_ms(p, p->smooth[P_OP1_DECAY]),
    time_coef_ms(p, p->smooth[P_OP2_DECAY]),
    time_coef_ms(p, p->smooth[P_OP3_DECAY]),
    time_coef_ms(p, p->smooth[P_OP4_DECAY]),
    time_coef_ms(p, p->smooth[P_OP5_DECAY]),
    time_coef_ms(p, p->smooth[P_OP6_DECAY])
  };
  const float release_coef = time_coef_ms(p, p->smooth[P_RELEASE]);
  const float attack_coef = time_coef_ms(p, p->smooth[P_ATTACK]);

  /* Resolve enum params once per block */
  int algo_idx = nearest_int(p->smooth[P_ALGO]);
  if (algo_idx < 0) algo_idx = 0;
  if (algo_idx > 15) algo_idx = 15;
  const AlgoSpec& algo = kAlgos[algo_idx];

  int mode_idx = nearest_int(p->smooth[P_OP_MODE]);
  if (mode_idx < 0) mode_idx = 0;
  if (mode_idx > 2) mode_idx = 2;

  const float feedback = clamp(p->smooth[P_FEEDBACK], 0.0f, 1.0f) * 0.9f;
  const float sustain = clamp(p->smooth[P_SUSTAIN], 0.0f, 1.0f);
  const float lfo_hz = clamp(p->smooth[P_LFO_RATE], 0.05f, 30.0f);
  const float lfo_depth = clamp(p->smooth[P_LFO_DEPTH], 0.0f, 1.0f);

  /* Op ratios cached */
  const float op_ratio[6] = {
    clamp(p->smooth[P_OP1_RATIO], 0.25f, 32.0f),
    clamp(p->smooth[P_OP2_RATIO], 0.25f, 32.0f),
    clamp(p->smooth[P_OP3_RATIO], 0.25f, 32.0f),
    clamp(p->smooth[P_OP4_RATIO], 0.25f, 32.0f),
    clamp(p->smooth[P_OP5_RATIO], 0.25f, 32.0f),
    clamp(p->smooth[P_OP6_RATIO], 0.25f, 32.0f)
  };
  const float op_level[6] = {
    clamp(p->smooth[P_OP1_LEVEL], 0.0f, 1.0f),
    clamp(p->smooth[P_OP2_LEVEL], 0.0f, 1.0f),
    clamp(p->smooth[P_OP3_LEVEL], 0.0f, 1.0f),
    clamp(p->smooth[P_OP4_LEVEL], 0.0f, 1.0f),
    clamp(p->smooth[P_OP5_LEVEL], 0.0f, 1.0f),
    clamp(p->smooth[P_OP6_LEVEL], 0.0f, 1.0f)
  };

  for (uint32_t i = 0; i < n; ++i) {
    /* Smooth all params */
    for (uint32_t pidx = 0; pidx < kParamCount; ++pidx) {
      const ParamDef* def = &kParamDefs[pidx];
      const float target = quantize_2(clamp(p->controls[pidx] ? *p->controls[pidx] : def->def, def->min, def->max));
      p->smooth[pidx] += (target - p->smooth[pidx]) * smooth_coeff;
      p->smooth[pidx] = zap(clamp(p->smooth[pidx], def->min, def->max));
    }

    /* LFO (pitch vibrato) */
    p->lfo_phase = wrap_phase(p->lfo_phase + kTwoPi * lfo_hz / sr);
    const float lfo = sinf(p->lfo_phase) * lfo_depth;
    const float lfo_pitch_mul = semitone_ratio(lfo * 2.0f);  /* +/- 2 st max */

    const float base_hz = clamp(p->base_hz * lfo_pitch_mul, 1.0f, sr * 0.45f);

    /* Envelope: gate-on -> decay toward sustain, gate-off -> release toward 0 */
    if (p->gate) {
      p->attack_phase += (1.0f - p->attack_phase) * (1.0f - attack_coef);
      for (int o = 0; o < 6; ++o) {
        p->op_env[o] += (sustain - p->op_env[o]) * (1.0f - decay_coef[o]);
        p->op_env[o] = zap(clamp(p->op_env[o], 0.0f, 1.0f));
      }
    } else {
      for (int o = 0; o < 6; ++o) {
        p->op_env[o] = zap(clamp(p->op_env[o] * release_coef, 0.0f, 1.0f));
      }
      /* Keep attack_phase as is for next note (will be reset on trigger) */
    }
    const float master_amp = clamp(p->attack_phase, 0.0f, 1.0f);

    /* Render all 6 operators in topological order (0..5) */
    float op_out[6];
    for (int o = 0; o < 6; ++o) {
      float mod_in = 0.0f;
      const uint8_t mask = algo.mod_in[o];
      for (int j = 0; j < 6; ++j) {
        if (mask & (1u << j)) {
          /* j must be < o for DAG order, OR o==5 && j==5 (self-feedback) */
          if (j < o) {
            mod_in += op_out[j] * op_level[j] * p->op_env[j];
          } else if (j == o && o == 5) {
            mod_in += p->op_prev[5] * feedback;
          }
        }
      }

      const float hz = clamp(base_hz * op_ratio[o], 0.1f, sr * 0.45f);
      p->phase[o] = wrap_phase(p->phase[o] + kTwoPi * hz / sr);

      float y;
      if (mode_idx == 0) {
        /* FM (phase modulation) */
        y = sinf(p->phase[o] + mod_in * kModIndexScale);
      } else if (mode_idx == 1) {
        /* Ring modulation: carrier sine multiplied by (1 + mod) */
        const float c = sinf(p->phase[o]);
        y = c * clamp(1.0f + mod_in * 3.0f, -3.0f, 3.0f);
        y = clamp(y, -2.0f, 2.0f);
      } else {
        /* Wave folder */
        y = wavefold(sinf(p->phase[o]) + mod_in * 1.5f);
      }
      op_out[o] = zap(finite_or(y, 0.0f));
    }
    p->op_prev[5] = op_out[5];

    /* Sum carriers */
    float mix = 0.0f;
    int n_carriers = 0;
    for (int o = 0; o < 6; ++o) {
      if (algo.carriers & (1u << o)) {
        mix += op_out[o] * p->op_env[o] * op_level[o];
        ++n_carriers;
      }
    }
    /* Mild gain compensation for additive carriers (avoid blowout on
     * additive algorithms while keeping single-carrier algos punchy). */
    if (n_carriers > 1) {
      const float comp = 1.0f / (0.6f + 0.4f * static_cast<float>(n_carriers));
      mix *= comp * 1.4f;
    }

    /* Master amp envelope and velocity */
    float out = mix * master_amp * p->velocity_gain;

    /* Filter */
    out = process_filter(p, out, p->smooth[P_FILTER_CUTOFF], p->smooth[P_FILTER_RES]);

    /* Drive */
    const float drive = clamp(p->smooth[P_DRIVE], 0.0f, 1.0f);
    if (drive > 0.0001f) {
      out = soft_clip(out * (1.0f + drive * 8.0f)) / (1.0f + drive * 0.6f);
    }

    /* Gain in dB and final safety limiter */
    out *= db_to_gain(p->smooth[P_GAIN]);
    out = clamp(finite_or(out, 0.0f), -1.0f, 1.0f);

    p->out_l[i] = out;
    p->out_r[i] = out;
  }
}

static void cleanup(LV2_Handle instance) {
  free(instance);
}

static const LV2_Descriptor descriptor = {
  kUri, instantiate, connect_port, activate, run, nullptr, cleanup, nullptr
};

extern "C" LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) {
  return index == 0 ? &descriptor : nullptr;
}
