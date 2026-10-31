// Amplitude envelope: linear attack, hold, exponential decay and release,
// retriggering from the current level so a stolen or repeated note never clicks.
#pragma once

#include <cmath>

namespace trove {

class Envelope {
public:
  enum Stage { Idle, Attack, Hold, Decay, Sustain, Release };

  void setSampleRate(double sr) { rate = sr; }
  void set(double a, double h, double d, double s, double r) {
    attack = a; hold = h; decay = d; sustain = s; release = r;
    aStep = 1.0 / std::fmax(0.0005, attack) / rate;
    decayK = 1.0 - std::exp(-1.0 / std::fmax(0.001, decay) / rate);
    releaseK = std::exp(-6.9 / std::fmax(0.001, release) / rate);
  }

  void gate(bool on) {
    if (on) {
      stage = attack > 0.0005 ? Attack : (hold > 0 ? Hold : Decay);
      holdLeft = hold * rate;
      if (stage == Hold) holdLeft = hold * rate;
      // no attack: the note starts at full level and falls to the sustain. Starting
      // from silence would leave a sustain-0 piano (The Gaffer) mute forever.
      if (attack <= 0.0005) level = 1.0;
    } else if (stage != Idle) {
      stage = Release;
    }
  }

  bool active() const { return stage != Idle; }
  double value() const { return level; }

  float next() {
    switch (stage) {
      case Attack:
        level += aStep;
        if (level >= 1.0) {
          level = 1.0;
          stage = hold > 0.0005 ? Hold : Decay;
          holdLeft = hold * rate;
        }
        break;
      case Hold:
        holdLeft -= 1.0;
        if (holdLeft <= 0.0) { stage = Decay; level = 1.0; }
        break;
      case Decay:
        level += (sustain - level) * decayK;
        if (level <= sustain + 1.0e-4) { level = sustain; stage = Sustain; }
        break;
      case Sustain:
        level = sustain;
        break;
      case Release:
        level *= releaseK;
        if (level < 1.0e-4) { level = 0.0; stage = Idle; }
        break;
      case Idle:
        level = 0.0;
        break;
    }
    return (float)level;
  }

  void reset() { stage = Idle; level = 0.0; holdLeft = 0.0; }

private:
  Stage stage = Idle;
  double level = 0.0, holdLeft = 0.0;
  double attack = 0.0, hold = 0.0, decay = 0.3, sustain = 1.0, release = 0.4;
  double aStep = 0.01, decayK = 0.01, releaseK = 0.999;
  double rate = 44100.0;
};

} // namespace trove
