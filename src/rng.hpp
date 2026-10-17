#pragma once

#include <cstdint>
#include <random>

namespace evo {

// One seeded generator for the whole simulation, so a run is reproducible from its seed.
class Rng {
public:
    explicit Rng(uint32_t seed) : gen_(seed) {}

    float uniform(float lo = 0.f, float hi = 1.f) { return std::uniform_real_distribution<float>(lo, hi)(gen_); }
    float normal(float mean = 0.f, float sd = 1.f) { return std::normal_distribution<float>(mean, sd)(gen_); }
    bool chance(float p) { return uniform() < p; }
    int index(int n) { return std::uniform_int_distribution<int>(0, n - 1)(gen_); }

private:
    std::mt19937 gen_;
};

}  // namespace evo
