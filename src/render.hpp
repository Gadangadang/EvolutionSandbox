#pragma once

#include <cstdint>

#include "raylib.h"
#include "world.hpp"

namespace evo {

constexpr int kPanelWidth = 360;

enum class MapMode { Biomes, Temperature, Plain, Count };
enum class ColourMode { Lineage, PreferredTemp, Swimming, Comfort, Count };

struct View {
    Camera2D camera{};
    MapMode map = MapMode::Biomes;
    ColourMode colours = ColourMode::Lineage;
    bool climatePage = false;  // side panel: life charts or climate charts
    uint32_t selected = 0;
    bool follow = false;
    bool showSenses = true;
    bool paused = false;
    int speed = 1;  // simulation ticks per frame
};

// Fit the whole world into the area left of the side panel.
void fitCamera(View& view, const World& world, int screenW, int screenH);
void drawFrame(const World& world, const View& view, int screenW, int screenH);
const char* mapModeName(MapMode m);
const char* colourModeName(ColourMode m);

}  // namespace evo
