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


#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <vector>

#include <sys/stat.h>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
#include <xmmintrin.h>
#endif

#include "clouds/dsp/granular_processor.h"
#include "clouds/resources.h"

using namespace clouds;
using namespace std;
using namespace stmlib;

const size_t kSampleRate = 32000;
const size_t kBlockSize = 32;

void write_wav_header(FILE* fp, int num_samples, int num_channels) {
  uint32_t l;
  uint16_t s;

  fwrite("RIFF", 4, 1, fp);
  l = 36 + num_samples * 2 * num_channels;
  fwrite(&l, 4, 1, fp);
  fwrite("WAVE", 4, 1, fp);

  fwrite("fmt ", 4, 1, fp);
  l = 16;
  fwrite(&l, 4, 1, fp);
  s = 1;
  fwrite(&s, 2, 1, fp);
  s = num_channels;
  fwrite(&s, 2, 1, fp);
  l = kSampleRate;
  fwrite(&l, 4, 1, fp);
  l = static_cast<uint32_t>(kSampleRate) * 2 * num_channels;
  fwrite(&l, 4, 1, fp);
  s = 2 * num_channels;
  fwrite(&s, 2, 1, fp);
  s = 16;
  fwrite(&s, 2, 1, fp);

  fwrite("data", 4, 1, fp);
  l = num_samples * 2 * num_channels;
  fwrite(&l, 4, 1, fp);
}

// ---------------------------------------------------------------------------
// Legacy tests (original code, kept for reference)
// ---------------------------------------------------------------------------

void TestDSP() {
  size_t duration = 19;

  FILE* fp_out = fopen("clouds.wav", "wb");
  FILE* fp_in = fopen("audio_samples/kettel_32k.wav", "rb");

  size_t remaining_samples = kSampleRate * duration;
  write_wav_header(fp_out, remaining_samples, 2);
  if (fp_in) fseek(fp_in, 48, SEEK_SET);

  uint8_t large_buffer[118784];
  uint8_t small_buffer[65536 - 128];

  GranularProcessor processor;
  processor.Init(
      &large_buffer[0], sizeof(large_buffer),
      &small_buffer[0],sizeof(small_buffer));

  processor.set_num_channels(2);
  processor.set_low_fidelity(false);
  processor.set_playback_mode(PLAYBACK_MODE_GRANULAR);

  Parameters* p = processor.mutable_parameters();

  size_t block_counter = 0;
  float phase_ = 0.0f;
  bool synthetic = true;
  processor.Prepare();
  float pot_noise = 0.0f;
  while (remaining_samples) {
    uint16_t tri = (remaining_samples * 2);
    tri = tri > 32767 ? 65535 - tri : tri;
    (void)tri;

    p->gate = false;
    p->trigger = false;
    p->freeze = (block_counter & 2047) > 1024;
    pot_noise += 0.05f * ((Random::GetSample() / 32768.0f) * 0.00f - pot_noise);
    p->position = 0.5f;
    p->size = 0.5f;
    p->pitch = 0.0f;
    p->density = 0.7f;
    p->texture = 0.5f;
    p->feedback = 0.3f;
    p->dry_wet = 1.0f;
    p->reverb = 0.0f;
    p->stereo_spread = 0.0f;

    ++block_counter;
    ShortFrame input[kBlockSize];
    ShortFrame output[kBlockSize];

    if (synthetic) {
      for (size_t i = 0; i < kBlockSize; ++i) {
        phase_ += 400.0f / kSampleRate; // (block_counter & 512 ? 110.0f : 220.0f) / kSampleRate;
        while (phase_ >= 1.0) {
          phase_ -= 1.0;
        }
        input[i].l = 16384.0f * sinf(phase_ * M_PI * 2);
        // input[i].r = 32768.0f * (phase_ - 0.5);
        input[i].r = input[i].l;
      }
      remaining_samples -= kBlockSize;
    } else {
      if (fread(
              input,
              sizeof(ShortFrame),
              kBlockSize,
              fp_in) != kBlockSize) {
        break;
      }
      remaining_samples -= kBlockSize;
    }
    processor.Process(input, output, kBlockSize);
    processor.Prepare();
    fwrite(output, sizeof(ShortFrame), kBlockSize, fp_out);
  }
  fclose(fp_out);
  fclose(fp_in);
}

