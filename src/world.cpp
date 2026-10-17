#include "world.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "parallel.hpp"

namespace evo {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kPlantEnergy = 22.f;
constexpr float kStartEnergy = 60.f;
constexpr float kMaxEnergy = 200.f;
constexpr float kBirthThreshold = 110.f;
constexpr float kChildEnergy = 45.f;
constexpr float kBirthCost = 10.f;
constexpr int kMaxAge = 6000;
constexpr int kSickTicks = 500;     // how long an infection lasts
constexpr int kImmuneTicks = 2500;  // how long immunity lasts after recovering (then susceptible again)
constexpr int kSampleEvery = 100;
constexpr size_t kMaxSamples = 1500;
constexpr size_t kMaxEvents = 8;
constexpr float kGridCell = kMaxVision / 2;  // neighbour-lookup cells; a lookup visits the cells its radius overlaps
constexpr float kMaxRadius = 4.f + 4.f * 2.5f;  // radius of the biggest possible body
// eye sectors as slopes, so a sight line is sorted by a dot and a cross product, not atan2
const float kTanFieldOfView = std::tan(kFieldOfView);
const float kTanCentre = std::tan(kCentreHalfAngle);

// Climate physiology. A creature is comfortable within preferredTemp +- tolerance. Outside that
// it pays energy and slows down, more steeply on the hot side: real thermal performance curves
// fall off sharply above the optimum (Huey & Kingsolver 1989). Heat escapes through the skin,
// and a big body has less skin for its bulk, so big bodies run warmer (the reasoning behind
// Bergmann's rule); the costs scale with skin, i.e. with size in this 2-D world.
constexpr float kBodyHeat = 5.f;          // degrees warmer felt per unit of size above 1
constexpr float kColdCost = 0.0012f;      // energy per tick per degree too cold, per unit of size
constexpr float kHeatCost = 0.0025f;      // ... too hot
constexpr float kColdSlow = 0.025f;       // speed lost per degree too cold
constexpr float kHeatSlow = 0.05f;        // ... too hot
constexpr float kToleranceCost = 0.0006f; // per tick per degree of tolerance: jack of all temperatures, master of none
constexpr float kNeutralTemp = 15.f;      // living far from this needs costly physiology (fur, cooling) ...
constexpr float kPhysiologyCost = 0.0002f;  // ... per tick per degree of preferred temperature away from it
constexpr float kProbe = 40.f;            // how far ahead creatures feel for warmth and water

float wrapAngle(float a) {
    while (a > kPi) a -= 2 * kPi;
    while (a < -kPi) a += 2 * kPi;
    return a;
}

float correlation(const std::vector<float>& a, const std::vector<float>& b) {
    const size_t n = a.size();
    if (n < 3) return 0.f;
    double ma = 0, mb = 0;
    for (size_t i = 0; i < n; ++i) ma += a[i], mb += b[i];
    ma /= n, mb /= n;
    double sab = 0, saa = 0, sbb = 0;
    for (size_t i = 0; i < n; ++i) {
        sab += (a[i] - ma) * (b[i] - mb);
        saa += (a[i] - ma) * (a[i] - ma);
        sbb += (b[i] - mb) * (b[i] - mb);
    }
    return saa > 0 && sbb > 0 ? static_cast<float>(sab / std::sqrt(saa * sbb)) : 0.f;
}

}  // namespace

const char* traitName(BigMutation m) {
    switch (m) {
        case BigMutation::Size: return "size";
        case BigMutation::Vision: return "vision";
        case BigMutation::Resistance: return "resistance";
        case BigMutation::Brain: return "brain rewiring";
        case BigMutation::PreferredTemp: return "preferred temp";
        case BigMutation::Tolerance: return "tolerance";
        case BigMutation::Swimming: return "swimming";
        default: return "";
    }
}

float Creature::maxSpeed() const { return 2.6f / std::pow(genome.size, 0.9f); }  // big bodies are slow

