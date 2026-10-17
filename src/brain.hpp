#pragma once

#include <array>

#include "rng.hpp"

namespace evo {

constexpr int kInputs = 17;
constexpr int kHidden = 8;
constexpr int kOutputs = 4;

// What each neuron means, for the brain view.
constexpr std::array<const char*, kInputs> kInputNames = {
    "food L", "food C", "food R", "other L", "other C", "other R",
    "size vs", "energy", "clock", "memory", "speed", "sick near", "I'm sick",
    "too hot", "warmer ahd", "in water", "water ahd"};
constexpr std::array<const char*, kOutputs> kOutputNames = {"turn", "thrust", "attack", "memory"};

// A small feed-forward network: inputs -> tanh hidden layer -> tanh outputs.
// The "memory" output is fed back as an input on the next tick, so it can hold state.
float fastTanh(float x);

struct Brain {
    std::array<float, kInputs * kHidden> w1{};  // w1[i * kHidden + h]
    std::array<float, kHidden> b1{};
    std::array<float, kHidden * kOutputs> w2{};  // w2[h * kOutputs + o]
    std::array<float, kOutputs> b2{};

    static Brain random(Rng& rng);
    void mutate(Rng& rng, float rate, float strength);
    void think(const std::array<float, kInputs>& in, std::array<float, kHidden>& hidden,
               std::array<float, kOutputs>& out) const;
};

}  // namespace evo