// ---------------------------------------------------------------------------
// Regression test harness
// ---------------------------------------------------------------------------

struct TestScenario {
  const char* name;
  PlaybackMode mode;
  float position;
  float size;
  float pitch;
  float density;
  float texture;
  float feedback;
  float dry_wet;
  float reverb;
  float stereo_spread;
  bool freeze_toggle;
  size_t duration;
};

static const TestScenario kScenarios[] = {
  // Granular mode — core variations
  {"gran_default",       PLAYBACK_MODE_GRANULAR, 0.5f, 0.5f,  0.0f, 0.7f, 0.5f, 0.3f, 1.0f, 0.0f, 0.0f, true,  5},
  {"gran_deterministic", PLAYBACK_MODE_GRANULAR, 0.5f, 0.5f,  0.0f, 0.3f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, false, 5},
  {"gran_probabilistic", PLAYBACK_MODE_GRANULAR, 0.5f, 0.5f,  0.0f, 0.8f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, false, 5},
  {"gran_freeze_fb",     PLAYBACK_MODE_GRANULAR, 0.5f, 0.5f,  0.0f, 0.7f, 0.5f, 0.8f, 1.0f, 0.0f, 0.0f, true,  5},
  {"gran_texture_lo",    PLAYBACK_MODE_GRANULAR, 0.5f, 0.5f,  0.0f, 0.7f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, false, 5},
  {"gran_texture_hi",    PLAYBACK_MODE_GRANULAR, 0.5f, 0.5f,  0.0f, 0.7f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, false, 5},
  {"gran_pitch_up",      PLAYBACK_MODE_GRANULAR, 0.5f, 0.5f, 12.0f, 0.7f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, true,  5},
  {"gran_reverb",        PLAYBACK_MODE_GRANULAR, 0.5f, 0.5f,  0.0f, 0.7f, 0.5f, 0.3f, 1.0f, 0.5f, 0.0f, true,  5},
  {"gran_spread",        PLAYBACK_MODE_GRANULAR, 0.5f, 0.5f,  0.0f, 0.7f, 0.5f, 0.0f, 1.0f, 0.0f, 1.0f, false, 5},

  // Stretch mode
  {"stretch_default",    PLAYBACK_MODE_STRETCH,  0.5f, 0.5f,  0.0f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, true,  5},
  {"stretch_pitch",      PLAYBACK_MODE_STRETCH,  0.5f, 0.5f,  7.0f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, true,  5},

  // Looping delay mode
  {"delay_default",      PLAYBACK_MODE_LOOPING_DELAY, 0.5f, 0.5f, 0.0f, 0.5f, 0.5f, 0.3f, 1.0f, 0.0f, 0.0f, false, 5},
  {"delay_feedback",     PLAYBACK_MODE_LOOPING_DELAY, 0.5f, 0.5f, 0.0f, 0.5f, 0.5f, 0.8f, 1.0f, 0.0f, 0.0f, false, 5},

  // Spectral mode
  {"spectral_default",   PLAYBACK_MODE_SPECTRAL, 0.5f, 0.5f,  0.0f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, true,  5},
  {"spectral_dense",     PLAYBACK_MODE_SPECTRAL, 0.5f, 0.5f,  0.0f, 0.9f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, false, 5},
};

static const size_t kNumScenarios = sizeof(kScenarios) / sizeof(kScenarios[0]);