World::World(const Settings& settings, uint32_t seed) : settings_(settings), rng_(seed) {
    foodGrid_.init(settings_.width, settings_.height, kGridCell);
    creatureGrid_.init(settings_.width, settings_.height, kGridCell);
    ClimateSettings climate = settings_.climateSettings;
    if (!settings_.water) climate.waterShare = 0.f;
    climate_.generate(settings_.width, settings_.height, climate, rng_);

    for (int i = 0; i < settings_.patches; ++i) {
        // spread over all latitudes, so every climate has some fertile ground
        float y = (static_cast<float>(i) + rng_.uniform(0.f, 1.f)) * settings_.height / static_cast<float>(settings_.patches);
        patches_.push_back({{rng_.uniform(0.f, settings_.width), y}, patchVelocity(),
                            rng_.uniform(120.f, 220.f)});
    }
    for (int i = 0; i < settings_.maxFood * 2 / 3; ++i) {
        foodDebt_ += 1.f;
        spawnFood();
    }
    for (int i = 0; i < settings_.startCreatures; ++i) addRandomCreature();
    newcomers_ = 0;
}

// Fertile patches drift mostly east or west, like weather along the prevailing winds, so a
// patch tends to stay in its climate band instead of carrying its creatures across all of them.
Vec2 World::patchVelocity() {
    float angle = (rng_.chance(0.5f) ? 0.f : kPi) + rng_.normal(0.f, 0.25f);
    return {std::cos(angle) * 0.12f, std::sin(angle) * 0.12f};
}

Vec2 World::wrap(Vec2 p) const {
    p.x = std::fmod(p.x, settings_.width);
    p.y = std::fmod(p.y, settings_.height);
    if (p.x < 0) p.x += settings_.width;  // -1e-8 + width rounds to width itself, hence the checks below
    if (p.y < 0) p.y += settings_.height;
    if (p.x >= settings_.width) p.x = 0.f;
    if (p.y >= settings_.height) p.y = 0.f;
    return p;
}

Vec2 World::delta(Vec2 from, Vec2 to) const {
    float dx = to.x - from.x, dy = to.y - from.y;
    if (dx > settings_.width / 2) dx -= settings_.width;
    if (dx < -settings_.width / 2) dx += settings_.width;
    if (dy > settings_.height / 2) dy -= settings_.height;
    if (dy < -settings_.height / 2) dy += settings_.height;
    return {dx, dy};
}

const Creature* World::find(uint32_t id) const {
    for (const auto& c : creatures_)
        if (c.id == id && c.alive) return &c;
    return nullptr;
}

void World::log(int kind, std::string text) {
    events_.push_front({tick_, std::move(text), kind});
    if (events_.size() > kMaxEvents) events_.pop_back();
}

void World::addRandomCreature() {
    Creature c;
    c.id = nextId_++;
    c.genome.brain = Brain::random(rng_);
    c.genome.size = rng_.uniform(0.8f, 1.2f);
    c.genome.vision = rng_.uniform(110.f, 150.f);
    c.genome.hue = rng_.uniform(0.f, 360.f);
    c.genome.preferredTemp = rng_.uniform(12.f, 20.f);  // newcomers come from a mild climate ...
    c.genome.tolerance = rng_.uniform(4.f, 7.f);
    c.genome.swimming = rng_.uniform(0.05f, 0.25f);     // ... and live on land
    c.pos = {rng_.uniform(0.f, settings_.width), rng_.uniform(0.f, settings_.height)};
    c.heading = rng_.uniform(-kPi, kPi);
    c.energy = kStartEnergy;
    creatures_.push_back(c);
    ++newcomers_;
}

void World::addCreature(const Genome& genome) {
    Creature c;
    c.id = nextId_++;
    c.genome = genome;
    c.pos = {rng_.uniform(0.f, settings_.width), rng_.uniform(0.f, settings_.height)};
    c.heading = rng_.uniform(-kPi, kPi);
    c.energy = kStartEnergy;
    creatures_.push_back(c);
}

