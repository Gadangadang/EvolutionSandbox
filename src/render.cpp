#include "render.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace evo {

namespace {

constexpr Color kBackground = {11, 14, 19, 255};
constexpr Color kWorld = {17, 23, 30, 255};
constexpr Color kGrid = {25, 33, 42, 255};
constexpr Color kPanel = {15, 19, 25, 255};
constexpr Color kCard = {21, 27, 35, 255};
constexpr Color kText = {226, 232, 240, 255};
constexpr Color kDim = {132, 145, 162, 255};
constexpr Color kAccent = {255, 196, 92, 255};
constexpr Color kPlant = {126, 224, 136, 255};
constexpr Color kMeat = {214, 112, 90, 255};
constexpr Color kPredator = {255, 84, 84, 255};
constexpr Color kSick = {206, 232, 64, 255};
constexpr Color kImmune = {110, 176, 255, 255};
constexpr Color kPositive = {92, 162, 255, 255};
constexpr Color kNegative = {255, 104, 92, 255};
constexpr Color kGood = {120, 214, 140, 255};
constexpr Color kWater = {92, 196, 240, 255};
constexpr Color kLand = {214, 176, 110, 255};
constexpr Color kCold = {96, 160, 255, 255};
constexpr Color kHot = {255, 110, 70, 255};

constexpr float kRadToDeg = 57.2957795f;
constexpr float kColdest = -20.f, kHottest = 40.f;  // the temperature colour scale

Color mix(Color a, Color b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    auto ch = [&](unsigned char x, unsigned char y) { return static_cast<unsigned char>(x + (y - x) * t); };
    return {ch(a.r, b.r), ch(a.g, b.g), ch(a.b, b.b), 255};
}

Color scale(Color c, float f) {
    auto ch = [&](unsigned char x) { return static_cast<unsigned char>(std::clamp(x * f, 0.f, 255.f)); };
    return {ch(c.r), ch(c.g), ch(c.b), c.a};
}

// A piecewise-linear palette: colours at given values, blended between them.
struct Stop {
    float at;
    Color colour;
};
template <size_t N>
Color ramp(const Stop (&stops)[N], float v) {
    if (v <= stops[0].at) return stops[0].colour;
    for (size_t i = 1; i < N; ++i)
        if (v <= stops[i].at) return mix(stops[i - 1].colour, stops[i].colour, (v - stops[i - 1].at) / (stops[i].at - stops[i - 1].at));
    return stops[N - 1].colour;
}

// temperature, cold blue to hot red
constexpr Stop kHeatStops[] = {{-20, {48, 84, 196, 255}},   {-5, {80, 150, 230, 255}}, {8, {90, 200, 190, 255}},
                               {18, {226, 222, 140, 255}}, {28, {244, 150, 64, 255}}, {40, {214, 56, 56, 255}}};
Color heatColour(float t) { return ramp(kHeatStops, t); }

// land by temperature, kept dark so creatures and food stand out on it
constexpr Stop kLandStops[] = {{-15, {118, 126, 138, 255}}, {-3, {78, 88, 86, 255}},  {8, {36, 66, 46, 255}},
                               {18, {56, 82, 44, 255}},     {27, {100, 94, 54, 255}}, {35, {124, 102, 66, 255}}};
const char* const kBiomeNames[] = {"snow", "tundra", "forest", "grass", "savanna", "desert"};
constexpr Color kShallow = {34, 76, 108, 255}, kDeep = {12, 30, 58, 255}, kIce = {92, 112, 132, 255};

Color biomeColour(float elevation, float temp) {
    if (elevation < 0.f) {
        Color sea = mix(kShallow, kDeep, -elevation * 1.4f);
        return mix(sea, kIce, std::min(0.5f, (-2.f - temp) / 12.f));  // icy when cold (it is still water)
    }
    return scale(ramp(kLandStops, temp), 1.f + 0.35f * elevation);  // peaks a little lighter
}

Color creatureColour(const Creature& c, ColourMode mode) {
    switch (mode) {
        case ColourMode::PreferredTemp: return heatColour(c.genome.preferredTemp);
        case ColourMode::Swimming: return mix(kLand, kWater, c.genome.swimming);
        case ColourMode::Comfort: {
            if (c.coldStress > 0.f) return mix({170, 236, 170, 255}, kCold, 0.25f + c.coldStress / 8.f);
            if (c.heatStress > 0.f) return mix({170, 236, 170, 255}, kHot, 0.25f + c.heatStress / 8.f);
            return {170, 236, 170, 255};
        }
        default: return ColorFromHSV(c.genome.hue, 0.58f, 0.96f);
    }
}

Color signedColour(float v, float alpha) { return Fade(v >= 0 ? kPositive : kNegative, alpha); }

const char* seasonName(int tick, int yearLength) {
    float phase = std::fmod(static_cast<float>(tick), static_cast<float>(yearLength)) / static_cast<float>(yearLength);
    if (phase < 0.125f || phase >= 0.875f) return "spring";
    if (phase < 0.375f) return "summer";
    if (phase < 0.625f) return "autumn";
    return "winter";
}

// The climate maps, drawn into a texture that is stretched over the world with smoothing. The
// texture is finer than the climate grid and samples it smoothly, so coasts are not jagged. It
// is refilled when the season has moved on a little or the map mode changes.
void drawMap(const World& world, MapMode mode) {
    constexpr float kPixel = 6.f;  // world units per texture pixel
    static Texture2D texture{};
    static int cols = 0, rows = 0, filledAt = -1;
    static MapMode filledMode = MapMode::Count;
    static std::vector<Color> pixels;
    const Settings& s = world.settings();
    const Climate& climate = world.climate();
    const int wantCols = static_cast<int>(s.width / kPixel), wantRows = static_cast<int>(s.height / kPixel);
    if (cols != wantCols || rows != wantRows) {
        if (texture.id != 0) UnloadTexture(texture);
        cols = wantCols;
        rows = wantRows;
        Image image = GenImageColor(cols, rows, BLACK);
        texture = LoadTextureFromImage(image);
        UnloadImage(image);
        SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
        pixels.resize(static_cast<size_t>(cols) * rows);
        filledAt = -1;
    }
    // (time running backwards means a new world)
    if (filledAt < 0 || world.tick() < filledAt || world.tick() - filledAt >= 20 || filledMode != mode) {
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c) {
                Vec2 p = {(c + 0.5f) * s.width / cols, (r + 0.5f) * s.height / rows};
                float t = climate.temperature(p), e = climate.elevation(p);
                pixels[r * cols + c] = mode == MapMode::Temperature ? scale(heatColour(t), e < 0.f ? 0.36f : 0.5f) : biomeColour(e, t);
            }
        UpdateTexture(texture, pixels.data());
        filledAt = world.tick();
        filledMode = mode;
    }
    DrawTexturePro(texture, {0, 0, static_cast<float>(cols), static_cast<float>(rows)}, {0, 0, s.width, s.height}, {0, 0}, 0, WHITE);
}

