feel free to send suggestions at:
yardenks@gmail.com

# poker simulator++

A fast C++ Texas Hold'em engine for interactive play, large-scale simulations,
and poker AI research.

- No-limit Hold'em for 2–8 players, with side pots and split payouts.
- Visual terminal play against simple AI opponents.
- Fast hand evaluation using precomputed lookup tables.
- Reproducible simulations, parallel workers, snapshots, and batched decisions.
- Single-table tournament API with rising blinds, tied finishes, and prize payouts.

## Build

Requires a C++20 compiler and CMake. Run in Linux or Ubuntu/WSL:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Play

```bash
./build/poker_cli
```

The default mode is a freezeout tournament with increasing blinds and finishing-place
prizes. Use `./build/poker_cli --game` for ordinary fixed-blind play.
Choose a player count. You play seat 0. Commands: `fold`, `check`, `call`,
`raise 500`, and `allin`. Raise amounts are totals for the current betting round.

## Simulate

```bash
cmake --build build --target poker_tournament_simulation -j
./build/poker_tournament_simulation 10000 4
```

The arguments specify complete tournaments and worker threads. The runner reports
hands, decisions, wins, and total prizes per seat. Prizes are abstract integer units.
For ordinary hand/match experiments, build and run `poker_simulation_benchmark`.
The included AI is a simple baseline; it does not assess hand strength.

## Test

```bash
ctest --test-dir build --output-on-failure
```

See [SIMULATION_GUIDE.md](SIMULATION_GUIDE.md) for research APIs and supported
betting rules.

See [AI_RESEARCH_PLAN.md](AI_RESEARCH_PLAN.md) for the mathematical AI roadmap,
implementation milestones, and linked research papers and dissertations.