void World::spawnFood() {
    while (foodDebt_ >= 1.f) {
        foodDebt_ -= 1.f;
        if (static_cast<int>(food_.size()) >= settings_.maxFood) continue;
        Vec2 p;
        // plants grow best where it is warm: a seedling in the cold usually fails, and the
        // seed is tried somewhere else (so the total stays the same, but moves toward warmth)
        for (int attempt = 0; attempt < 4; ++attempt) {
            if (rng_.chance(0.8f)) {  // mostly in the fertile patches
                const Patch& patch = patches_[rng_.index(static_cast<int>(patches_.size()))];
                p = wrap({patch.pos.x + rng_.normal(0.f, patch.radius * 0.6f), patch.pos.y + rng_.normal(0.f, patch.radius * 0.6f)});
            } else {
                p = {rng_.uniform(0.f, settings_.width), rng_.uniform(0.f, settings_.height)};
            }
            if (!settings_.climate) break;
            float growth = 0.3f + 0.7f * std::clamp((climate_.temperature(p) + 8.f) / 28.f, 0.f, 1.f);
            if (rng_.chance(growth)) break;
        }
        food_.push_back({wrap(p), kPlantEnergy, false, true});
    }
}

void World::movePatches() {
    for (auto& patch : patches_) {
        if (rng_.chance(0.002f)) {  // now and then a patch changes course
            patch.vel = patchVelocity();
        }
        patch.pos = wrap({patch.pos.x + patch.vel.x, patch.pos.y + patch.vel.y});
    }
}

void World::buildFoodGrid() {
    foodGrid_.build(static_cast<int>(food_.size()), [&](int i) { return food_[i].pos; });
}

void World::buildCreatureGrid() {
    creatureGrid_.build(static_cast<int>(creatures_.size()), [&](int i) { return creatures_[i].pos; });
    glimpses_.resize(creatures_.size());
    for (size_t i = 0; i < creatures_.size(); ++i) glimpses_[i] = {creatures_[i].genome.size, creatures_[i].sick()};
}

void World::sense(int index) {
    // Runs for every creature at once on several threads, before anything moves or is eaten:
    // so everything in the grids is alive, and the only thing written is this creature's inputs.
    Creature& c = creatures_[index];
    const float vision = c.genome.vision, vision2 = vision * vision;
    const float hx = std::cos(c.heading), hy = std::sin(c.heading);
    // Each eye keeps the squared distance of the nearest thing it sees; closeness = 1 - dist/vision.
    // In heading coordinates, ahead = d.h and side = h x d (positive: to the right on screen).
    auto sector = [&](float dx, float dy, float d2) -> int {
        if (d2 > vision2 || d2 < 1e-6f) return -1;
        float ahead = dx * hx + dy * hy, side = hx * dy - hy * dx;
        if (ahead <= 0.f || std::fabs(side) > kTanFieldOfView * ahead) return -1;
        return side < -kTanCentre * ahead ? 0 : side > kTanCentre * ahead ? 2 : 1;
    };
    auto closeness = [&](float d2) { return d2 < vision2 ? 1.f - std::sqrt(d2) / vision : 0.f; };

    std::array<float, 3> foodD2{vision2, vision2, vision2}, otherD2{vision2, vision2, vision2};
    foodGrid_.query(c.pos, vision, [&](int, float dx, float dy) {
        float d2 = dx * dx + dy * dy;
        int s = sector(dx, dy, d2);
        if (s >= 0) foodD2[s] = std::min(foodD2[s], d2);
    });
    float nearestD2 = vision2, sizeVs = 0.f, sickD2 = vision2;
    creatureGrid_.query(c.pos, vision, [&](int i, float dx, float dy) {
        float d2 = dx * dx + dy * dy;
        int s = sector(dx, dy, d2);
        if (s < 0 || i == index) return;
        const Glimpse& o = glimpses_[i];
        otherD2[s] = std::min(otherD2[s], d2);
        if (d2 < nearestD2) {
            nearestD2 = d2;
            sizeVs = std::clamp(o.size / c.genome.size - 1.f, -1.f, 1.f);
        }
        if (o.sick) sickD2 = std::min(sickD2, d2);
    });
    std::array<float, 3> food{}, others{};
    for (int s = 0; s < 3; ++s) {
        food[s] = closeness(foodD2[s]);
        others[s] = closeness(otherD2[s]);
    }
    const float sickNear = closeness(sickD2);
    // climate: how warm it feels here and a little way ahead, and where the water is
    Vec2 ahead = wrap({c.pos.x + hx * kProbe, c.pos.y + hy * kProbe});
    c.inWater = settings_.water && climate_.isWater(c.pos);
    const bool waterAhead = settings_.water && climate_.isWater(ahead);
    float tooHot = 0.f, warmerAhead = 0.f;
    if (settings_.climate) {
        const Genome& g = c.genome;
        c.ambient = climate_.temperature(c.pos);
        c.felt = c.ambient + kBodyHeat * (g.size - 1.f);
        c.coldStress = std::max(0.f, g.preferredTemp - g.tolerance - c.felt);
        c.heatStress = std::max(0.f, c.felt - g.preferredTemp - g.tolerance);
        tooHot = std::clamp((c.felt - g.preferredTemp) / 15.f, -1.f, 1.f);
        warmerAhead = std::clamp((climate_.temperature(ahead) - c.ambient) / 2.f, -1.f, 1.f);
    } else {
        c.ambient = c.felt = c.genome.preferredTemp;
        c.coldStress = c.heatStress = 0.f;
    }

    c.in = {food[0], food[1], food[2], others[0], others[1], others[2], sizeVs,
            c.energy / kBirthThreshold, std::sin(c.age * 0.06f), c.memory, c.speed / c.maxSpeed(),
            sickNear, c.sick() ? 1.f : 0.f,
            tooHot, warmerAhead, c.inWater ? 1.f : 0.f, waterAhead ? 1.f : 0.f};
}