void drawSenses(const Creature& c) {
    // three eyes: shaded by how close the nearest food in each one is
    const float edges[4] = {-kFieldOfView, -kCentreHalfAngle, kCentreHalfAngle, kFieldOfView};
    const float vision = c.genome.vision;
    Vector2 centre = {c.pos.x, c.pos.y};
    for (int s = 0; s < 3; ++s) {
        float start = (c.heading + edges[s]) * kRadToDeg, end = (c.heading + edges[s + 1]) * kRadToDeg;
        DrawCircleSector(centre, vision, start, end, 12, Fade(kPlant, 0.05f + 0.35f * c.in[s]));
        if (c.in[3 + s] > 0) DrawCircleSector(centre, vision * c.in[3 + s] * 0.35f, start, end, 12, Fade(kAccent, 0.35f));
    }
    DrawCircleSectorLines(centre, vision, (c.heading - kFieldOfView) * kRadToDeg, (c.heading + kFieldOfView) * kRadToDeg, 24,
                          Fade(kText, 0.25f));
}

void drawWorld(const World& world, const View& view, int screenW, int screenH) {
    const Settings& s = world.settings();
    BeginScissorMode(0, 0, screenW - kPanelWidth, screenH);
    BeginMode2D(view.camera);
    if (view.map == MapMode::Plain) {
        DrawRectangleV({0, 0}, {s.width, s.height}, kWorld);
        for (float x = 300; x < s.width; x += 300) DrawLineV({x, 0}, {x, s.height}, kGrid);
        for (float y = 300; y < s.height; y += 300) DrawLineV({0, y}, {s.width, y}, kGrid);
    } else {
        drawMap(world, view.map);
    }
    for (const auto& p : world.patches()) DrawCircleV({p.pos.x, p.pos.y}, p.radius * 1.2f, Fade(kPlant, 0.035f));

    for (const auto& f : world.food()) {
        if (f.meat) DrawCircleV({f.pos.x, f.pos.y}, 3.2f, kMeat);
        else DrawCircleV({f.pos.x, f.pos.y}, 3.f, kPlant);
    }

    const Creature* selected = world.find(view.selected);
    if (selected && view.showSenses) drawSenses(*selected);

    const float pulse = 0.6f + 0.4f * std::sin(static_cast<float>(GetTime()) * 6.f);
    for (const auto& c : world.creatures()) {
        Vector2 p = {c.pos.x, c.pos.y};
        float r = c.radius();
        Color col = creatureColour(c, view.colours);
        if (c.sick()) DrawCircleV(p, r + 7.f, Fade(kSick, 0.18f * pulse));
        Vector2 nose = {p.x + std::cos(c.heading) * (r + 5), p.y + std::sin(c.heading) * (r + 5)};
        DrawLineEx(p, nose, 2.f, col);
        DrawCircleV(p, r + 1.f, Fade(kBackground, 0.7f));  // a dark outline keeps bodies visible on any terrain
        DrawCircleV(p, r, col);
        DrawCircleV({p.x + std::cos(c.heading) * r * 0.5f, p.y + std::sin(c.heading) * r * 0.5f}, r * 0.28f, kWorld);
        // rims: sick (yellow-green), immune (blue), hunter (red), so the body colour still shows
        if (c.sick()) DrawRing(p, r + 1.5f, r + 3.f, 0, 360, 24, Fade(kSick, 0.9f));
        else if (c.health == Health::Recovered) DrawRing(p, r + 1.5f, r + 2.3f, 0, 360, 24, Fade(kImmune, 0.6f));
        else if (c.carnivore()) DrawRing(p, r + 1.5f, r + 2.5f, 0, 360, 24, Fade(kPredator, 0.8f));
        else if (c.attacking) DrawRing(p, r + 1.5f, r + 2.2f, 0, 360, 24, Fade(kPredator, 0.35f));
    }
    if (selected) DrawRing({selected->pos.x, selected->pos.y}, selected->radius() + 6, selected->radius() + 8, 0, 360, 32, kText);
    EndMode2D();
    EndScissorMode();
}

