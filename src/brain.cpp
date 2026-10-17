#include "brain.hpp"

#include <algorithm>
#include <cmath>

namespace evo {

Brain Brain::random(Rng& rng) {
    Brain b;
    for (auto& w : b.w1) w = rng.normal(0.f, 0.8f);
    for (auto& w : b.b1) w = rng.normal(0.f, 0.3f);
    for (auto& w : b.w2) w = rng.normal(0.f, 0.8f);
    for (auto& w : b.b2) w = rng.normal(0.f, 0.3f);
    return b;
}

void Brain::mutate(Rng& rng, float rate, float strength) {
    auto nudge = [&](auto& weights) {
        for (auto& w : weights) {
            if (rng.chance(rate)) w += rng.normal(0.f, strength);
            if (rng.chance(rate * 0.02f)) w = rng.normal(0.f, 0.8f);  // rare full rewire
        }
    };
    nudge(w1);
    nudge(b1);
    nudge(w2);
    nudge(b2);
}

// tanh as a rational function (the approximation Eigen uses): accurate to float precision,
// several times faster than std::tanh, and free of library calls so the loops vectorise.
float fastTanh(float x) {
    x = std::clamp(x, -7.90531110763549805f, 7.90531110763549805f);
    const float x2 = x * x;
    float p = -2.76076847742355e-16f;
    p = p * x2 + 2.00018790482477e-13f;
    p = p * x2 - 8.60467152213735e-11f;
    p = p * x2 + 5.12229709037114e-08f;
    p = p * x2 + 1.48572235717979e-05f;
    p = p * x2 + 6.37261928875436e-04f;
    p = p * x2 + 4.89352455891786e-03f;
    float q = 1.19825839466702e-06f;
    q = q * x2 + 1.18534705686654e-04f;
    q = q * x2 + 2.26843463243900e-03f;
    q = q * x2 + 4.89352518554385e-03f;
    return x * p / q;
}

void Brain::think(const std::array<float, kInputs>& in, std::array<float, kHidden>& hidden,
                  std::array<float, kOutputs>& out) const {
    // input-major order: the inner loop runs over contiguous weights, so it becomes SIMD
    std::array<float, kHidden> h = b1;
    for (int i = 0; i < kInputs; ++i)
        for (int j = 0; j < kHidden; ++j) h[j] += in[i] * w1[i * kHidden + j];
    for (int j = 0; j < kHidden; ++j) hidden[j] = fastTanh(h[j]);
    std::array<float, kOutputs> o = b2;
    for (int j = 0; j < kHidden; ++j)
        for (int k = 0; k < kOutputs; ++k) o[k] += hidden[j] * w2[j * kOutputs + k];
    for (int k = 0; k < kOutputs; ++k) out[k] = fastTanh(o[k]);
}

}  // namespace evo