void World::progressDisease(Creature& c) {
    if (c.health == Health::Infected) {
        c.energy -= c.strain.virulence;
        if (--c.healthTimer <= 0) {
            c.health = Health::Recovered;
            c.healthTimer = kImmuneTicks;
        }
    } else if (c.health == Health::Recovered && --c.healthTimer <= 0) {
        c.health = Health::Susceptible;  // immunity wanes: SIRS
    }
}

Creature World::makeChild(Creature& parent) {
    Creature child;
    child.id = nextId_++;
    child.genome = parent.genome;
    Genome& g = child.genome;
    // small random mutations in every child: most do little, some help, some hurt
    g.brain.mutate(rng_, parent.genome.mutation, 0.35f);
    g.size = std::clamp(g.size * std::exp(rng_.normal(0.f, 0.05f)), 0.5f, 2.5f);
    g.vision = std::clamp(g.vision * std::exp(rng_.normal(0.f, 0.04f)), kMinVision, kMaxVision);
    g.resistance = std::clamp(g.resistance + rng_.normal(0.f, 0.02f), 0.f, 0.9f);
    g.hue = std::fmod(g.hue + rng_.normal(0.f, 6.f) + 360.f, 360.f);
    g.mutation = std::clamp(g.mutation * std::exp(rng_.normal(0.f, 0.1f)), 0.01f, 0.3f);
    // +-1.2 C per generation: at +-0.5 the population stayed one generalist within a 150k-tick run
    g.preferredTemp = std::clamp(g.preferredTemp + rng_.normal(0.f, 1.2f), -30.f, 50.f);
    g.tolerance = std::clamp(g.tolerance * std::exp(rng_.normal(0.f, 0.05f)), 0.5f, 20.f);
    g.swimming = std::clamp(g.swimming + rng_.normal(0.f, 0.025f), 0.f, 1.f);

    // and now and then a big jump in one trait
    if (rng_.chance(settings_.bigMutationChance)) {
        auto jump = [&](float& value, float step, float lo, float hi, bool multiply) {
            float before = value;
            bool up = rng_.chance(0.5f);
            auto apply = [&](bool dirUp) {
                float v = multiply ? (dirUp ? before * step : before / step) : (dirUp ? before + step : before - step);
                return std::clamp(v, lo, hi);
            };
            value = apply(up);
            if (std::fabs(value - before) < 1e-4f) value = apply(!up);  // at a limit: go the other way
            child.bigBefore = before;
            child.bigAfter = value;
        };
        child.bigMutation = static_cast<BigMutation>(1 + rng_.index(kBigMutationKinds));
        switch (child.bigMutation) {
            case BigMutation::Size: jump(g.size, 1.45f, 0.5f, 2.5f, true); break;
            case BigMutation::Vision: jump(g.vision, 60.f, kMinVision, kMaxVision, false); break;
            case BigMutation::Resistance: jump(g.resistance, 0.35f, 0.f, 0.9f, false); break;
            case BigMutation::PreferredTemp: jump(g.preferredTemp, 8.f, -30.f, 50.f, false); break;
            case BigMutation::Tolerance: jump(g.tolerance, 1.7f, 0.5f, 20.f, true); break;
            case BigMutation::Swimming: jump(g.swimming, 0.35f, 0.f, 1.f, false); break;
            default: g.brain.mutate(rng_, 0.5f, 0.8f); break;
        }
        char text[160];
        if (child.bigMutation == BigMutation::Brain)
            std::snprintf(text, sizeof text, "#%u big mutation: brain rewired", child.id);
        else if (child.bigMutation == BigMutation::Vision)
            std::snprintf(text, sizeof text, "#%u big mutation: vision %.0f -> %.0f", child.id, child.bigBefore, child.bigAfter);
        else if (child.bigMutation == BigMutation::PreferredTemp || child.bigMutation == BigMutation::Tolerance)
            std::snprintf(text, sizeof text, "#%u big mutation: %s %.1f -> %.1f C", child.id, traitName(child.bigMutation),
                          child.bigBefore, child.bigAfter);
        else
            std::snprintf(text, sizeof text, "#%u big mutation: %s %.2f -> %.2f", child.id, traitName(child.bigMutation),
                          child.bigBefore, child.bigAfter);
        log(0, text);
    }

    float r = parent.radius();
    child.pos = wrap({parent.pos.x - std::cos(parent.heading) * r * 2, parent.pos.y - std::sin(parent.heading) * r * 2});
    child.heading = rng_.uniform(-kPi, kPi);
    child.energy = kChildEnergy;
    child.generation = parent.generation + 1;
    return child;
}

