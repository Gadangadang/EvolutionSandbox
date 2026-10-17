#pragma once

#include <vector>

#include "grid.hpp"
#include "rng.hpp"

namespace evo {

struct ClimateSettings {
    float waterShare = 0.3f;     // fraction of the world under water
    float poleTemp = -12.f;      // yearly mean at the poles (top and bottom edges), degrees C
    float equatorTemp = 32.f;    // yearly mean at the equator (the middle row)
    float lapseRate = 10.f;      // degrees colder on the highest mountains
    float equatorSwing = 2.f;    // seasonal swing (amplitude) at the equator ...
    float poleSwing = 12.f;      // ... and at the poles, as on Earth
    int yearLength = 6000;       // ticks per year: about one lifetime
};

// Land, water and temperature. Temperature falls from the equator toward the poles, which
// meet at the top and bottom edges since the world wraps; it falls with altitude, swings with
// the seasons (more toward the poles) and is milder over water. All maps tile seamlessly.
class Climate {
public:
    static constexpr float kCell = 12.f;  // map resolution in world units

    void generate(float width, float height, const ClimateSettings& settings, Rng& rng);
    void update(int tick, float shift);  // season and any global warming or cooling

    // < 0: water (down to -1 at the deepest), > 0: land (up to 1 at the highest peak)
    float elevation(Vec2 p) const { return sample(elevation_, p); }
    bool isWater(Vec2 p) const { return elevation(p) < 0.f; }
    float temperature(Vec2 p) const { return sample(mean_, p) + sample(swing_, p) * season_ + shift_; }
    float meanTemperature(Vec2 p) const { return sample(mean_, p) + shift_; }
    float seasonalSwing(Vec2 p) const { return sample(swing_, p); }

    // the maps themselves, for drawing
    int cols() const { return cols_; }
    int rows() const { return rows_; }
    float elevationAt(int cell) const { return elevation_[cell]; }
    float temperatureAt(int cell) const { return mean_[cell] + swing_[cell] * season_ + shift_; }

    float season() const { return season_; }  // -1 midwinter .. 1 midsummer
    float shift() const { return shift_; }    // global warming (+) or cooling (-) so far
    const ClimateSettings& settings() const { return settings_; }

private:
    float sample(const std::vector<float>& map, Vec2 p) const;  // bilinear, wrapping

    ClimateSettings settings_;
    int cols_ = 1, rows_ = 1;
    float cellW_ = 1.f, cellH_ = 1.f;
    std::vector<float> elevation_, mean_, swing_;
    float season_ = 0.f, shift_ = 0.f;
};

}  // namespace evo
