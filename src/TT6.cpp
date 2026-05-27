/*
 * TT6 - 6-operator altered FM synthesizer for Isla S2400
 * TTLab - GPL-2.0-only
 *
 * Polyphonic revision:
 * - fixed 8 voices
 * - voice stealing
 * - sample-accurate MIDI event timing inside each block
 *
 * Inspired by classic 6-operator FM synthesis
 * (phase modulation, ring modulation, wave folding).
 * Not affiliated with or endorsed by Yamaha or Korg.
 */

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lv2/core/lv2.h"
#include "lv2/atom/atom.h"
#include "lv2/atom/util.h"
#include "lv2/midi/midi.h"
#include "lv2/urid/urid.h"

static const char* kUri = "urn:asier:lv2:tt6";
static const char* kUridMapFeature = "http://lv2plug.in/ns/ext/urid#map";

typedef struct {
  void* handle;
  LV2_URID (*map)(void* handle, const char* uri);
} LV2_URID_Map;

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

static constexpr int kOpCount = 6;
static constexpr int kVoiceCount = 8;
static constexpr uint32_t kMaxMidiEventsPerBlock = 256u;

struct Voice {
  float phase[kOpCount];
  float op_env[kOpCount];
  float op_prev[kOpCount];
  float attack_phase;
  float base_hz;
  float velocity_gain;
  int note;
  bool gate;
  bool active;
  uint32_t age;
};

struct Plugin {
  float sr;
  float* out_l;
  float* out_r;
  const float* controls[kParamCount];
  const LV2_Atom_Sequence* midi_in;

  LV2_URID_Map* map;
  LV2_URID midi_event_urid;

  float smooth[kParamCount];

  Voice voices[kVoiceCount];
  uint32_t voice_counter;

  /* Global filter and LFO */
  float svf_lp;
  float svf_bp;
  float lfo_phase;
};

