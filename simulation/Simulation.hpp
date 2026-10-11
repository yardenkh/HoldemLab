// Independent parallel experiments and reusable paused environments for batched
// model decisions.

#pragma once
#include "game/PokerGame.hpp"
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace poker {
// Split an experiment seed into stable per-episode seeds. Scheduling and worker
// count do not affect this mapping; providers should seed their own RNG with
// it.
std::uint64_t simulation_seed(std::uint64_t master, std::uint64_t episode);
struct SimulationOptions {
  // Episode = an independently reset starting state, played for one or more
  // hands.
  std::uint64_t episodes = 1000;
  int players = 6;
  GameConfig config{};
  int hands_per_episode = 1; // <= 0 plays each match to completion.
  unsigned workers = 1; // 0 uses hardware_concurrency, with a minimum of one.
  std::uint64_t seed = 42;
  // Continue the same deterministic experiment in bounded-memory chunks.
  std::uint64_t first_episode = 0;
};
struct EpisodeResult {
  // Final stacks belong to the full episode; hands/decisions are actual counts,
  // which may fall below a requested limit when the match ends early.
  std::uint64_t hands = 0, decisions = 0;
  std::array<int, MAX_PLAYERS> final_stacks{};
};
struct SimulationResult {
  // Aggregated work plus a reproducible checksum of episode results. A checksum
  // is a quick comparison aid, not a substitute for detailed correctness tests.
  std::uint64_t hands = 0, decisions = 0, checksum = 0;
};
// Called once per seat per worker to create private mutable providers. Each
// provider's reset_for_episode(seed) must reset all episode-local policy state.
// A shared model can be referenced from providers, but mutable inference state
// needs its own synchronization. The factory itself is called serially.
using ProviderFactory = std::function<std::unique_ptr<DecisionProvider>()>;
// Optional output is caller-owned, one slot per episode; write it to disk in
// chunks after this call. With an empty output span only aggregates are kept.
SimulationResult
run_simulations(const SimulationOptions &, const ProviderFactory &,
                std::span<EpisodeResult> output = {},
                const FastHandEvaluator & = default_fast_hand_evaluator());

// Reusable collection of paused games for batched inference. collect() returns
// contiguous observations; game_ids() maps those rows back to environments.
// apply() consumes one action per row. Completed games wait for explicit reset,
// allowing callers to read rewards before starting the next episode.
class BatchSimulator {
public:
  BatchSimulator(std::size_t count, int players, const GameConfig &config = {},
                 std::uint64_t seed = 42,
                 const FastHandEvaluator & = default_fast_hand_evaluator());
  std::span<const PlayerView> collect();
  std::span<const std::size_t> game_ids() const { return ids_; }
  void apply(std::span<const PlayerDecision> actions);
  const PokerGame &game(std::size_t id) const;
  void reset(std::size_t id, std::uint32_t seed);

private:
  // These buffers allocate at construction and reuse capacity every batch.
  std::vector<PokerGame> games_;
  std::vector<PlayerView> views_;
  // ids_[row] maps views_[row] to games_[id]. pending_ keeps collect from
  // advancing games again before the caller supplies this batch's actions.
  std::vector<std::size_t> ids_;
  bool pending_ = false;
};
} // namespace poker