// A colour bar from a palette, with labels under it.
void colourBar(int x, int y, int w, float lo, float hi, Color (*colour)(float), const char* loLabel, const char* hiLabel) {
    for (int i = 0; i < w; ++i) DrawRectangle(x + i, y, 1, 8, colour(lo + (hi - lo) * i / (w - 1)));
    DrawText(loLabel, x, y + 11, 10, kDim);
    DrawText(hiLabel, x + w - MeasureText(hiLabel, 10), y + 11, 10, kDim);
}

Color swimColour(float v) { return mix(kLand, kWater, v); }
Color comfortColour(float v) {  // -1 too cold .. 1 too hot
    return v < 0 ? mix({170, 236, 170, 255}, kCold, -v) : mix({170, 236, 170, 255}, kHot, v);
}

// Bottom-left of the world view: what the map and the colours mean, and the season.
void drawLegend(const World& world, const View& view, int screenH) {
    const int w = 370, h = 74, x = 12, y = screenH - h - 12;
    DrawRectangle(x, y, w, h, Fade(kPanel, 0.88f));
    DrawRectangleLines(x, y, w, h, kGrid);
    const Settings& s = world.settings();
    const Climate& climate = world.climate();
    const char* warming = s.warming > 0 ? "warming" : s.warming < 0 ? "cooling" : "steady";
    DrawText(TextFormat("%s   global %+.1f C   %s [W]", s.climate ? seasonName(world.tick(), climate.settings().yearLength) : "no climate",
                        climate.shift(), warming),
             x + 10, y + 8, 10, kText);
    DrawText(TextFormat("map: %s [M]    colours: %s [K]", mapModeName(view.map), colourModeName(view.colours)), x + 10, y + 22, 10,
             kDim);
    // a key for whatever needs one: the creature colours first, then the map
    const int bx = x + 10, by = y + 40, bw = w - 20;
    switch (view.colours) {
        case ColourMode::PreferredTemp: colourBar(bx, by, bw, kColdest, kHottest, heatColour, "prefers -20 C", "prefers 40 C"); return;
        case ColourMode::Swimming: colourBar(bx, by, bw, 0.f, 1.f, swimColour, "land animal", "water animal"); return;
        case ColourMode::Comfort: colourBar(bx, by, bw, -1.f, 1.f, comfortColour, "too cold", "too hot"); return;
        default: break;
    }
    if (view.map == MapMode::Temperature) {
        colourBar(bx, by, bw, kColdest, kHottest, heatColour, "-20 C", "40 C");
    } else if (view.map == MapMode::Biomes) {
        const int sw = bw / 7;
        DrawRectangle(bx, by, sw - 4, 8, kShallow);
        DrawText("water", bx, by + 11, 10, kDim);
        for (int i = 0; i < 6; ++i) {
            DrawRectangle(bx + sw * (i + 1), by, sw - 4, 8, kLandStops[i].colour);
            DrawText(kBiomeNames[i], bx + sw * (i + 1), by + 11, 10, kDim);
        }
    }
}

