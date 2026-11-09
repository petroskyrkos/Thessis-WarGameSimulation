# War Game Simulation

Agent-based simulation of a Greece–Turkey war game. MSc thesis, Web
Intelligence. The eventual research question compares the **Mosaic Warfare**
concept (DARPA/RAND) against a classical force structure.

> ### This build is Phase 1: classical (monolithic) forces
>
> Each platform carries its own full set of capabilities, as a conventional
> order of battle does. **There is no Mosaic code here.** Mosaic — capabilities
> fractionated across cheap, expendable assets forming ad-hoc battle groups —
> is phase 3, and is not started until the classical simulation is complete,
> running, and validated as methodologically sound. The classical build is the
> baseline Mosaic will later be measured against.

The model is specified in [`docs/MODEL.md`](docs/MODEL.md). The current
deliverable is specified in [`docs/PILOT.md`](docs/PILOT.md).

---

## Build

Requires CMake ≥ 3.20 and a C++20 compiler. No external dependencies — the one
third-party library (nlohmann/json) is vendored under `third_party/`, so the
build works offline.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/wargame                # self-checks, then one measured battle
./build/wargame --live         # watch a battle instead
./build/wargame --log          # write a full record of the battle to results/
./build/wargame --symmetry    # 150 battles, one seed each: checks that no side is favoured
```

`Release` matters. Benchmark numbers from a `Debug` build are meaningless, and
the whole point of the pilot is to measure the event loop.

With Visual Studio:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

---

## Configuration

**No numeric value lives in the source.** Every parameter is in
`config/sim.json`, read by dotted path:

```cpp
const auto boxKm = cfg.req<double>("sim.box_km");
```

Each entry in the config is tagged with its provenance:

| Tag | Meaning |
|---|---|
| `[meeting]` | settled with the supervisor — see `docs/MODEL.md` |
| `[derived]` | computed from a `[meeting]` figure or from public data |
| `[pilot]` | invented so the benchmark can run. **Not a researched figure and not to be quoted in the thesis.** |
| `null` | not researched yet |

Reading a `null` throws and names the key, so a missing figure fails loudly
instead of silently producing a wrong number. The program lists every remaining
placeholder at startup.

---

## Reproducibility

A run is fully determined by `sim.seed`. `std::mt19937_64` generates the same
stream everywhere, but the standard distributions do not — their internals are
implementation-defined, so the same seed gives different draws under MSVC and
GCC. `src/util/Rng.h` therefore writes out the uniform conversions by hand. Who acts first among
events on the same tick is also drawn from the seeded RNG (`sim.tie_break`), never
decided by how units happen to be numbered: that once gave the army on the right
two wins in three. Each run is reproducible from its seed; `--symmetry` checks the
balance over many seeds.

---

## Layout

```
src/main.cpp      entry point: arguments, --live / --log, runs the self-checks
src/config/      Config - JSON loader with provenance and TODO detection
src/util/        Rng - reproducible random numbers
src/world/       GridPos, Side, World (grid, occupancy index, octile range)
src/pathfinding/ AStar
src/engine/      Event, Simulation (future event list, dispatch, run loop)
src/entities/    Entity, Resource, UnitProfile, PostCombatSurge, DynamicUnit
src/entities/infantry/  InfantryBattalion (state machine), Tactics
src/scenarios/   PlainBattle - the pilot's 100-a-side flat-ground battle
src/log/         EventLog - battle record (csv + text) and invariant audit
src/viewer/      Viewer interface, Win32 window, console fallback
src/selfcheck/   built-in checks run when the program starts with no flags
config/          all parameters (sim.json)
docs/            MODEL.md (the model), PILOT.md (current deliverable),
                 CODE_ANALYSIS.txt (what every file does and why - kept up to date)
third_party/     vendored nlohmann/json
```

---

## Progress

- [x] **Step 1** — skeleton: CMake, config loader with provenance and TODO
      detection, reproducible RNG
- [x] **Step 2** — World: flat grid in one contiguous vector, `GridPos`, `Cell`,
      Moore neighbourhood, octile range metric. Self-checks run at startup.
- [x] **Step 3** — DES core: `Event` as a POD with inline args, future event
      list ordered by tick then creation order, entities in a flat table,
      `postEvent`/`dispatch`/`run`, run checksum for determinism
- [x] **Step 4** — `DynamicUnit`: three resources, lazy consumption settled from
      elapsed time, the post-combat supply surge with cumulative windows,
      linear capability loss from damage
- [x] **Step 5** — `InfantryBattalion` with a sense/approach/engage state
      machine, A\* with reusable buffers, an intrusive occupancy index, and the
      plain-battle scenario
- [x] **Live viewer** — a Win32 window (and a console fallback) that plays a
      run at a chosen speed. Detached from the engine, so a watched run and a
      measured run produce the same checksum.
- [x] **Battle log** — every event recorded to `results/battle.csv` (one row
      per event, 30 columns) and `results/battle.log` (one sentence per event),
      with periodic situation reports and an audit that writes an `anomaly`
      line the moment an invariant breaks.
- [x] **Fairness and stalemate** — random tie-break (`sim.tie_break`) and a stalemate
      stop (`sim.stalemate_after_hours`), proved by `--symmetry`
- [x] **Step 6** — engagement model: per-shot hit probability from the unit profile,
      lowered by electronic warfare (`damage.ew_reduction`, `damage.ew_mode`,
      `ew_coverage`). A miss still costs ammunition and starts the supply surge. EW is
      switched off in the pilot battle (`ew_coverage` = 0)
- [x] **Step 7** — benchmark harness: `wargame --benchmark` times the battle at 200 to
      2,000 objects (median of repeated runs, events/s and ticks/s, per-window CSV), and
      `wargame --dump-state` writes every unit's state at intervals

Target: the pilot running by **10–15 November 2026**.