void World::live(Creature& c) {
    c.genome.brain.think(c.in, c.hidden, c.out);
    float turn = c.out[0], thrust = (c.out[1] + 1.f) * 0.5f;
    c.attacking = settings_.predation && c.out[2] > 0.f;
    c.memory = c.out[3];

    c.heading = wrapAngle(c.heading + turn * 0.14f);
    const Genome& g = c.genome;
    const float habitat = c.inWater ? 0.3f + 0.7f * g.swimming : 1.f - 0.65f * g.swimming;  // fins or legs
    const float thermal = 1.f / (1.f + kColdSlow * c.coldStress + kHeatSlow * c.heatStress);
    c.speed = thrust * c.maxSpeed() * habitat * thermal * (c.sick() ? 0.75f : 1.f);
    c.pos = wrap({c.pos.x + std::cos(c.heading) * c.speed, c.pos.y + std::sin(c.heading) * c.speed});

    // living costs: eyes, a body that grows with size squared, moving fast, an immune system,
    // and coping with the climate
    c.energy -= 0.006f + 0.00005f * g.vision + 0.01f * g.size * g.size + 0.025f * thrust * thrust * g.size +
                0.012f * g.resistance;
    if (settings_.climate)
        c.energy -= (kColdCost * c.coldStress + kHeatCost * c.heatStress) * g.size + kToleranceCost * g.tolerance +
                    kPhysiologyCost * std::fabs(g.preferredTemp - kNeutralTemp);
    if (c.attacking) {
        c.energy -= 0.015f;
        ++c.attackTicks;
    }
    progressDisease(c);
    ++c.age;
}

void World::findFood(int index) {
    const Creature& c = creatures_[index];
    const float mouth = c.mouth();
    std::vector<int>& bites = bites_[index];
    bites.clear();
    foodGrid_.query(c.pos, mouth, [&](int i, float dx, float dy) {
        if (dx * dx + dy * dy < mouth * mouth) bites.push_back(i);
    });
    std::sort(bites.begin(), bites.end());  // index order, so the energy sum never depends on the grid
}