struct MidiEventItem {
  uint32_t frame;
  uint8_t data[3];
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

static void reset_voice(Voice* v) {
  if (!v) return;
  for (int i = 0; i < kOpCount; ++i) {
    v->phase[i] = 0.0f;
    v->op_env[i] = 0.0f;
    v->op_prev[i] = 0.0f;
  }
  v->attack_phase = 0.0f;
  v->base_hz = 220.0f;
  v->velocity_gain = 1.0f;
  v->note = -1;
  v->gate = false;
  v->active = false;
  v->age = 0u;
}

static float voice_energy(const Voice* v) {
  if (!v) return 0.0f;
  float e = clamp(v->attack_phase, 0.0f, 1.0f) * 0.6f;
  for (int i = 0; i < kOpCount; ++i) {
    e += clamp(v->op_env[i], 0.0f, 1.0f) * 0.07f;
  }
  return e;
}

static bool voice_is_quiet(const Voice* v) {
  return voice_energy(v) < 1.0e-4f;
}

static void init_plugin(Plugin* p, float sr) {
  memset(p, 0, sizeof(Plugin));
  p->sr = sr;
  p->midi_event_urid = 0;
  for (uint32_t i = 0; i < kParamCount; ++i) {
    p->controls[i] = nullptr;
    p->smooth[i] = kParamDefs[i].def;
  }
  for (int v = 0; v < kVoiceCount; ++v) {
    reset_voice(&p->voices[v]);
  }
}

static void reset_dsp_state(Plugin* p) {
  if (!p) return;
  for (int v = 0; v < kVoiceCount; ++v) {
    reset_voice(&p->voices[v]);
  }
  p->voice_counter = 0u;
  p->svf_lp = 0.0f;
  p->svf_bp = 0.0f;
  p->lfo_phase = 0.0f;
}

static int allocate_voice(Plugin* p, int note) {
  if (!p) return 0;

  int free_voice = -1;
  int same_note_voice = -1;
  int quiet_released_voice = -1;
  float quiet_energy = 1.0e9f;
  int oldest_voice = 0;
  uint32_t oldest_age = UINT32_MAX;

  for (int v = 0; v < kVoiceCount; ++v) {
    const Voice* voice = &p->voices[v];
    if (!voice->active) {
      free_voice = v;
      break;
    }
    if (voice->gate && voice->note == note) {
      same_note_voice = v;
    }
    if (!voice->gate) {
      const float e = voice_energy(voice);
      if (e < quiet_energy) {
        quiet_energy = e;
        quiet_released_voice = v;
      }
    }
    if (voice->age < oldest_age) {
      oldest_age = voice->age;
      oldest_voice = v;
    }
  }

  if (same_note_voice >= 0) return same_note_voice;
  if (free_voice >= 0) return free_voice;
  if (quiet_released_voice >= 0) return quiet_released_voice;
  return oldest_voice;
}

static void note_on(Plugin* p, int note, float velocity) {
  if (!p) return;
  velocity = clamp(velocity, 0.0f, 1.0f);
  const int v_idx = allocate_voice(p, note);
  Voice* v = &p->voices[v_idx];

  const float vel_amt = clamp(p->smooth[P_VEL_AMOUNT], 0.0f, 1.0f);
  v->velocity_gain = clamp((1.0f - vel_amt) + velocity * vel_amt, 0.0f, 1.0f);
  v->note = note;
  v->base_hz = note_to_hz(note);
  v->gate = true;
  v->active = true;
  v->attack_phase = 0.0f;
  v->age = ++p->voice_counter;
  for (int i = 0; i < kOpCount; ++i) {
    v->phase[i] = 0.0f;
    v->op_env[i] = 1.0f;
    v->op_prev[i] = 0.0f;
  }
}

static void note_off(Plugin* p, int note) {
  if (!p) return;
  for (int v = 0; v < kVoiceCount; ++v) {
    Voice* voice = &p->voices[v];
    if (voice->active && voice->gate && voice->note == note) {
      voice->gate = false;
    }
  }
}

static void all_notes_off(Plugin* p) {
  if (!p) return;
  for (int v = 0; v < kVoiceCount; ++v) {
    Voice* voice = &p->voices[v];
    if (voice->active) {
      voice->gate = false;
      voice->note = -1;
    }
  }
}

static uint32_t collect_midi_events(const Plugin* p, MidiEventItem* events, uint32_t max_events, uint32_t nframes) {
  if (!p || !events || max_events == 0u || !p->midi_in || p->midi_in->atom.size < 8u || nframes == 0u) {
    return 0u;
  }

  uint32_t count = 0u;
  LV2_ATOM_SEQUENCE_FOREACH(p->midi_in, ev) {
    if (count >= max_events) {
      break;
    }
    if (ev->body.size < 3u) {
      continue;
    }
    if (p->midi_event_urid != 0u && ev->body.type != p->midi_event_urid) {
      continue;
    }

    const uint8_t* msg = reinterpret_cast<const uint8_t*>(ev + 1);
    const uint8_t status = msg[0] & 0xF0u;
    if (status != 0x80u && status != 0x90u && status != 0xB0u) {
      continue;
    }

    uint32_t frame = 0u;
    if (ev->time.frames > 0) {
      const int64_t f = ev->time.frames;
      frame = (f >= static_cast<int64_t>(nframes)) ? (nframes - 1u) : static_cast<uint32_t>(f);
    }

    events[count].frame = frame;
    events[count].data[0] = msg[0];
    events[count].data[1] = msg[1];
    events[count].data[2] = msg[2];
    ++count;
  }

  return count;
}

static void dispatch_midi(Plugin* p, const MidiEventItem* ev) {
  if (!p || !ev) return;
  const uint8_t status = ev->data[0] & 0xF0u;
  if (status == 0x90u && ev->data[2] > 0u) {
    note_on(p, static_cast<int>(ev->data[1]), clamp(static_cast<float>(ev->data[2]) / 127.0f, 0.0f, 1.0f));
    return;
  }
  if (status == 0x80u || (status == 0x90u && ev->data[2] == 0u)) {
    note_off(p, static_cast<int>(ev->data[1]));
    return;
  }
  if (status == 0xB0u) {
    const uint8_t cc = ev->data[1];
    if (cc == 120u || cc == 123u) {
      all_notes_off(p);
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

static float render_voice_sample(Voice* v,
                                 float sr,
                                 const AlgoSpec& algo,
                                 int mode_idx,
                                 float base_hz,
                                 const float op_ratio[kOpCount],
                                 const float op_level[kOpCount],
                                 const float decay_coef[kOpCount],
                                 float attack_coef,
                                 float sustain,
                                 float release_coef,
                                 float feedback) {
  if (!v || !v->active) return 0.0f;

  if (v->gate) {
    v->attack_phase += (1.0f - v->attack_phase) * (1.0f - attack_coef);
    v->attack_phase = zap(clamp(v->attack_phase, 0.0f, 1.0f));
    for (int o = 0; o < kOpCount; ++o) {
      v->op_env[o] += (sustain - v->op_env[o]) * (1.0f - decay_coef[o]);
      v->op_env[o] = zap(clamp(v->op_env[o], 0.0f, 1.0f));
    }
  } else {
    for (int o = 0; o < kOpCount; ++o) {
      v->op_env[o] = zap(clamp(v->op_env[o] * release_coef, 0.0f, 1.0f));
    }
  }

  if (!v->gate && voice_is_quiet(v)) {
    reset_voice(v);
    return 0.0f;
  }

  const float master_amp = clamp(v->attack_phase, 0.0f, 1.0f);

  float op_out[kOpCount];
  for (int o = 0; o < kOpCount; ++o) {
    float mod_in = 0.0f;
    const uint8_t mask = algo.mod_in[o];
    for (int j = 0; j < kOpCount; ++j) {
      if (mask & (1u << j)) {
        if (j < o) {
          mod_in += op_out[j] * op_level[j] * v->op_env[j];
        } else if (j == o && o == 5) {
          mod_in += v->op_prev[5] * feedback;
        }
      }
    }

    const float hz = clamp(base_hz * op_ratio[o], 0.1f, sr * 0.45f);
    v->phase[o] = wrap_phase(v->phase[o] + kTwoPi * hz / sr);

    float y;
    if (mode_idx == 0) {
      y = sinf(v->phase[o] + mod_in * kModIndexScale);
    } else if (mode_idx == 1) {
      const float c = sinf(v->phase[o]);
      y = c * clamp(1.0f + mod_in * 3.0f, -3.0f, 3.0f);
      y = clamp(y, -2.0f, 2.0f);
    } else {
      y = wavefold(sinf(v->phase[o]) + mod_in * 1.5f);
    }
    op_out[o] = zap(finite_or(y, 0.0f));
  }
  v->op_prev[5] = op_out[5];

  float mix = 0.0f;
  int n_carriers = 0;
  for (int o = 0; o < kOpCount; ++o) {
    if (algo.carriers & (1u << o)) {
      mix += op_out[o] * v->op_env[o] * op_level[o];
      ++n_carriers;
    }
  }

  if (n_carriers > 1) {
    const float comp = 1.0f / (0.6f + 0.4f * static_cast<float>(n_carriers));
    mix *= comp * 1.4f;
  }

  return mix * master_amp * v->velocity_gain;
}

static LV2_Handle instantiate(const LV2_Descriptor*,
                              double rate,
                              const char*,
                              const LV2_Feature* const* features) {
  Plugin* p = static_cast<Plugin*>(calloc(1, sizeof(Plugin)));
  if (!p) return nullptr;

  const float sr = (finite_double(rate) && rate >= 1000.0 && rate <= 384000.0)
                     ? static_cast<float>(rate)
                     : kRefSampleRate;
  init_plugin(p, sr);

  if (features) {
    for (uint32_t i = 0; features[i]; ++i) {
      const LV2_Feature* f = features[i];
      if (f && f->URI && strcmp(f->URI, kUridMapFeature) == 0) {
        p->map = (LV2_URID_Map*)f->data;
        break;
      }
    }
  }
  if (p->map && p->map->map) {
    p->midi_event_urid = p->map->map(p->map->handle, LV2_MIDI__MidiEvent);
  }

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
  if (!p || !p->out_l || !p->out_r || n == 0u) return;

  const float sr = sample_rate(p);
  const float smooth_coeff = 1.0f - time_coef_ms(p, 8.0f);

  float target[kParamCount];
  for (uint32_t pidx = 0; pidx < kParamCount; ++pidx) {
    const ParamDef* def = &kParamDefs[pidx];
    const float raw = p->controls[pidx] ? *p->controls[pidx] : def->def;
    target[pidx] = quantize_2(clamp(raw, def->min, def->max));
  }

  MidiEventItem midi_events[kMaxMidiEventsPerBlock];
  const uint32_t midi_count = collect_midi_events(p, midi_events, kMaxMidiEventsPerBlock, n);
  uint32_t midi_index = 0u;

  float decay_coef[kOpCount] = {0};
  float attack_coef = time_coef_ms(p, p->smooth[P_ATTACK]);
  float release_coef = time_coef_ms(p, p->smooth[P_RELEASE]);

  for (uint32_t i = 0; i < n; ++i) {
    while (midi_index < midi_count && midi_events[midi_index].frame <= i) {
      dispatch_midi(p, &midi_events[midi_index]);
      ++midi_index;
    }

    for (uint32_t pidx = 0; pidx < kParamCount; ++pidx) {
      const ParamDef* def = &kParamDefs[pidx];
      p->smooth[pidx] += (target[pidx] - p->smooth[pidx]) * smooth_coeff;
      p->smooth[pidx] = zap(clamp(p->smooth[pidx], def->min, def->max));
    }

    /* Recompute time constants every 16 samples for lower CPU. */
    if ((i & 15u) == 0u) {
      attack_coef = time_coef_ms(p, p->smooth[P_ATTACK]);
      release_coef = time_coef_ms(p, p->smooth[P_RELEASE]);
      decay_coef[0] = time_coef_ms(p, p->smooth[P_OP1_DECAY]);
      decay_coef[1] = time_coef_ms(p, p->smooth[P_OP2_DECAY]);
      decay_coef[2] = time_coef_ms(p, p->smooth[P_OP3_DECAY]);
      decay_coef[3] = time_coef_ms(p, p->smooth[P_OP4_DECAY]);
      decay_coef[4] = time_coef_ms(p, p->smooth[P_OP5_DECAY]);
      decay_coef[5] = time_coef_ms(p, p->smooth[P_OP6_DECAY]);
    }

    int algo_idx = nearest_int(p->smooth[P_ALGO]);
    if (algo_idx < 0) algo_idx = 0;
    if (algo_idx > 15) algo_idx = 15;
    const AlgoSpec& algo = kAlgos[algo_idx];

    int mode_idx = nearest_int(p->smooth[P_OP_MODE]);
    if (mode_idx < 0) mode_idx = 0;
    if (mode_idx > 2) mode_idx = 2;

    const float feedback = clamp(p->smooth[P_FEEDBACK], 0.0f, 1.0f) * 0.9f;
    const float sustain = clamp(p->smooth[P_SUSTAIN], 0.0f, 1.0f);

    const float op_ratio[kOpCount] = {
      clamp(p->smooth[P_OP1_RATIO], 0.25f, 32.0f),
      clamp(p->smooth[P_OP2_RATIO], 0.25f, 32.0f),
      clamp(p->smooth[P_OP3_RATIO], 0.25f, 32.0f),
      clamp(p->smooth[P_OP4_RATIO], 0.25f, 32.0f),
      clamp(p->smooth[P_OP5_RATIO], 0.25f, 32.0f),
      clamp(p->smooth[P_OP6_RATIO], 0.25f, 32.0f)
    };
    const float op_level[kOpCount] = {
      clamp(p->smooth[P_OP1_LEVEL], 0.0f, 1.0f),
      clamp(p->smooth[P_OP2_LEVEL], 0.0f, 1.0f),
      clamp(p->smooth[P_OP3_LEVEL], 0.0f, 1.0f),
      clamp(p->smooth[P_OP4_LEVEL], 0.0f, 1.0f),
      clamp(p->smooth[P_OP5_LEVEL], 0.0f, 1.0f),
      clamp(p->smooth[P_OP6_LEVEL], 0.0f, 1.0f)
    };

    p->lfo_phase = wrap_phase(p->lfo_phase + kTwoPi * clamp(p->smooth[P_LFO_RATE], 0.05f, 30.0f) / sr);
    const float lfo_depth = clamp(p->smooth[P_LFO_DEPTH], 0.0f, 1.0f);
    const float lfo = sinf(p->lfo_phase) * lfo_depth;
    const float lfo_pitch_mul = semitone_ratio(lfo * 2.0f);  /* +/-2 semitones */

    float mix = 0.0f;
    int live_voices = 0;
    for (int v = 0; v < kVoiceCount; ++v) {
      Voice* voice = &p->voices[v];
      if (!voice->active) continue;
      const float voice_hz = clamp(voice->base_hz * lfo_pitch_mul, 1.0f, sr * 0.45f);
      mix += render_voice_sample(voice,
                                 sr,
                                 algo,
                                 mode_idx,
                                 voice_hz,
                                 op_ratio,
                                 op_level,
                                 decay_coef,
                                 attack_coef,
                                 sustain,
                                 release_coef,
                                 feedback);
      if (voice->active) {
        ++live_voices;
      }
    }

    if (live_voices > 1) {
      mix *= 1.0f / (0.8f + 0.2f * static_cast<float>(live_voices));
    }

    float out = process_filter(p, mix, p->smooth[P_FILTER_CUTOFF], p->smooth[P_FILTER_RES]);

    const float drive = clamp(p->smooth[P_DRIVE], 0.0f, 1.0f);
    if (drive > 0.0001f) {
      out = soft_clip(out * (1.0f + drive * 8.0f)) / (1.0f + drive * 0.6f);
    }

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
