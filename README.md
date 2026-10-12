feel free to send suggestions at:
yardenks@gmail.com

# HoldemLab

A fast C++ Texas Hold'em engine for interactive play, large-scale simulations,
and poker AI research.

- No-limit Hold'em for 2–8 players, with side pots and split payouts.
- Visual terminal play against simple AI opponents.
- Fast hand evaluation using precomputed lookup tables.
- Reproducible simulations, parallel workers, snapshots, and batched decisions.

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

Choose a player count. You play seat 0. Commands: `fold`, `check`, `call`,
`raise 500`, and `allin`. Raise amounts are totals for the current betting round.

## Simulate

```bash
cmake --build build --target poker_simulation_benchmark -j
./build/poker_simulation_benchmark 10000 4
```

The arguments specify simulation episodes and worker threads.
The included AI is a simple baseline; it does not assess hand strength.

## Test

```bash
ctest --test-dir build --output-on-failure
```

See [SIMULATION_GUIDE.md](SIMULATION_GUIDE.md) for research APIs and supported
betting rules.

See [AI_RESEARCH_PLAN.md](AI_RESEARCH_PLAN.md) for the mathematical AI roadmap,
implementation milestones, and linked research papers and dissertations.