// Read a 16-bit PCM WAV file into a sample buffer.
static bool ReadWav(const char* path, vector<int16_t>* samples) {
  FILE* fp = fopen(path, "rb");
  if (!fp) return false;

  char header[44];
  if (fread(header, 1, 44, fp) != 44) { fclose(fp); return false; }

  uint32_t data_size = static_cast<uint8_t>(header[40])
                     | (static_cast<uint8_t>(header[41]) << 8)
                     | (static_cast<uint8_t>(header[42]) << 16)
                     | (static_cast<uint8_t>(header[43]) << 24);
  size_t n = data_size / 2;
  samples->resize(n);
  size_t got = fread(samples->data(), 2, n, fp);
  samples->resize(got);
  fclose(fp);
  return true;
}

// Copy a file byte-for-byte.
static bool CopyFile(const char* src, const char* dst) {
  FILE* in = fopen(src, "rb");
  if (!in) return false;
  FILE* out = fopen(dst, "wb");
  if (!out) { fclose(in); return false; }
  char buf[8192];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
    fwrite(buf, 1, n, out);
  }
  fclose(in);
  fclose(out);
  return true;
}

// Run a single regression scenario. Returns output RMS.
static double RunScenario(const TestScenario& s, const char* output_dir) {
  Random::Seed(0x21);

  char path[256];
  snprintf(path, sizeof(path), "%s/%s.wav", output_dir, s.name);

  FILE* fp = fopen(path, "wb");
  if (!fp) {
    fprintf(stderr, "ERROR: cannot open %s\n", path);
    return 0.0;
  }

  size_t total_samples = kSampleRate * s.duration;
  write_wav_header(fp, total_samples, 2);

  uint8_t large_buffer[118784];
  uint8_t small_buffer[65536 - 128];

  GranularProcessor processor;
  processor.Init(&large_buffer[0], sizeof(large_buffer),
                 &small_buffer[0], sizeof(small_buffer));
  processor.set_num_channels(2);
  processor.set_low_fidelity(false);
  processor.set_playback_mode(s.mode);

  Parameters* p = processor.mutable_parameters();

  size_t remaining = total_samples;
  size_t block_counter = 0;
  float phase = 0.0f;
  double sum_sq = 0.0;
  size_t sample_count = 0;

  processor.Prepare();

  while (remaining >= kBlockSize) {
    p->gate = false;
    p->trigger = false;
    p->freeze = s.freeze_toggle ? ((block_counter & 2047) > 1024) : false;
    p->position = s.position;
    p->size = s.size;
    p->pitch = s.pitch;
    p->density = s.density;
    p->texture = s.texture;
    p->feedback = s.feedback;
    p->dry_wet = s.dry_wet;
    p->reverb = s.reverb;
    p->stereo_spread = s.stereo_spread;

    ShortFrame input[kBlockSize];
    ShortFrame output[kBlockSize];

    for (size_t i = 0; i < kBlockSize; ++i) {
      phase += 400.0f / kSampleRate;
      while (phase >= 1.0f) phase -= 1.0f;
      int16_t sample = static_cast<int16_t>(16384.0f * sinf(phase * M_PI * 2));
      input[i].l = sample;
      input[i].r = sample;
    }

    processor.Process(input, output, kBlockSize);
    processor.Prepare();
    fwrite(output, sizeof(ShortFrame), kBlockSize, fp);

    for (size_t i = 0; i < kBlockSize; ++i) {
      double l = output[i].l;
      double r = output[i].r;
      sum_sq += l * l + r * r;
      sample_count += 2;
    }

    remaining -= kBlockSize;
    ++block_counter;
  }

  fclose(fp);
  return sqrt(sum_sq / sample_count);
}