struct Series {
    float (*value)(const Sample&);
    Color colour;
};

// A small line chart; several series share one scale. `zeroBased` puts the bottom at 0;
// a fixed range (lo < hi) overrides both.
void chart(Rectangle r, const std::vector<Sample>& history, std::initializer_list<Series> series, const char* label,
           const char* format, bool zeroBased = false, float fixedLo = 0.f, float fixedHi = 0.f) {
    DrawRectangleRec(r, kCard);
    DrawText(label, static_cast<int>(r.x) + 8, static_cast<int>(r.y) + 5, 10, kDim);
    if (history.size() < 2) return;
    float lo = zeroBased ? 0.f : std::numeric_limits<float>::max(), hi = std::numeric_limits<float>::lowest();
    if (fixedLo < fixedHi) {
        lo = fixedLo;
        hi = fixedHi;
    } else {
        for (const auto& s : history)
            for (const auto& line : series) {
                lo = std::min(lo, line.value(s));
                hi = std::max(hi, line.value(s));
            }
    }
    if (hi - lo < 1e-6f) hi = lo + 1.f;
    const float top = r.y + 18, bottom = r.y + r.height - 5, left = r.x + 8, width = r.width - 16;
    if (lo < 0.f && hi > 0.f) {  // a zero line when the scale crosses zero
        float y0 = bottom - (0.f - lo) / (hi - lo) * (bottom - top);
        DrawLineV({left, y0}, {left + width, y0}, kGrid);
    }
    const size_t n = history.size();
    for (const auto& line : series) {
        auto point = [&](size_t i) {
            float v = std::clamp(line.value(history[i]), lo, hi);
            return Vector2{left + width * static_cast<float>(i) / static_cast<float>(n - 1), bottom - (v - lo) / (hi - lo) * (bottom - top)};
        };
        for (size_t i = 1; i < n; ++i) DrawLineEx(point(i - 1), point(i), 1.5f, line.colour);
        DrawCircleV(point(n - 1), 2.5f, line.colour);
    }
    const Series& first = *series.begin();
    const char* now = TextFormat(format, first.value(history.back()));
    DrawText(now, static_cast<int>(r.x + r.width) - MeasureText(now, 10) - 8, static_cast<int>(r.y) + 5, 10, first.colour);
}

