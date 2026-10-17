# Evolution Sandbox

Creatures with small neural-network brains roam a wrap-around world of land, water and
climate zones; they eat, hunt, catch diseases, reproduce and mutate. Nothing about finding
food or coping with heat and cold is programmed in: it evolves.

```bash
./build.sh                                   # first build fetches raylib 5.5 (about a minute)
./build/evolution_sandbox                    # open the window
./build/evolution_sandbox --seed 21          # a reproducible world
./build/evolution_sandbox --headless --ticks 150000 --seed 7   # no window: progress table and report
./build/evolution_sandbox --ticks 60000 --snap shot.png        # run, then save one frame
```

More options: `--scale 4` (4x the area, food, fertile patches and population), `--threads N`
(default: all cores), `--warming 2` (degrees C per 10,000 ticks), `--warm-after 100000`
(headless: steady climate until then), `--no-climate`, `--no-water`. For snapshots:
`--map biomes|temperature|plain`, `--colours lineage|temp|swim|comfort`, `--climate-page`.

`build.sh` uses Apple's clang and git explicitly: on this Mac the shell sets `CC=gcc-11`,
and `/usr/local/bin/git` is an old Intel-only binary that no longer runs.

## How it works

- **Body.** Size is a gene: bigger creatures are slower and burn more energy, but can eat
  creatures under 1/1.3 of their size. Colour is a gene too, drifting a little each
  generation, so families show up as colour families.
- **Senses.** Three eyes (left, centre, right) report how close the nearest food and the
  nearest other creature are; plus how big the nearest creature is relative to itself, its
  energy, a clock, one memory value, its own speed, how close the nearest sick creature is,
  whether it is sick itself, how hot or cold it feels, whether it is warmer a little way
  ahead, and whether there is water here and ahead. 17 inputs. How far the eyes reach is a
  gene (vision); seeing farther costs energy.
- **Brain.** 17 inputs → 8 hidden neurons → 4 outputs (turn, thrust, attack, memory), all
  tanh. Memory is fed back next tick, so a brain can hold state.
- **Life.** Eating plants or carcasses gives energy; everything costs energy. Above 110
  energy a creature splits: the child gets a copy of the genome with mutations. Death by
  starvation, old age or being eaten.
- **World.** Plants grow mostly in fertile patches that drift east or west (like weather),
  spread over all latitudes; seedlings do better where it is warm.

## Climate, land and water

The world has a climate map (`src/climate.*`). Temperature, in degrees C, falls from the
**equator** (the middle row, about 32 C) toward the **poles** (the top and bottom edges,
which wrap into each other, about -12 C). Mountains are colder, water is milder, and there
are **seasons**: the swing is ±2 C at the equator and ±12 C at the poles, as on Earth. A year
is 6000 ticks, about one lifetime. About 30% of the world is lakes and seas.

Three genes deal with it:

- **Preferred temperature** and **tolerance**: a creature is comfortable within
  preferred ± tolerance. Outside that it pays energy and slows down, and more steeply on
  the hot side, because real thermal performance curves fall off sharply above the optimum
  (Huey & Kingsolver 1989). A broad tolerance costs upkeep ("a jack of all temperatures is a
  master of none", Huey & Hertz 1984), and so does physiology far from a neutral 15 C (fur,
  cooling).
- **Body heat.** Bigger bodies have less skin for their bulk, so they lose heat more
  slowly and feel 5 C warmer per unit of size. This is the physics behind Bergmann's rule
  (bigger animals in colder places, 1847).
- **Swimming**, from 0 (legs) to 1 (fins): fast in water means slow on land, and the
  other way round.

Brains get four new senses for this (too hot, warmer ahead, in water, water ahead), so
creatures can also *move* to a better climate instead of evolving a new physiology.
Press **W** to start global warming (+2 C per 10,000 ticks), press again to cool, again to
hold.

### What happens (six seeds per experiment, 150,000–200,000 ticks)

- **Local adaptation evolves.** Preferred temperatures spread out to match where their
  carriers live: the correlation of preferred temperature with local temperature averages
  r ≈ 0.55 (0.2–0.8 depending on the seed). Heat lovers fill the equator and cold-adapted
  lineages hold the cooler latitudes. Colour creatures by preferred temperature (K) on the
  temperature map (M) to see it. A smaller mutation step (±0.5 C per generation) left the
  whole population as one generalist within a run. That is gene swamping: migrants from the
  crowded warm centre keep diluting the cold margins (Kirkpatrick & Barton 1997).
- **Bergmann's rule does not emerge.** Body size is, if anything, slightly *larger* in warm
  places (r ≈ +0.15), and the same with body heat switched off. So size here follows food
  and hunting (the warm zones are richer), not heat loss, and cold is met by evolving a
  cold preference instead. That fits the evidence that Bergmann's rule is weak and
  inconsistent in many groups, and that body size often tracks resources (McNab 2010).
- **No climate-variability effect.** Tolerance is not broader where seasons swing more (the
  climate variability hypothesis, Janzen 1967; Stevens 1989): r ≈ 0 on average.
- **Water lineages appear.** In some worlds an aquatic branch evolves, with a swimming gene
  of about 0.6 in water against 0.3 on land (seed 21).
