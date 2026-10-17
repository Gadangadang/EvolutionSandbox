# Evolution Sandbox: notes for Claude

C++20 artificial-life simulation drawn with raylib 5.5 (fetched by CMake; nothing installed
system-wide). README.md has the full design, controls and results; this file has what a new
session needs to work on it.

## This machine (Apple Silicon Mac)

- Use `/usr/bin/git`. `/usr/local/bin/git` is an old Intel-only binary ("bad CPU type").
- The shell sets `CC=gcc-11`, which breaks CMake: always build with `./build.sh` (it uses
  `/usr/bin/clang` and `/usr/bin/clang++`).
- For profiling use `/usr/bin/sample <pid> <seconds>`: an Anaconda script named `sample`
  shadows it on the PATH.
- The shell is zsh with multios: `cmd 2>&1 >/dev/null` does not hide stdout. Send stderr to a
  file instead.
- Git: committing is fine; the user pushes to GitHub themselves.

## Commands

```bash
./build.sh                                                     # build (first time fetches raylib, ~1 min)
./build/evolution_sandbox --seed 21                            # window
./build/evolution_sandbox --headless --ticks 150000 --seed 21  # progress table + adaptation report + speed
./build/evolution_sandbox --seed 21 --ticks 50000 --snap shot.png --map temperature --colours temp --climate-page
```

Other options: `--scale N`, `--threads N`, `--warming X` (C per 10k ticks), `--warm-after T`,
`--no-climate`, `--no-water`. `--snap` renders one frame offscreen: use it to check visuals,
since the live window cannot be driven from a session. The live keys (M, K, Tab, W and the
older ones) have not been tested interactively yet.

## Code

- `src/world.*`: simulation. `World::step()` runs a parallel phase (`sense`, `live`,
  `findFood`, then `findPrey`), in which each creature writes only to itself, and then a
  serial phase (`feed`, `spreadDisease`, deaths, births) in creature-index order.
- `src/climate.*`: terrain, water, temperature by latitude, altitude, seasons and water.
- `src/grid.hpp`: flat spatial grid (counting sort), `query(p, radius, f(item, dx, dy))`.
- `src/parallel.*`: thread pool, `threadPool().parallelFor(count, chunk, body)`.
- `src/brain.*`: 17 inputs -> 8 tanh hidden -> 4 tanh outputs; `fastTanh`.
- `src/render.*`: world, map texture, legend, charts (life and climate pages), brain view.
- `src/main.cpp`: options, keys, skill test, headless report.

## Rules that keep results trustworthy

- **Determinism.** A run depends only on its seed: never on the thread count or the grid
  layout. Anything that draws random numbers or settles a conflict (contested food, kills,
  infections) must do so on one thread, in a fixed order (sort candidates by index). After
  touching `step()`, check that `--threads 1` and `--threads 8` give identical output
  (`... | grep -v speed | md5`) and run a ThreadSanitizer build
  (`-DCMAKE_CXX_FLAGS=-fsanitize=thread` in a separate build directory).
- **Measure before optimising.** Profile first. The brain is about 2% of the time; neighbour
  search dominates. Compiler flags beyond `-O3` measured within 2% of each other.
- **Experiments need several seeds.** Single runs are noisy (about 200 creatures). Use 6 or
  more seeds and average over time, not one end snapshot; run them in parallel with
  `--threads 1` each.
- Changing anything that affects the simulation changes every seed's trajectory: update
  the numbers in README.md if they no longer hold.

## Findings so far (see README.md for details)

- Local thermal adaptation evolves (r about 0.55), but only with a preferred-temperature
  mutation step of ±1.2 C. At ±0.5 the population stayed one generalist (gene swamping from
  the crowded warm centre).
- Bergmann's rule does not emerge (size is slightly larger in warm places, with or without
  body heat). The likely reason: energy is an absolute pool with a fixed birth threshold, so
  bulk is never an advantage in itself.
- No climate-variability effect on tolerance. Aquatic lineages appear in some seeds.
- Under +10 C warming: about half genetic tracking, half moving to cooler places.

## Agreed next steps

1. Sexual reproduction (mating, recombination, a mate-preference gene) and a family-tree
   view, to test whether the thermal and water niches split into separate species.
2. Energy that depends on body size (storage scales with mass) plus scarce food in winter,
   to test the fasting-endurance explanation for Bergmann's rule.
3. A built-in experiment mode (`--runs N --csv out.csv`).
4. For larger worlds: creature data in compact arrays, batched drawing, save and load.