void World::findPrey(int index) {
    const Creature& c = creatures_[index];
    std::vector<int>& prey = prey_[index];
    prey.clear();
    if (!c.attacking) return;
    const float r = c.radius();
    creatureGrid_.query(c.pos, r + kMaxRadius, [&](int i, float dx, float dy) {
        const Creature& p = creatures_[i];
        float reach = r + p.radius();
        if (i != index && p.genome.size * 1.3f <= c.genome.size && dx * dx + dy * dy < reach * reach) prey.push_back(i);
    });
    std::sort(prey.begin(), prey.end());
}

void World::feed(int index, std::vector<Creature>& born) {
    Creature& c = creatures_[index];
    for (int i : bites_[index]) {
        Food& f = food_[i];
        if (!f.alive) continue;  // someone earlier in the order got it first
        f.alive = false;
        c.energy += f.energy;
        ++c.eaten;
        if (!f.meat) {
            ++plantEatenWindow_;
            ++plantsEatenTotal_;
        }
    }
    for (int i : prey_[index]) {
        Creature& prey = creatures_[i];
        if (!prey.alive) continue;
        prey.alive = false;
        prey.eatenByPredator = true;
        c.energy += 0.6f * std::max(prey.energy, 0.f) + 12.f * prey.genome.size;
        ++c.kills;
        reportDeath(prey, "eaten");
    }
    c.energy = std::min(c.energy, kMaxEnergy);
    if (!settings_.lifeCycle) {
        c.energy = kStartEnergy;
        return;
    }

    if (c.energy > kBirthThreshold && c.age > 60) {
        born.push_back(makeChild(c));
        c.energy -= kChildEnergy + kBirthCost;
        ++c.children;
        ++births_;
    }
}

void World::spreadDisease() {
    if (!settings_.disease) return;
    // (the creature grid already holds this tick's final positions)
    // find the contacts first, then roll the dice in a fixed order (sick creature, then contact
    // index), so the random sequence does not depend on how the grid is laid out
    std::vector<std::pair<int, int>> contacts;
    for (int i = 0; i < static_cast<int>(creatures_.size()); ++i) {
        const Creature& c = creatures_[i];
        if (!c.alive || !c.sick()) continue;
        const size_t first = contacts.size();
        creatureGrid_.query(c.pos, c.radius() + kMaxRadius + 8.f, [&](int j, float dx, float dy) {
            const Creature& o = creatures_[j];
            if (!o.alive || o.health != Health::Susceptible) return;
            float contact = c.radius() + o.radius() + 8.f;
            if (dx * dx + dy * dy <= contact * contact) contacts.push_back({i, j});
        });
        std::sort(contacts.begin() + static_cast<long>(first), contacts.end());
    }
    std::vector<std::pair<int, Strain>> caught;
    for (auto [i, j] : contacts) {
        const Creature& c = creatures_[i];
        if (!rng_.chance(c.strain.infectiousness * (1.f - creatures_[j].genome.resistance))) continue;
        Strain s = c.strain;  // the strain mutates a little with every jump
        s.infectiousness = std::clamp(s.infectiousness * std::exp(rng_.normal(0.f, 0.08f)), 0.003f, 0.2f);
        s.virulence = std::clamp(s.virulence * std::exp(rng_.normal(0.f, 0.08f)), 0.003f, 0.15f);
        caught.push_back({j, s});
    }
    for (auto& [j, s] : caught) {
        Creature& o = creatures_[j];
        if (o.health != Health::Susceptible) continue;
        o.health = Health::Infected;
        o.healthTimer = kSickTicks;
        o.strain = s;
    }
}

void World::startOutbreak(uint32_t patientZero) {
    if (creatures_.empty()) return;
    Creature* target = nullptr;
    for (auto& c : creatures_)
        if (c.id == patientZero && c.alive) target = &c;
    if (!target) target = &creatures_[rng_.index(static_cast<int>(creatures_.size()))];
    target->health = Health::Infected;
    target->healthTimer = kSickTicks;
    target->strain = Strain{};
    ++outbreaks_;
    if (epidemicStart_ < 0) {
        epidemicStart_ = tick_;
        epidemicPeak_ = 1;
        epidemicDeathsAtStart_ = diseaseDeaths_;
    }
    char text[120];
    std::snprintf(text, sizeof text, "Outbreak %d: #%u falls sick", outbreaks_, target->id);
    log(2, text);
}