- **Under warming (+10 C over 100,000 ticks)** populations coped in two ways. Preferred
  temperature rose by about 4 C (genetic tracking), and populations shifted to places that
  had been about 4 C cooler (moving poleward and uphill, as species are doing now:
  Parmesan & Yohe 2003). Population size held. The mutation-rate gene ended higher than in
  the steady climate in 5 of 6 seeds, a hint (not significant) of the prediction that
  changing environments favour higher mutation rates.

## Disease (SIRS)

Each creature is **S**usceptible, **I**nfected or **R**ecovered. Close contact with a sick
creature can pass the infection on; it lasts 500 ticks, drains energy and slows the host.
Recovered creatures are immune for 2500 ticks, then susceptible again (the "S" at the end of
SIRS), which gives repeated waves. The first outbreak comes at tick 4000, then at random
while no one is sick; press I to start one.

Both sides evolve. Creatures carry a **resistance** gene (less chance to catch it, but it
costs energy to keep) and can learn to avoid sick neighbours. The **pathogen** mutates a
little at every jump: how infectious it is and how much harm it does. In test runs it tends
to become milder while it circulates, since a strain that kills its host fast stops
spreading, and resistance rises while it is around.

## Mutations

Every child gets small random changes to its brain weights, size, vision, resistance,
preferred temperature (±1.2 C), tolerance, swimming, colour and mutation rate. Most do
little, some help, some hurt, and selection keeps the helpful ones. About 1% of births also
get a **big mutation**: a large jump in one trait (size ×/÷1.45, vision ±60, resistance
±0.35, preferred temperature ±8 C, tolerance ×/÷1.7, swimming ±0.35) or a heavy brain
rewiring. The event feed reports each one, and when that creature dies, whether it helped
(clearly more children than a typical life), hurt or made no clear difference. The headless
report breaks this down by trait. Pooled over 24 runs, about 20% of big jumps in a body or
climate trait helped, but only 8% of brain rewirings did: as in nature, most large
mutations are harmful, and scrambling a working brain is worst of all.

## Does it really evolve?

The headless run includes a **skill test**: 40 brains from the population are copied into
a standard arena (same size, all land, no climate, never hungry, no hunting, plenty of food)
for 2000 ticks, and the plants they find are counted. Random brains score about 2–5 plants
per 1000 ticks; evolved brains reach 20–35 within 150,000 ticks.

Different seeds evolve differently: in some worlds hunting dies out, in others most
creatures hunt; some grow a water branch and some do not.

## Performance

The work per tick is mostly *neighbour search* (who sees what), not the brains: a brain is
a 17×8 and an 8×4 dense matrix, about 170 multiply-adds, around 2% of the time in a
profile. A BLAS library (Accelerate, OpenBLAS) would cost more in call overhead than that
arithmetic. Sparse storage would not save anything either, since evolution mutates every
weight, so none are zero. What did help, measured on seed 21:

- A flat spatial grid rebuilt each tick by counting sort (`src/grid.hpp`), with cells half
  the longest eyesight and no divisions in the cell walk.
- Eye sectors from a dot and a cross product instead of `atan2`, and a rational `tanh`
  accurate to 3e-7.
- A tick split into a parallel phase (sense, think, move, living costs, finding food and
  prey within reach) and a serial phase that settles who gets a contested bite or is eaten.
  It runs on a small thread pool (`src/parallel.*`). Results are bit-identical for any
  number of threads, and do not depend on the grid layout.

Result: a 50,000-tick headless run went from 13.9 s to 4.9 s while also simulating the
climate. About 0.2 µs per creature per tick at `--scale 4` and `--scale 16` (1000 and 3200
creatures; about 1000 ticks/s at scale 16) on an 8-core M1. The cost per creature stays
flat as the world grows.

Compiler flags (in `CMakeLists.txt`): `-O3`, `-mcpu=native`, link-time optimisation and
`-fno-math-errno`. Measured against plain `-O2`, none of them, nor `-ffast-math` or
profile-guided optimisation, changed speed by more than 2%. The remaining time is spent
waiting on memory and branches, which flags cannot fix. `-ffast-math` is left out because it
assumes there are no NaNs and reorders sums. Turn off `EVO_NATIVE` to build a binary for
other machines.

## Controls

| Key | Action |
|---|---|
| Space | pause |
| 1–6 | speed: 1, 2, 4, 8, 16, 64 ticks per frame |
| Click | select a creature (its brain appears in the panel) |
| F | follow the selected creature |
| O / G | select the oldest / the highest generation |
| V | show or hide the selected creature's senses |
| M | map: biomes, temperature, plain |
| K | creature colours: lineage, preferred temperature, swimming, comfort right now |
| Tab | side panel: life charts or climate charts |
| W | climate: steady → warming → cooling → steady |
| P | hunting on or off |
| I | start an outbreak (in the selected creature, or a random one) |
| D | disease on or off |
| + / − | more or less food |
| Drag, scroll | pan, zoom |
| C | fit the whole world |
| R | new random world |

## Code

| File | What |
|---|---|
| `src/world.*` | the simulation: senses, movement, eating, hunting, births, disease, history |
| `src/climate.*` | terrain, water, temperature, seasons |
| `src/grid.hpp` | the spatial grid for neighbour lookups |
| `src/parallel.*` | the thread pool |
| `src/brain.*` | the neural network and mutation |
| `src/render.*` | drawing with raylib: world, maps, charts, brain view |
| `src/main.cpp` | options, input, the skill test, the headless report |
