#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "brain.hpp"
#include "climate.hpp"
#include "grid.hpp"
#include "rng.hpp"

namespace evo {

// Eyes: left, centre and right sectors around the heading. How far they reach is a gene.
constexpr float kCentreHalfAngle = 0.35f;  // radians, about 20 degrees
constexpr float kFieldOfView = 1.05f;      // radians either side, about 60 degrees
constexpr float kMinVision = 60.f;
constexpr float kMaxVision = 220.f;

struct Genome {
    Brain brain;
    float size = 1.f;        // bigger: stronger and can hunt, but slower and hungrier
    float vision = 130.f;    // how far the eyes reach; seeing farther costs energy
    float resistance = 0.f;  // 0..0.9: chance to shrug off an infection; costs energy to keep
    float hue = 0.f;         // colour, drifts each generation so families are visible
    float mutation = 0.08f;  // chance each weight changes in a child; itself evolves
    // climate: comfortable from preferredTemp - tolerance to preferredTemp + tolerance (degrees C)
    float preferredTemp = 16.f;
    float tolerance = 5.f;   // a broad comfort zone costs energy to keep
    float swimming = 0.15f;  // 0 land animal .. 1 water animal: fast in one means slow in the other
};

// The disease that infected creatures carry. It mutates a little every time it spreads.
struct Strain {
    float infectiousness = 0.03f;  // chance per tick of close contact to pass it on
    float virulence = 0.04f;       // energy the host loses per tick while sick
};

enum class Health : uint8_t { Susceptible, Infected, Recovered };

// Which trait a rare big mutation changed, so its outcome can be reported when the creature dies.
enum class BigMutation : uint8_t { None, Size, Vision, Resistance, Brain, PreferredTemp, Tolerance, Swimming, Count };
constexpr int kBigMutationKinds = static_cast<int>(BigMutation::Count) - 1;
const char* traitName(BigMutation m);

struct Creature {
    uint32_t id = 0;
    Genome genome;
    Vec2 pos;
    float heading = 0.f;
    float speed = 0.f;
    float energy = 0.f;
    float memory = 0.f;
    int age = 0;
    int generation = 0;
    int children = 0;
    int eaten = 0;
    int kills = 0;
    int attackTicks = 0;
    bool alive = true;
    bool eatenByPredator = false;
    bool attacking = false;
    Health health = Health::Susceptible;
    int healthTimer = 0;  // ticks left sick, or ticks of immunity left
    Strain strain;
    // climate as felt now: air temperature, plus body heat for big bodies
    float ambient = 0.f, felt = 0.f;
    float coldStress = 0.f, heatStress = 0.f;  // degrees outside the comfort zone
    bool inWater = false;
    BigMutation bigMutation = BigMutation::None;
    float bigBefore = 0.f, bigAfter = 0.f;
    // last tick's mind, for the brain view
    std::array<float, kInputs> in{};
    std::array<float, kHidden> hidden{};
    std::array<float, kOutputs> out{};

    float radius() const { return 4.f + 4.f * genome.size; }
    float mouth() const { return 5.f + 1.5f * genome.size; }  // how close food must be to eat it
    float maxSpeed() const;
    bool carnivore() const { return age > 100 && attackTicks > age / 2; }
    bool sick() const { return health == Health::Infected; }
};

struct Food {
    Vec2 pos;
    float energy = 0.f;
    bool meat = false;
    bool alive = true;
};

struct Patch {  // a fertile area that drifts slowly, so good spots move over time
    Vec2 pos, vel;
    float radius = 0.f;
};

struct Settings {
    float width = 2400.f;
    float height = 1500.f;
    float foodPerTick = 0.85f;
    int maxFood = 2200;
    int patches = 5;  // fertile areas
    int startCreatures = 120;
    int minPopulation = 20;  // below this, random newcomers arrive so life never dies out
    bool predation = true;
    bool disease = true;
    bool lifeCycle = true;   // off for the skill test: no hunger, births or deaths
    int firstOutbreak = 4000;      // tick of the first outbreak
    float outbreakChance = 1.f / 15000.f;  // per tick, while no one is sick
    float bigMutationChance = 0.01f;       // per birth
    bool climate = true;   // temperature matters (off: everyone is always comfortable)
    bool water = true;     // lakes and seas (off: all land)
    float warming = 0.f;   // degrees C per 10,000 ticks; negative cools
    ClimateSettings climateSettings;
};

// One row of the history charts.
struct Sample {
    int tick = 0;
    int population = 0;
    int food = 0;
    int infected = 0;
    int recovered = 0;
    float avgSize = 0.f;
    float avgVision = 0.f;
    float avgResistance = 0.f;
    float carnivoreShare = 0.f;
    float foraging = 0.f;  // plant food eaten per creature per 1000 ticks
    float avgVirulence = 0.f;       // of the strains in circulation (0 when none)
    float avgInfectiousness = 0.f;
    float avgGeneration = 0.f;
    int maxGeneration = 0;
    float avgMutation = 0.f;
    // climate adaptation
    float avgPreferred = 0.f, sdPreferred = 0.f, avgTolerance = 0.f, avgSwimming = 0.f;
    float inWater = 0.f;          // share of creatures in water
    float swimInWater = 0.f, swimOnLand = 0.f;  // average swimming gene of those in water / on land
    float avgMismatch = 0.f;      // average degrees outside the comfort zone
    float localFit = 0.f;         // correlation of preferred temperature with the local yearly mean
    float sizeVsTemp = 0.f;       // correlation of body size with the local yearly mean (Bergmann: < 0)
    float toleranceVsSwing = 0.f; // correlation of tolerance with the local seasonal swing
    float climateShift = 0.f, season = 0.f;
    float livesAt = 0.f;  // yearly mean temperature where creatures live, before any warming: falls if they move poleward
};

struct Event {
    int tick = 0;
    std::string text;
    int kind = 0;  // 0 neutral, 1 good news for the creature, -1 bad, 2 disease
};

class World {
public:
    World(const Settings& settings, uint32_t seed);