void World::reportDeath(const Creature& c, const char* cause) {
    ++deaths_;
    childrenOfDead_ += c.children;
    if (c.bigMutation == BigMutation::None) return;
    // judge the big mutation against a typical life: helped if it led to clearly more children
    float typical = deaths_ > 0 ? static_cast<float>(childrenOfDead_) / deaths_ : 1.f;
    int kind = c.children > typical * 1.5f + 0.5f ? 1 : c.children < typical * 0.5f ? -1 : 0;
    (kind > 0 ? bigHelped_ : kind < 0 ? bigHurt_ : bigNeutral_)++;
    Outcomes& byKind = bigByKind_[static_cast<int>(c.bigMutation)];
    (kind > 0 ? byKind.helped : kind < 0 ? byKind.hurt : byKind.unclear)++;
    char text[200];
    std::snprintf(text, sizeof text, "#%u (%s) %s, %d kids: %s", c.id, traitName(c.bigMutation), cause, c.children,
                  kind > 0 ? "helped" : kind < 0 ? "hurt" : "unclear");
    log(kind, text);
}

void World::step() {
    climateShift_ += settings_.warming / 10000.f;
    climate_.update(tick_, climateShift_);
    foodDebt_ += settings_.foodPerTick;
    spawnFood();
    movePatches();
    buildFoodGrid();
    buildCreatureGrid();

    // Everything a creature does alone runs in parallel: it reads the world and writes only to
    // itself, so the result is the same on any number of threads. First sensing, thinking and
    // moving (from the world as it was at the start of the tick), and finding the food within
    // reach; then, once everyone has moved, finding prey within reach. What creatures do to
    // each other (who gets a contested bite, who gets eaten, births, infection) is then decided
    // on one thread, in a fixed order.
    const int n = static_cast<int>(creatures_.size());
    bites_.resize(n);
    prey_.resize(n);
    threadPool().parallelFor(n, 32, [&](int begin, int end) {
        for (int i = begin; i < end; ++i) {
            sense(i);
            live(creatures_[i]);
            findFood(i);
        }
    });
    buildCreatureGrid();
    threadPool().parallelFor(n, 64, [&](int begin, int end) {
        for (int i = begin; i < end; ++i) findPrey(i);
    });
    std::vector<Creature> born;
    for (int i = 0; i < n; ++i)
        if (creatures_[i].alive) feed(i, born);
    spreadDisease();

    // deaths: hunger, disease or old age leave a carcass that others can eat
    for (auto& c : creatures_) {
        if (settings_.lifeCycle && c.alive && (c.energy <= 0.f || c.age > kMaxAge)) {
            c.alive = false;
            const char* cause = c.age > kMaxAge ? "old age" : c.sick() ? "disease" : "starved";
            if (c.sick() && c.energy <= 0.f) ++diseaseDeaths_;
            reportDeath(c, cause);
        }
        if (!c.alive && !c.eatenByPredator) food_.push_back({c.pos, 15.f + 6.f * c.genome.size, true, true});
    }
    creatures_.erase(std::remove_if(creatures_.begin(), creatures_.end(), [](const Creature& c) { return !c.alive; }),
                     creatures_.end());
    food_.erase(std::remove_if(food_.begin(), food_.end(), [](const Food& f) { return !f.alive; }), food_.end());
    creatures_.insert(creatures_.end(), born.begin(), born.end());
    while (settings_.lifeCycle && static_cast<int>(creatures_.size()) < settings_.minPopulation) addRandomCreature();

    // epidemics: start one now and then while no one is sick, and report each one when it ends
    int sick = static_cast<int>(std::count_if(creatures_.begin(), creatures_.end(), [](const Creature& c) { return c.sick(); }));
    if (sick > 0) {
        epidemicPeak_ = std::max(epidemicPeak_, sick);
    } else if (epidemicStart_ >= 0) {
        char text[160];
        std::snprintf(text, sizeof text, "Epidemic over: %d ticks, peak %d sick, %d died",
                      tick_ - epidemicStart_, epidemicPeak_, diseaseDeaths_ - epidemicDeathsAtStart_);
        log(2, text);
        epidemicStart_ = -1;
    }
    if (settings_.disease && settings_.lifeCycle && sick == 0 &&
        (tick_ == settings_.firstOutbreak || (tick_ > settings_.firstOutbreak && rng_.chance(settings_.outbreakChance))))
        startOutbreak();

    creatureTicksWindow_ += static_cast<long>(creatures_.size());
    ++tick_;
    if (tick_ % kSampleEvery == 0) sample();
}

