// Evolution Sandbox: creatures with small neural-network brains forage, hunt, reproduce and
// mutate. Nothing is programmed in about how to find food: it evolves.
//
//   ./evolution_sandbox                        open the window
//   ./evolution_sandbox --seed 7               a reproducible world
//   ./evolution_sandbox --headless --ticks 100000   run without a window, print progress
//   ./evolution_sandbox --ticks 50000 --snap shot.png   run, then save one frame as an image
//   options: --scale 4 (4x the area, food and population), --threads N, --warming 2 (degrees C
//   per 10,000 ticks), --warm-after T (headless: start warming at tick T), --no-climate, --no-water; for snapshots: --map biomes|temperature|plain,
//   --colours lineage|temp|swim|comfort, --climate-page

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "parallel.hpp"
#include "raylib.h"
#include "render.hpp"
#include "world.hpp"

using namespace evo;

namespace {

struct Options {
    uint32_t seed = std::random_device{}();
    bool headless = false;
    int ticks = 0;
    float scale = 1.f;  // world area, food and starting population all multiplied by this
    float warming = 0.f;
    int warmAfter = 0;  // headless: steady climate until this tick, then warming
    bool climate = true, water = true;
    MapMode map = MapMode::Biomes;
    ColourMode colours = ColourMode::Lineage;
    bool climatePage = false;
    std::string snap;
};

Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a value\n", arg.c_str());
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--seed") o.seed = static_cast<uint32_t>(std::strtoul(next(), nullptr, 10));
        else if (arg == "--ticks") o.ticks = std::atoi(next());
        else if (arg == "--snap") o.snap = next();
        else if (arg == "--threads") setThreadCount(std::atoi(next()));
        else if (arg == "--scale") o.scale = std::max(0.1f, static_cast<float>(std::atof(next())));
        else if (arg == "--headless") o.headless = true;
        else if (arg == "--warming") o.warming = static_cast<float>(std::atof(next()));
        else if (arg == "--warm-after") o.warmAfter = std::atoi(next());
        else if (arg == "--no-climate") o.climate = false;
        else if (arg == "--no-water") o.water = false;
        else if (arg == "--climate-page") o.climatePage = true;
        else if (arg == "--map") {
            std::string m = next();
            o.map = m == "temperature" ? MapMode::Temperature : m == "plain" ? MapMode::Plain : MapMode::Biomes;
        } else if (arg == "--colours") {
            std::string m = next();
            o.colours = m == "temp" ? ColourMode::PreferredTemp : m == "swim" ? ColourMode::Swimming
                        : m == "comfort" ? ColourMode::Comfort : ColourMode::Lineage;
        }
        else {
            std::fprintf(stderr, "unknown option %s\n", arg.c_str());
            std::exit(2);
        }
    }
    return o;
}

// A fair test of foraging skill: copies of the given brains, all the same size, never hungry
// and not hunting, in the same arena with plenty of food, all land and no climate to cope with. Returns plants eaten per creature
// per 1000 ticks, so brains from different moments (or random ones) can be compared.
float skillTest(const std::vector<Genome>& genomes) {
    Settings s;
    s.startCreatures = 0;
    s.minPopulation = 0;
    s.predation = false;
    s.disease = false;
    s.lifeCycle = false;
    s.climate = false;
    s.water = false;
    s.foodPerTick = 3.f;
    s.maxFood = 1200;
    World arena(s, 12345);
    for (Genome g : genomes) {
        g.size = 1.f;
        g.swimming = 0.f;  // the arena is all land: test the brain, not the legs
        arena.addCreature(g);
    }
    const int ticks = 2000;
    for (int t = 0; t < ticks; ++t) arena.step();
    return 1000.f * static_cast<float>(arena.plantsEaten()) / (static_cast<float>(genomes.size()) * ticks);
}

std::vector<Genome> sampleGenomes(const World& world, size_t n) {
    std::vector<Genome> out;
    const auto& cs = world.creatures();
    for (size_t i = 0; i < n && !cs.empty(); ++i) out.push_back(cs[i * cs.size() / n].genome);
    return out;
}