// Compare current output against baseline. Returns true if identical or no baseline.
static bool CompareBaseline(const char* output_dir, const TestScenario& s) {
  char test_path[256], baseline_path[256];
  snprintf(test_path, sizeof(test_path), "%s/%s.wav", output_dir, s.name);
  snprintf(baseline_path, sizeof(baseline_path),
           "%s/baseline/%s.wav", output_dir, s.name);

  vector<int16_t> test_samples, baseline_samples;
  if (!ReadWav(baseline_path, &baseline_samples)) {
    printf("  [NO BASELINE]");
    return true;
  }
  if (!ReadWav(test_path, &test_samples)) {
    printf("  [READ ERROR]");
    return false;
  }

  size_t n = min(test_samples.size(), baseline_samples.size());
  int32_t max_delta = 0;
  size_t num_different = 0;
  double sum_sq = 0.0;

  for (size_t i = 0; i < n; ++i) {
    int32_t d = static_cast<int32_t>(test_samples[i])
              - static_cast<int32_t>(baseline_samples[i]);
    if (d < 0) d = -d;
    if (d > max_delta) max_delta = d;
    if (d > 0) num_different++;
    sum_sq += static_cast<double>(d) * d;
  }

  if (test_samples.size() != baseline_samples.size()) {
    printf("  [SIZE MISMATCH: %zu vs %zu]", test_samples.size(), baseline_samples.size());
    return false;
  } else if (max_delta == 0) {
    printf("  [IDENTICAL]");
    return true;
  } else {
    double rms_delta = sqrt(sum_sq / n);
    printf("  max:%d rms:%.1f diff:%zu/%zu", max_delta, rms_delta, num_different, n);
    if (max_delta > 100) {
      printf(" [CHANGED]");
      return false;
    } else {
      printf(" [MINOR]");
      return true;
    }
  }
}

static int TestRegression(const char* output_dir, bool save_baseline) {
  mkdir(output_dir, 0755);

  printf("Cumulus regression suite: %zu scenarios\n", kNumScenarios);
  printf("%-26s %9s  %s\n", "Scenario", "RMS", "Baseline");
  printf("%-26s %9s  %s\n", "--------", "---", "--------");

  int failures = 0;
  for (size_t i = 0; i < kNumScenarios; ++i) {
    double rms = RunScenario(kScenarios[i], output_dir);
    printf("  %-24s %7.1f", kScenarios[i].name, rms);
    if (!CompareBaseline(output_dir, kScenarios[i])) {
      failures++;
    }
    printf("\n");
  }

  if (save_baseline) {
    char baseline_dir[256];
    snprintf(baseline_dir, sizeof(baseline_dir), "%s/baseline", output_dir);
    mkdir(baseline_dir, 0755);

    printf("\nSaving baselines to %s/\n", baseline_dir);
    for (size_t i = 0; i < kNumScenarios; ++i) {
      char src[256], dst[256];
      snprintf(src, sizeof(src), "%s/%s.wav", output_dir, kScenarios[i].name);
      snprintf(dst, sizeof(dst), "%s/baseline/%s.wav", output_dir, kScenarios[i].name);
      if (CopyFile(src, dst)) {
        printf("  saved %s\n", kScenarios[i].name);
      }
    }
  }

  if (failures > 0) {
    printf("\nFAILED: %d scenario(s) changed.\n", failures);
  } else {
    printf("\nPASSED.\n");
  }
  return failures;
}

// ---------------------------------------------------------------------------

static void PrintUsage(const char* argv0) {
  printf("Usage: %s [OPTIONS]\n", argv0);
  printf("\n");
  printf("  (default)          Run regression suite, compare against baselines\n");
  printf("  --save-baseline    Run suite and save outputs as new baselines\n");
  printf("  --legacy           Run original TestDSP (19s granular output)\n");
  printf("  --help             Show this help\n");
}

int main(int argc, char* argv[]) {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
  _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
#endif

  bool save_baseline = false;
  bool legacy = false;

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--save-baseline") == 0) {
      save_baseline = true;
    } else if (strcmp(argv[i], "--legacy") == 0) {
      legacy = true;
    } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      PrintUsage(argv[0]);
      return 0;
    } else {
      fprintf(stderr, "Unknown option: %s\n", argv[i]);
      PrintUsage(argv[0]);
      return 1;
    }
  }

  if (legacy) {
    TestDSP();
    return 0;
  } else {
    return TestRegression("regression", save_baseline) > 0 ? 1 : 0;
  }
}
