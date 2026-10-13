// Complete tournament episodes with isolated policies and reproducible seeds.
#pragma once
#include "simulation/Simulation.hpp"
#include "tournament/Tournament.hpp"
namespace poker {
struct TournamentSimulationOptions {
  std::uint64_t episodes = 1000;
  int players = 6;
  TournamentConfig config; // Supply prizes explicitly, in integer prize units.
  unsigned workers = 1;    // Zero selects available hardware concurrency.
  std::uint64_t seed = 42;
  std::uint64_t first_episode =
      0; // Stable IDs allow experiments to run in chunks.
};
struct TournamentEpisodeResult {
  TournamentResult standings;
  std::uint64_t decisions = 0;
  std::array<int, MAX_PLAYERS> final_stacks{};
};
struct TournamentSimulationResult {
  std::uint64_t tournaments = 0, hands = 0, decisions = 0, checksum = 0;
  std::array<std::int64_t, MAX_PLAYERS> total_prizes{};
  std::array<std::uint64_t, MAX_PLAYERS> wins{};
};
// A seat-aware factory permits different algorithms in the same tournament.
// It runs serially once per seat per worker. Mutable providers are never shared
// across seats/workers; reset_for_episode receives a separate seed for each
// seat. Construct equivalent policies per seat regardless of worker count;
// reset_for_episode must clear episode-local state for reproducibility.
using TournamentProviderFactory =
    std::function<std::unique_ptr<DecisionProvider>(int)>;
// One episode runs to completion, never to an arbitrary hand cutoff. Optional
// output has exactly one row per episode. Discard partial output if a call
// throws.
TournamentSimulationResult run_tournament_simulations(
    const TournamentSimulationOptions &, const TournamentProviderFactory &,
    std::span<TournamentEpisodeResult> output = {},
    const FastHandEvaluator & = default_fast_hand_evaluator());
} // namespace poker