// One dot per creature: a gene against the place it lives. `diagonal` draws y = x, the line
// of perfect fit for preferred against local temperature.
void scatter(Rectangle r, const World& world, float (*x)(const World&, const Creature&), float (*y)(const Creature&), float xlo,
             float xhi, float ylo, float yhi, const char* label, const char* xLabel, float correlation, Color colour,
             bool diagonal = false) {
    DrawRectangleRec(r, kCard);
    DrawText(label, static_cast<int>(r.x) + 8, static_cast<int>(r.y) + 5, 10, kDim);
    const char* rText = TextFormat("r %+.2f", correlation);
    DrawText(rText, static_cast<int>(r.x + r.width) - MeasureText(rText, 10) - 8, static_cast<int>(r.y) + 5, 10, colour);
    const float top = r.y + 19, bottom = r.y + r.height - 16, left = r.x + 8, right = r.x + r.width - 8;
    DrawRectangleLinesEx({left, top, right - left, bottom - top}, 1.f, kGrid);
    auto px = [&](float v) { return left + (std::clamp(v, xlo, xhi) - xlo) / (xhi - xlo) * (right - left); };
    auto py = [&](float v) { return bottom - (std::clamp(v, ylo, yhi) - ylo) / (yhi - ylo) * (bottom - top); };
    if (diagonal) DrawLineV({px(std::max(xlo, ylo)), py(std::max(xlo, ylo))}, {px(std::min(xhi, yhi)), py(std::min(xhi, yhi))}, Fade(kText, 0.25f));
    for (const auto& c : world.creatures()) DrawCircleV({px(x(world, c)), py(y(c))}, 1.6f, Fade(colour, 0.65f));
    DrawText(xLabel, static_cast<int>(left), static_cast<int>(bottom) + 3, 10, kDim);
}

void drawBrain(const Creature& c, Rectangle r) {
    DrawRectangleRec(r, kCard);
    const float xIn = r.x + 66, xHidden = r.x + r.width * 0.54f, xOut = r.x + r.width - 62;
    auto yAt = [&](int i, int count) { return r.y + 10 + (r.height - 20) * (static_cast<float>(i) + 0.5f) / count; };
    // connections, drawn by how much signal flows through them this tick
    for (int i = 0; i < kInputs; ++i)
        for (int h = 0; h < kHidden; ++h) {
            float flow = c.genome.brain.w1[i * kHidden + h] * c.in[i];
            DrawLineEx({xIn, yAt(i, kInputs)}, {xHidden, yAt(h, kHidden)}, 1.f, signedColour(flow, std::min(0.85f, 0.04f + std::fabs(flow) * 0.6f)));
        }
    for (int h = 0; h < kHidden; ++h)
        for (int o = 0; o < kOutputs; ++o) {
            float flow = c.genome.brain.w2[h * kOutputs + o] * c.hidden[h];
            DrawLineEx({xHidden, yAt(h, kHidden)}, {xOut, yAt(o, kOutputs)}, 1.f, signedColour(flow, std::min(0.85f, 0.04f + std::fabs(flow) * 0.6f)));
        }
    auto node = [&](float x, float y, float v) {
        DrawCircleV({x, y}, 4.5f, kPanel);
        DrawCircleV({x, y}, 4.5f, signedColour(v, 0.15f + 0.85f * std::min(1.f, std::fabs(v))));
        DrawCircleLinesV({x, y}, 4.5f, Fade(kText, 0.35f));
    };
    for (int i = 0; i < kInputs; ++i) {
        node(xIn, yAt(i, kInputs), c.in[i]);
        DrawText(kInputNames[i], static_cast<int>(xIn) - 10 - MeasureText(kInputNames[i], 10), static_cast<int>(yAt(i, kInputs)) - 5, 10, kDim);
    }
    for (int h = 0; h < kHidden; ++h) node(xHidden, yAt(h, kHidden), c.hidden[h]);
    for (int o = 0; o < kOutputs; ++o) {
        node(xOut, yAt(o, kOutputs), c.out[o]);
        DrawText(kOutputNames[o], static_cast<int>(xOut) + 11, static_cast<int>(yAt(o, kOutputs)) - 5, 10, kText);
    }
}

