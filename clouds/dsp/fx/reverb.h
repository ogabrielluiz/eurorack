// Copyright 2014 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Reverb (4x4 FDN with Hadamard feedback).

#ifndef CLOUDS_DSP_FX_REVERB_H_
#define CLOUDS_DSP_FX_REVERB_H_

#include "stmlib/stmlib.h"

#include "clouds/dsp/fx/fx_engine.h"

namespace clouds {

class Reverb {
 public:
  Reverb() { }
  ~Reverb() { }

  void Init(uint16_t* buffer) {
    engine_.Init(buffer);
    engine_.SetLFOFrequency(LFO_1, 0.5f / 32000.0f);
    engine_.SetLFOFrequency(LFO_2, 0.3f / 32000.0f);
    lp_ = 0.7f;
    diffusion_ = 0.625f;
    for (int32_t i = 0; i < 4; ++i) {
      lp_decay_[i] = 0.0f;
    }
  }

  void Process(FloatFrame* in_out, size_t size) {
    // 4x4 Feedback Delay Network with Hadamard mixing matrix.
    // 2 input diffusion allpasses, 4 delay lines with per-line lowpass.
    typedef E::Reserve<241,
      E::Reserve<397,
      E::Reserve<1559,
      E::Reserve<2333,
      E::Reserve<3571,
      E::Reserve<5107> > > > > > Memory;
    E::DelayLine<Memory, 0> ap1;
    E::DelayLine<Memory, 1> ap2;
    E::DelayLine<Memory, 2> del0;
    E::DelayLine<Memory, 3> del1;
    E::DelayLine<Memory, 4> del2;
    E::DelayLine<Memory, 5> del3;
    E::Context c;

    const float kap = diffusion_;
    const float klp = lp_;
    const float krt = reverb_time_;
    const float amount = amount_;
    const float gain = input_gain_;

    float lp0 = lp_decay_[0];
    float lp1 = lp_decay_[1];
    float lp2 = lp_decay_[2];
    float lp3 = lp_decay_[3];

    while (size--) {
      engine_.Start(&c);

      // Input diffusion through 2 allpasses.
      c.Read(in_out->l + in_out->r, gain);
      c.Read(ap1 TAIL, kap);
      c.WriteAllPass(ap1, -kap);
      c.Read(ap2 TAIL, kap);
      c.WriteAllPass(ap2, -kap);
      float input;
      c.Write(input, 0.0f);

      // Read FDN delay line outputs.
      float d0, d1, d2, d3;

      c.Read(del0 TAIL, 1.0f);
      c.Write(d0, 0.0f);

      c.Read(del1 TAIL, 1.0f);
      c.Write(d1, 0.0f);

      c.Interpolate(del2, 3500.0f, LFO_1, 70.0f, 1.0f);
      c.Write(d2, 0.0f);

      c.Read(del3 TAIL, 1.0f);
      c.Write(d3, 0.0f);

      // Hadamard mixing matrix (orthogonal, energy-preserving).
      float h0 = 0.5f * (d0 + d1 + d2 + d3);
      float h1 = 0.5f * (d0 - d1 + d2 - d3);
      float h2 = 0.5f * (d0 + d1 - d2 - d3);
      float h3 = 0.5f * (d0 - d1 - d2 + d3);

      // Apply decay, lowpass, inject input, write back.
      lp0 += klp * (h0 * krt + input - lp0);
      c.Load(lp0);
      c.Write(del0, 2.0f);

      lp1 += klp * (h1 * krt + input - lp1);
      c.Load(lp1);
      c.Write(del1, 2.0f);

      lp2 += klp * (h2 * krt - lp2);
      c.Load(lp2);
      c.Write(del2, 2.0f);

      lp3 += klp * (h3 * krt - lp3);
      c.Load(lp3);
      c.Write(del3, 2.0f);

      // Stereo output: tap different line pairs for L and R.
      float wet_l = d0 + d2;
      float wet_r = d1 + d3;

      in_out->l += (wet_l - in_out->l) * amount;
      in_out->r += (wet_r - in_out->r) * amount;

      ++in_out;
    }

    lp_decay_[0] = lp0;
    lp_decay_[1] = lp1;
    lp_decay_[2] = lp2;
    lp_decay_[3] = lp3;
  }

  inline void set_amount(float amount) {
    amount_ = amount;
  }

  inline void set_input_gain(float input_gain) {
    input_gain_ = input_gain;
  }

  inline void set_time(float reverb_time) {
    reverb_time_ = reverb_time;
  }

  inline void set_diffusion(float diffusion) {
    diffusion_ = diffusion;
  }

  inline void set_lp(float lp) {
    lp_ = lp;
  }

 private:
  typedef FxEngine<16384, FORMAT_12_BIT> E;
  E engine_;

  float amount_;
  float input_gain_;
  float reverb_time_;
  float diffusion_;
  float lp_;

  float lp_decay_[4];

  DISALLOW_COPY_AND_ASSIGN(Reverb);
};

}  // namespace clouds

#endif  // CLOUDS_DSP_FX_REVERB_H_
