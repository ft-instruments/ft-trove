// The eight host-visible controls. The UI is built from this table, so the
// page and the plugin cannot drift apart.
#pragma once

namespace trove {

enum ParamId {
  pGain, pTone, pReverb, pAttack, pDecay, pSustain, pRelease, pOutput, NUM_PARAMS
};

struct ParamDef {
  const char* id;
  const char* name;
  float min, max, def, skew;
  const char* unit;
};

inline const ParamDef PARAMS[NUM_PARAMS] = {
  { "gain",    "Level",   -36.0f,  18.0f,   0.0f, 1.00f, "dB" },
  { "tone",    "Tone",      0.0f,   1.0f,   1.0f, 1.00f, "" },
  { "reverb",  "Space",     0.0f,   1.0f,   0.12f, 1.00f, "" },
  { "attack",  "Attack",    0.0f,  12.0f,   0.0f, 2.20f, "s" },
  { "decay",   "Decay",     0.0f,  12.0f,   0.30f, 2.00f, "s" },
  { "sustain", "Sustain",   0.0f,   1.0f,   1.0f, 1.00f, "" },
  { "release", "Release",   0.0f,  20.0f,   0.40f, 2.20f, "s" },
  // Overall output trim. Unlike "gain" it belongs to the user, not the preset,
  // so loading a preset leaves it where it is.
  { "output",  "Output",  -36.0f,  18.0f,   0.0f, 1.00f, "dB" },
};

} // namespace trove