void printHeader() {
    std::printf("%8s %6s %5s %5s %6s %7s %6s %5s %6s %5s %6s | %6s %5s %5s %5s %5s %6s %5s %5s\n", "tick", "alive", "sick", "size",
                "vision", "hunters", "resist", "gen", "harm", "mut", "skill", "pref", "+-sd", "tol", "swim", "water", "misfit",
                "fit r", "berg");
}

void printProgress(const World& world, float skill) {
    const Sample& s = world.history().back();
    std::printf("%8d %6d %5d %5.2f %6.0f %6.0f%% %6.2f %5d %6.3f %5.3f %6.1f | %6.1f %5.1f %5.1f %5.2f %4.0f%% %6.2f %5.2f %5.2f\n",
                s.tick, s.population, s.infected, s.avgSize, s.avgVision, s.carnivoreShare * 100.f, s.avgResistance,
                s.maxGeneration, s.avgVirulence, s.avgMutation, skill, s.avgPreferred, s.sdPreferred, s.avgTolerance,
                s.avgSwimming, s.inWater * 100.f, s.avgMismatch, s.localFit, s.sizeVsTemp);
}

// What evolution did with the climate, and how each kind of big mutation fared.
void printReport(const World& world) {
    const Sample& s = world.history().back();
    std::printf("\nClimate adaptation at tick %d (season %+.2f, global shift %+.1f C):\n", s.tick, s.season, s.climateShift);
    std::printf("  preferred temperature %.1f C (spread %.1f), tolerance +-%.1f C, %.2f degrees outside comfort on average\n",
                s.avgPreferred, s.sdPreferred, s.avgTolerance, s.avgMismatch);
    std::printf("  they live where the yearly mean was %.1f C before any warming\n", s.livesAt);
    std::printf("  local adaptation: r(preferred temp, local yearly mean) = %+.2f   (1 = everyone fits where they live)\n", s.localFit);
    std::printf("  Bergmann's rule:  r(body size, local yearly mean)      = %+.2f   (negative = bigger in the cold)\n", s.sizeVsTemp);
    std::printf("  climate variability: r(tolerance, seasonal swing)     = %+.2f   (positive = broader where seasons swing more)\n",
                s.toleranceVsSwing);
    std::printf("  water: %.0f%% of creatures swim; swimming gene %.2f in water vs %.2f on land\n", s.inWater * 100.f,
                s.swimInWater, s.swimOnLand);
    std::printf("\nBig mutations, judged at death (helped / hurt / unclear):\n");
    for (int k = 1; k <= kBigMutationKinds; ++k) {
        const auto& o = world.bigOutcomes(static_cast<BigMutation>(k));
        std::printf("  %-15s %3d / %3d / %3d\n", traitName(static_cast<BigMutation>(k)), o.helped, o.hurt, o.unclear);
    }
}

uint32_t pick(const World& world, bool byGeneration) {
    const Creature* best = nullptr;
    for (const auto& c : world.creatures())
        if (!best || (byGeneration ? c.generation > best->generation : c.age > best->age)) best = &c;
    return best ? best->id : 0;
}

}  // namespace