void World::sample() {
    Sample s;
    s.tick = tick_;
    s.population = static_cast<int>(creatures_.size());
    s.food = static_cast<int>(food_.size());
    int carnivores = 0;
    for (const auto& c : creatures_) {
        s.avgSize += c.genome.size;
        s.avgVision += c.genome.vision;
        s.avgResistance += c.genome.resistance;
        s.avgGeneration += static_cast<float>(c.generation);
        s.maxGeneration = std::max(s.maxGeneration, c.generation);
        carnivores += c.carnivore() ? 1 : 0;
        if (c.sick()) {
            ++s.infected;
            s.avgVirulence += c.strain.virulence;
            s.avgInfectiousness += c.strain.infectiousness;
        }
        if (c.health == Health::Recovered) ++s.recovered;
    }
    // climate adaptation: means, and how well genes match where their carriers live
    std::vector<float> preferred, tolerance, localMean, localSwing, size;
    int inWater = 0;
    double swimWater = 0, swimLand = 0, mismatch = 0;
    for (const auto& c : creatures_) {
        const Genome& g = c.genome;
        s.avgMutation += g.mutation;
        s.livesAt += climate_.meanTemperature(c.pos) - climateShift_;
        preferred.push_back(g.preferredTemp);
        tolerance.push_back(g.tolerance);
        size.push_back(g.size);
        localMean.push_back(climate_.meanTemperature(c.pos));
        localSwing.push_back(climate_.seasonalSwing(c.pos));
        s.avgSwimming += g.swimming;
        mismatch += c.coldStress + c.heatStress;
        if (c.inWater) {
            ++inWater;
            swimWater += g.swimming;
        } else {
            swimLand += g.swimming;
        }
    }
    if (s.population > 0) {
        const int n = s.population;
        s.avgMutation /= n;
        s.livesAt /= n;
        s.avgSwimming /= n;
        for (float t : preferred) s.avgPreferred += t / n;
        for (float t : preferred) s.sdPreferred += (t - s.avgPreferred) * (t - s.avgPreferred) / n;
        s.sdPreferred = std::sqrt(s.sdPreferred);
        for (float t : tolerance) s.avgTolerance += t / n;
        s.inWater = static_cast<float>(inWater) / n;
        s.swimInWater = inWater > 0 ? static_cast<float>(swimWater / inWater) : 0.f;
        s.swimOnLand = n > inWater ? static_cast<float>(swimLand / (n - inWater)) : 0.f;
        s.avgMismatch = static_cast<float>(mismatch / n);
        s.localFit = correlation(preferred, localMean);
        s.sizeVsTemp = correlation(size, localMean);
        s.toleranceVsSwing = correlation(tolerance, localSwing);
    }
    s.climateShift = climateShift_;
    s.season = climate_.season();
    if (s.population > 0) {
        s.avgSize /= s.population;
        s.avgVision /= s.population;
        s.avgResistance /= s.population;
        s.avgGeneration /= s.population;
        s.carnivoreShare = static_cast<float>(carnivores) / s.population;
    }
    if (s.infected > 0) {
        s.avgVirulence /= s.infected;
        s.avgInfectiousness /= s.infected;
    }
    s.foraging = creatureTicksWindow_ > 0 ? 1000.f * plantEatenWindow_ / creatureTicksWindow_ : 0.f;
    plantEatenWindow_ = 0;
    creatureTicksWindow_ = 0;
    history_.push_back(s);
    if (history_.size() > kMaxSamples) {  // keep a long view: thin out the oldest half
        std::vector<Sample> thinned;
        for (size_t i = 0; i < history_.size(); ++i)
            if (i >= history_.size() / 2 || i % 2 == 0) thinned.push_back(history_[i]);
        history_ = std::move(thinned);
    }
}

}  // namespace evo