    void step();

    const Settings& settings() const { return settings_; }
    Settings& settings() { return settings_; }
    const std::vector<Creature>& creatures() const { return creatures_; }
    const std::vector<Food>& food() const { return food_; }
    const std::vector<Patch>& patches() const { return patches_; }
    const std::vector<Sample>& history() const { return history_; }
    const std::deque<Event>& events() const { return events_; }
    int tick() const { return tick_; }
    int births() const { return births_; }
    int newcomers() const { return newcomers_; }
    int outbreaks() const { return outbreaks_; }
    int diseaseDeaths() const { return diseaseDeaths_; }
    // how creatures born with a big mutation did, judged when they died
    int bigHelped() const { return bigHelped_; }
    int bigHurt() const { return bigHurt_; }
    int bigNeutral() const { return bigNeutral_; }
    struct Outcomes {
        int helped = 0, hurt = 0, unclear = 0;
    };
    const Outcomes& bigOutcomes(BigMutation kind) const { return bigByKind_[static_cast<int>(kind)]; }
    const Climate& climate() const { return climate_; }
    float climateShift() const { return climateShift_; }
    const Creature* find(uint32_t id) const;
    void addCreature(const Genome& genome);
    long plantsEaten() const { return plantsEatenTotal_; }
    void startOutbreak(uint32_t patientZero = 0);  // 0: a random creature

    Vec2 delta(Vec2 from, Vec2 to) const;  // shortest offset on the wrap-around world

private:
    void spawnFood();
    void movePatches();
    void buildFoodGrid();
    void buildCreatureGrid();
    void sense(int index);
    void live(Creature& c);                              // thinking, moving, living costs: touches only c
    void findFood(int index);  // what is within reach: run in parallel, read only
    void findPrey(int index);
    void feed(int index, std::vector<Creature>& born);  // eating, hunting, giving birth, in creature order
    void spreadDisease();
    void progressDisease(Creature& c);
    Creature makeChild(Creature& parent);
    void reportDeath(const Creature& c, const char* cause);
    void addRandomCreature();
    void sample();
    void log(int kind, std::string text);
    Vec2 wrap(Vec2 p) const;
    Vec2 patchVelocity();

    Settings settings_;
    Rng rng_;
    std::vector<Creature> creatures_;
    std::vector<Food> food_;
    std::vector<Patch> patches_;
    std::vector<Sample> history_;
    std::deque<Event> events_;
    SpatialGrid foodGrid_, creatureGrid_;
    struct Glimpse {  // what others can tell about a creature at a glance, packed for the eye loop
        float size;
        bool sick;
    };
    std::vector<Glimpse> glimpses_;
    std::vector<std::vector<int>> bites_, prey_;  // per creature, this tick: food and prey within reach
    uint32_t nextId_ = 1;
    int tick_ = 0;
    int births_ = 0;
    int newcomers_ = 0;
    int outbreaks_ = 0;
    int diseaseDeaths_ = 0;
    int epidemicStart_ = -1, epidemicPeak_ = 0, epidemicDeathsAtStart_ = 0;
    int bigHelped_ = 0, bigHurt_ = 0, bigNeutral_ = 0;
    std::array<Outcomes, kBigMutationKinds + 1> bigByKind_{};
    Climate climate_;
    float climateShift_ = 0.f;
    long deaths_ = 0, childrenOfDead_ = 0;  // for the typical number of children per life
    float foodDebt_ = 0.f;
    long plantEatenWindow_ = 0;
    long creatureTicksWindow_ = 0;
    long plantsEatenTotal_ = 0;
};

}  // namespace evo