Color eventColour(int kind) {
    switch (kind) {
        case 1: return kGood;
        case -1: return kNegative;
        case 2: return kSick;
        default: return kDim;
    }
}

float localMean(const World& w, const Creature& c) { return w.climate().meanTemperature(c.pos); }
float localSwing(const World& w, const Creature& c) { return w.climate().seasonalSwing(c.pos); }

// Returns the y below the charts.
int lifeCharts(const std::vector<Sample>& h, int x, int y, int w) {
    const float ch = 46, gap = 5;
    auto row = [&](float height) { Rectangle r{float(x), float(y), float(w), height}; y += int(height + gap); return r; };
    chart(row(ch + 8), h,
          {{[](const Sample& s) { return float(s.population); }, kText},
           {[](const Sample& s) { return float(s.infected); }, kSick},
           {[](const Sample& s) { return float(s.recovered); }, kImmune}},
          "POPULATION   sick   immune", "%.0f", true);
    chart(row(ch), h, {{[](const Sample& s) { return s.foraging; }, kPlant}}, "FORAGING (plants per creature per 1000 ticks)", "%.1f");
    chart(row(ch), h, {{[](const Sample& s) { return s.avgSize; }, kAccent}}, "BODY SIZE", "%.2f");
    chart(row(ch), h, {{[](const Sample& s) { return s.carnivoreShare * 100.f; }, kPredator}}, "HUNTERS %", "%.0f%%", true);
    chart(row(ch), h, {{[](const Sample& s) { return s.avgResistance; }, kImmune}}, "DISEASE RESISTANCE", "%.2f", true);
    chart(row(ch), h, {{[](const Sample& s) { return s.avgVision; }, kText}}, "VISION RANGE", "%.0f");
    return y;
}