int main(int argc, char** argv) {
    Options opt = parse(argc, argv);
    Settings settings;
    settings.width *= std::sqrt(opt.scale);
    settings.height *= std::sqrt(opt.scale);
    settings.foodPerTick *= opt.scale;
    settings.maxFood = static_cast<int>(settings.maxFood * opt.scale);
    settings.patches = std::max(1, static_cast<int>(std::lround(settings.patches * opt.scale)));
    settings.startCreatures = static_cast<int>(settings.startCreatures * opt.scale);
    settings.minPopulation = static_cast<int>(settings.minPopulation * opt.scale);
    settings.warming = opt.headless && opt.warmAfter > 0 ? 0.f : opt.warming;
    settings.climate = opt.climate;
    settings.water = opt.water;
    World world(settings, opt.seed);
    std::printf("seed %u\n", opt.seed);

    if (opt.headless) {
        Rng rng(opt.seed + 1);
        std::vector<Genome> randomBrains(40);
        for (auto& g : randomBrains) g.brain = Brain::random(rng);
        std::printf("skill test, random brains: %.1f plants per 1000 ticks\n\n", skillTest(randomBrains));
        printHeader();
        using Clock = std::chrono::steady_clock;
        Clock::duration simTime{};
        long creatureTicks = 0;
        for (int t = 0; t < opt.ticks; ++t) {
            if (opt.warmAfter > 0 && world.tick() == opt.warmAfter) world.settings().warming = opt.warming;
            auto start = Clock::now();
            world.step();
            simTime += Clock::now() - start;
            creatureTicks += static_cast<long>(world.creatures().size());
            if (world.tick() % 10000 == 0) printProgress(world, skillTest(sampleGenomes(world, 40)));
        }
        const double seconds = std::chrono::duration<double>(simTime).count();
        std::printf("\n%d outbreaks, %d deaths from disease. Big mutations judged at death: %d helped, %d hurt, %d unclear.\n",
                    world.outbreaks(), world.diseaseDeaths(), world.bigHelped(), world.bigHurt(), world.bigNeutral());
        std::printf("harm = average virulence of the strains in circulation (0 when no one is sick); mut = mutation rate gene\n");
        std::printf("pref/tol = preferred temperature and tolerance (C); misfit = degrees outside comfort; fit r, berg: see below\n");
        if (!world.history().empty()) printReport(world);
        if (opt.ticks > 0)
            std::printf("speed: %.0f ticks/s, %.2f us per creature per tick (simulation only, skill tests excluded)\n",
                        opt.ticks / seconds, 1e6 * seconds / std::max(1L, creatureTicks));
        return 0;
    }

    SetTraceLogLevel(LOG_WARNING);
    unsigned flags = FLAG_MSAA_4X_HINT;
    flags |= opt.snap.empty() ? (FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI) : FLAG_WINDOW_HIDDEN;
    SetConfigFlags(flags);
    InitWindow(1440, 900, "Evolution Sandbox");
    SetTargetFPS(60);

    View view;
    view.map = opt.map;
    view.colours = opt.colours;
    view.climatePage = opt.climatePage;
    fitCamera(view, world, GetScreenWidth(), GetScreenHeight());

    if (!opt.snap.empty()) {  // run, then save one frame without showing a window
        for (int t = 0; t < opt.ticks; ++t) world.step();
        view.selected = pick(world, false);
        const int w = GetScreenWidth(), h = GetScreenHeight();
        RenderTexture2D target = LoadRenderTexture(w, h);
        BeginTextureMode(target);
        drawFrame(world, view, w, h);
        EndTextureMode();
        Image image = LoadImageFromTexture(target.texture);
        ImageFlipVertical(&image);
        ExportImage(image, opt.snap.c_str());
        UnloadImage(image);
        UnloadRenderTexture(target);
        CloseWindow();
        std::printf("saved %s after %d ticks\n", opt.snap.c_str(), world.tick());
        return 0;
    }

    const int speeds[] = {1, 2, 4, 8, 16, 64};
    Vector2 pressAt = {0, 0};
    bool dragging = false;
    int lastW = GetScreenWidth(), lastH = GetScreenHeight();

    while (!WindowShouldClose()) {
        const int sw = GetScreenWidth(), sh = GetScreenHeight();
        if (sw != lastW || sh != lastH) {
            fitCamera(view, world, sw, sh);
            lastW = sw;
            lastH = sh;
        }
        const Vector2 mouse = GetMousePosition();
        const bool inWorld = mouse.x < sw - kPanelWidth;

        // keys
        if (IsKeyPressed(KEY_SPACE)) view.paused = !view.paused;
        for (int k = 0; k < 6; ++k)
            if (IsKeyPressed(KEY_ONE + k)) view.speed = speeds[k];
        if (IsKeyPressed(KEY_F)) view.follow = !view.follow;
        if (IsKeyPressed(KEY_V)) view.showSenses = !view.showSenses;
        if (IsKeyPressed(KEY_P)) world.settings().predation = !world.settings().predation;
        if (IsKeyPressed(KEY_D)) world.settings().disease = !world.settings().disease;
        if (IsKeyPressed(KEY_I)) world.startOutbreak(view.selected);
        if (IsKeyPressed(KEY_M)) view.map = static_cast<MapMode>((static_cast<int>(view.map) + 1) % static_cast<int>(MapMode::Count));
        if (IsKeyPressed(KEY_K))
            view.colours = static_cast<ColourMode>((static_cast<int>(view.colours) + 1) % static_cast<int>(ColourMode::Count));
        if (IsKeyPressed(KEY_TAB)) view.climatePage = !view.climatePage;
        if (IsKeyPressed(KEY_W)) {  // steady -> warming -> cooling -> steady (degrees C per 10,000 ticks)
            float& w = world.settings().warming;
            w = w == 0.f ? 2.f : w > 0.f ? -2.f : 0.f;
        }
        if (IsKeyPressed(KEY_C)) {
            view.follow = false;
            fitCamera(view, world, sw, sh);
        }
        if (IsKeyPressed(KEY_O)) view.selected = pick(world, false);
        if (IsKeyPressed(KEY_G)) view.selected = pick(world, true);
        if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD))
            world.settings().foodPerTick = std::min(4.f, world.settings().foodPerTick * 1.2f);
        if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT))
            world.settings().foodPerTick = std::max(0.1f, world.settings().foodPerTick / 1.2f);
        if (IsKeyPressed(KEY_R)) {
            world = World(settings, std::random_device{}());
            view.selected = 0;
            view.follow = false;
            fitCamera(view, world, sw, sh);
        }

        // mouse: scroll zooms toward the pointer, drag pans, a click selects
        if (inWorld) {
            float wheel = GetMouseWheelMove();
            if (wheel != 0) {
                view.camera.target = GetScreenToWorld2D(mouse, view.camera);
                view.camera.offset = mouse;
                view.camera.zoom = std::clamp(view.camera.zoom * (1.f + 0.12f * wheel), 0.15f, 10.f);
            }
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                pressAt = mouse;
                dragging = false;
            }
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
                Vector2 d = GetMouseDelta();
                if (std::fabs(mouse.x - pressAt.x) + std::fabs(mouse.y - pressAt.y) > 4 || IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
                    dragging = true;
                    view.follow = false;
                    view.camera.target.x -= d.x / view.camera.zoom;
                    view.camera.target.y -= d.y / view.camera.zoom;
                }
            }
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && !dragging) {
                Vector2 p = GetScreenToWorld2D(mouse, view.camera);
                uint32_t hit = 0;
                float best = std::pow(18.f / view.camera.zoom, 2.f);
                for (const auto& c : world.creatures()) {
                    float dx = c.pos.x - p.x, dy = c.pos.y - p.y, d2 = dx * dx + dy * dy;
                    float reach = std::pow(c.radius() + 6.f / view.camera.zoom, 2.f);
                    if (d2 < std::max(best, reach)) {
                        best = d2;
                        hit = c.id;
                    }
                }
                view.selected = hit;
            }
        }

        if (!view.paused)
            for (int s = 0; s < view.speed; ++s) world.step();

        if (view.follow) {
            if (const Creature* c = world.find(view.selected)) {
                view.camera.target = {c->pos.x, c->pos.y};
                view.camera.offset = {(sw - kPanelWidth) / 2.f, sh / 2.f};
            } else {
                view.follow = false;
            }
        }

        BeginDrawing();
        drawFrame(world, view, sw, sh);
        EndDrawing();
    }
    CloseWindow();
    return 0;
}
