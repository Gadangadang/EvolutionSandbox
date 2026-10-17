#include "climate.hpp"

#include <algorithm>
#include <cmath>

namespace evo {

namespace {
constexpr float kTau = 6.28318531f;
constexpr float kWaterTemp = 14.f;      // water pulls the temperature toward this ...
constexpr float kWaterMildness = 0.3f;  // ... by this fraction
constexpr float kWaterSwing = 0.5f;     // and seasons swing less over water
}  // namespace

void Climate::generate(float width, float height, const ClimateSettings& settings, Rng& rng) {
    settings_ = settings;
    cols_ = std::max(2, static_cast<int>(width / kCell));
    rows_ = std::max(2, static_cast<int>(height / kCell));
    cellW_ = width / cols_;
    cellH_ = height / rows_;
    const int cells = cols_ * rows_;

    // Terrain: a sum of waves with whole numbers of periods across the world, so it tiles
    // seamlessly. Long waves are strong and short ones weak, which gives continents, islands
    // and lakes rather than noise.
    struct Wave {
        float kx, ky, amplitude, phase;
    };
    std::vector<Wave> waves;
    for (int i = 0; i < 48; ++i) {
        int kx = rng.index(13) - 6, ky = rng.index(9) - 4;
        if (kx == 0 && ky == 0) continue;
        float frequency = std::hypot(kx / width, ky / height) * height;  // in waves per world height
        waves.push_back({static_cast<float>(kx), static_cast<float>(ky), rng.normal(0.f, 1.f) / std::pow(frequency, 1.3f),
                         rng.uniform(0.f, kTau)});
    }
    elevation_.assign(cells, 0.f);
    for (int r = 0; r < rows_; ++r)
        for (int c = 0; c < cols_; ++c) {
            float u = (c + 0.5f) / cols_, v = (r + 0.5f) / rows_, h = 0.f;
            for (const Wave& w : waves) h += w.amplitude * std::cos(kTau * (w.kx * u + w.ky * v) + w.phase);
            elevation_[r * cols_ + c] = h;
        }
    // sea level where the requested share of the world is under water; then scale to -1..1
    std::vector<float> sorted = elevation_;
    auto at = sorted.begin() + static_cast<long>(std::clamp(settings.waterShare, 0.f, 1.f) * (cells - 1));
    std::nth_element(sorted.begin(), at, sorted.end());
    const float sea = settings.waterShare <= 0.f ? *std::min_element(sorted.begin(), sorted.end()) - 1.f : *at;
    const auto [lo, hi] = std::minmax_element(elevation_.begin(), elevation_.end());
    const float depth = std::max(1e-6f, sea - *lo), peak = std::max(1e-6f, *hi - sea);
    for (float& e : elevation_) e = e < sea ? (e - sea) / depth : (e - sea) / peak;

    mean_.assign(cells, 0.f);
    swing_.assign(cells, 0.f);
    for (int r = 0; r < rows_; ++r) {
        // 0 at the poles (top and bottom edge), 1 at the equator (the middle row)
        const float tropic = 0.5f - 0.5f * std::cos(kTau * (r + 0.5f) / rows_);
        const float latitudeTemp = settings.poleTemp + (settings.equatorTemp - settings.poleTemp) * tropic;
        const float swing = settings.equatorSwing + (settings.poleSwing - settings.equatorSwing) * (1.f - tropic);
        for (int c = 0; c < cols_; ++c) {
            const int i = r * cols_ + c;
            if (elevation_[i] < 0.f) {
                mean_[i] = latitudeTemp + kWaterMildness * (kWaterTemp - latitudeTemp);
                swing_[i] = swing * kWaterSwing;
            } else {
                mean_[i] = latitudeTemp - settings.lapseRate * elevation_[i];
                swing_[i] = swing;
            }
        }
    }
    update(0, 0.f);
}

void Climate::update(int tick, float shift) {
    season_ = std::sin(kTau * static_cast<float>(tick) / static_cast<float>(std::max(1, settings_.yearLength)));
    shift_ = shift;
}

float Climate::sample(const std::vector<float>& map, Vec2 p) const {
    const float u = p.x / cellW_ - 0.5f, v = p.y / cellH_ - 0.5f;
    const float fu = std::floor(u), fv = std::floor(v);
    const float tx = u - fu, ty = v - fv;
    int c0 = static_cast<int>(fu), r0 = static_cast<int>(fv);
    c0 = (c0 % cols_ + cols_) % cols_;
    r0 = (r0 % rows_ + rows_) % rows_;
    const int c1 = c0 + 1 == cols_ ? 0 : c0 + 1, r1 = r0 + 1 == rows_ ? 0 : r0 + 1;
    const float top = map[r0 * cols_ + c0] + (map[r0 * cols_ + c1] - map[r0 * cols_ + c0]) * tx;
    const float bottom = map[r1 * cols_ + c0] + (map[r1 * cols_ + c1] - map[r1 * cols_ + c0]) * tx;
    return top + (bottom - top) * ty;
}

}  // namespace evo