int climateCharts(const World& world, int x, int y, int w) {
    const auto& h = world.history();
    const Sample last = h.empty() ? Sample{} : h.back();
    const float sh = 92, ch = 46, gap = 5;
    const float half = (w - gap) / 2;
    auto pair = [&]() {
        Rectangle l{float(x), float(y), half, sh}, r{float(x) + half + gap, float(y), half, sh};
        y += int(sh + gap);
        return std::pair{l, r};
    };
    auto row = [&](float height) { Rectangle r{float(x), float(y), float(w), height}; y += int(height + gap); return r; };
    auto [a, b] = pair();
    scatter(a, world, localMean, [](const Creature& c) { return c.genome.preferredTemp; }, kColdest, kHottest, kColdest, kHottest,
            "PREFERS vs LIVES", "local mean C", last.localFit, kAccent, true);
    scatter(b, world, localMean, [](const Creature& c) { return c.genome.size; }, kColdest, kHottest, 0.5f, 2.5f, "SIZE vs LIVES",
            "local mean C", last.sizeVsTemp, kText);
    auto [c, d] = pair();
    scatter(c, world, localSwing, [](const Creature& cr) { return cr.genome.tolerance; }, 0.f, 12.f, 0.f, 20.f, "TOLERANCE vs SWING",
            "seasonal swing C", last.toleranceVsSwing, kGood);
    chart(d, h,
          {{[](const Sample& s) { return s.swimInWater; }, kWater}, {[](const Sample& s) { return s.swimOnLand; }, kLand}},
          "SWIM: water / land", "%.2f", false, 0.f, 1.f);
    chart(row(ch), h,
          {{[](const Sample& s) { return s.avgPreferred; }, kAccent},
           {[](const Sample& s) { return s.avgPreferred + s.sdPreferred; }, Fade(kAccent, 0.35f)},
           {[](const Sample& s) { return s.avgPreferred - s.sdPreferred; }, Fade(kAccent, 0.35f)}},
          "PREFERRED TEMPERATURE (mean, +-1 sd)", "%.1f C");
    chart(row(ch), h,
          {{[](const Sample& s) { return s.avgTolerance; }, kGood}, {[](const Sample& s) { return s.avgMismatch; }, kHot}},
          "TOLERANCE   degrees outside comfort", "+-%.1f C", true);
    chart(row(ch), h, {{[](const Sample& s) { return s.avgMutation; }, kText}}, "MUTATION RATE (gene)", "%.3f");
    return y;
}

void drawPanel(const World& world, const View& view, int screenW, int screenH) {
    const int x = screenW - kPanelWidth, pad = 16, w = kPanelWidth - 2 * pad;
    DrawRectangle(x, 0, kPanelWidth, screenH, kPanel);
    DrawLine(x, 0, x, screenH, kGrid);
    int y = 14;
    DrawText("EVOLUTION SANDBOX", x + pad, y, 20, kAccent);
    y += 24;
    DrawText(TextFormat("tick %d   speed %dx%s", world.tick(), view.speed, view.paused ? "   PAUSED" : ""), x + pad, y, 10, kDim);
    y += 16;

    const auto& h = world.history();
    const Sample last = h.empty() ? Sample{} : h.back();
    const int pop = static_cast<int>(world.creatures().size());
    DrawText(TextFormat("%d creatures   %d food   gen %d (max %d)", pop, static_cast<int>(world.food().size()),
                        static_cast<int>(std::lround(last.avgGeneration)), last.maxGeneration),
             x + pad, y, 10, kText);
    y += 14;
    DrawText(TextFormat("%d sick   %d immune   %d outbreaks   %d died of disease", last.infected, last.recovered,
                        world.outbreaks(), world.diseaseDeaths()),
             x + pad, y, 10, kSick);
    y += 14;
    DrawText(TextFormat("big mutations: %d helped   %d hurt   %d unclear", world.bigHelped(), world.bigHurt(), world.bigNeutral()),
             x + pad, y, 10, kDim);
    y += 14;
    DrawText(TextFormat("hunting %s   disease %s   [Tab] %s charts", world.settings().predation ? "on" : "off",
                        world.settings().disease ? "on" : "off", view.climatePage ? "life" : "climate"),
             x + pad, y, 10, kDim);
    y += 18;

    y = view.climatePage ? climateCharts(world, x + pad, y, w) : lifeCharts(h, x + pad, y, w);
    y += 2;

    DrawText("EVENTS", x + pad, y, 10, kAccent);
    y += 14;
    int shown = 0;
    for (const auto& e : world.events()) {
        if (shown++ >= 4) break;
        const char* line = TextFormat("%6d  %s", e.tick, e.text.c_str());
        DrawText(line, x + pad, y, 10, eventColour(e.kind));
        y += 13;
    }
    if (world.events().empty()) {
        DrawText("Nothing yet. Big mutations and outbreaks appear here.", x + pad, y, 10, kDim);
        y += 13;
    }
    y += 8;

    if (const Creature* c = world.find(view.selected)) {
        DrawText(TextFormat("CREATURE #%u", c->id), x + pad, y, 10, kAccent);
        DrawCircleV({float(x + pad + 112), float(y + 5)}, 5, creatureColour(*c, view.colours));
        y += 14;
        DrawText(TextFormat("gen %d  age %d  energy %.0f  size %.2f  vision %.0f", c->generation, c->age, c->energy,
                            c->genome.size, c->genome.vision),
                 x + pad, y, 10, kText);
        y += 13;
        const char* health = c->sick() ? TextFormat("SICK %d ticks left (strain: spreads %.3f, harms %.3f)", c->healthTimer,
                                                    c->strain.infectiousness, c->strain.virulence)
                             : c->health == Health::Recovered ? TextFormat("immune for %d more ticks", c->healthTimer)
                                                              : "healthy";
        DrawText(TextFormat("resistance %.2f   %s", c->genome.resistance, health), x + pad, y, 10, c->sick() ? kSick : kDim);
        y += 13;
        const Genome& g = c->genome;
        const char* comfort = c->coldStress > 0 ? TextFormat("%.1f too cold", c->coldStress)
                              : c->heatStress > 0 ? TextFormat("%.1f too hot", c->heatStress)
                                                  : "comfortable";
        DrawText(TextFormat("likes %.1f C +-%.1f  feels %.1f C: %s", g.preferredTemp, g.tolerance, c->felt, comfort), x + pad, y, 10,
                 c->coldStress > 0 ? kCold : c->heatStress > 0 ? kHot : kDim);
        y += 13;
        DrawText(TextFormat("swim %.2f %s  eaten %d  kills %d  kids %d  mut %.3f", g.swimming, c->inWater ? "(in water)" : "(on land)",
                            c->eaten, c->kills, c->children, g.mutation),
                 x + pad, y, 10, kDim);
        y += 16;
        float brainH = std::max(120.f, std::min(240.f, float(screenH - y - 13 * 4 - 22)));
        drawBrain(*c, {float(x + pad), float(y), float(w), brainH});
    } else {
        DrawText("Click a creature to see its brain.", x + pad, y, 10, kDim);
    }

    const char* help[] = {"space pause  1-6 speed  O oldest  G top gen  click select",
                          "F follow  V senses  drag/scroll move  C fit  R new world",
                          "I infect  D disease  P hunting  +/- food  W warming",
                          "M map  K colours  Tab charts"};
    int hy = screenH - 12 - 13 * 4;
    for (const char* line : help) {
        DrawText(line, x + pad, hy, 10, kDim);
        hy += 13;
    }
}

}  // namespace

const char* mapModeName(MapMode m) {
    switch (m) {
        case MapMode::Biomes: return "biomes";
        case MapMode::Temperature: return "temperature";
        default: return "plain";
    }
}

const char* colourModeName(ColourMode m) {
    switch (m) {
        case ColourMode::PreferredTemp: return "preferred temperature";
        case ColourMode::Swimming: return "swimming";
        case ColourMode::Comfort: return "comfort now";
        default: return "lineage";
    }
}

void fitCamera(View& view, const World& world, int screenW, int screenH) {
    const Settings& s = world.settings();
    float areaW = float(screenW - kPanelWidth), areaH = float(screenH);
    view.camera.zoom = std::min(areaW / s.width, areaH / s.height) * 0.96f;
    view.camera.target = {s.width / 2, s.height / 2};
    view.camera.offset = {areaW / 2, areaH / 2};
    view.camera.rotation = 0;
}

void drawFrame(const World& world, const View& view, int screenW, int screenH) {
    ClearBackground(kBackground);
    drawWorld(world, view, screenW, screenH);
    drawLegend(world, view, screenH);
    drawPanel(world, view, screenW, screenH);
}

}  // namespace evo
