// Assigns reproducible episode seeds, isolates worker state, and gathers
// pending decision batches.

#include "simulation/Simulation.hpp"
#include <algorithm>
#include <atomic>
#include <exception>
#include <limits>
#include <stdexcept>
#include <thread>

namespace poker {
std::uint64_t simulation_seed(std::uint64_t master, std::uint64_t episode) {
  // Unsigned arithmetic wraps intentionally. This is deterministic seed
  // mixing, not a cryptographic RNG or a source of fresh operating-system
  // entropy. SplitMix64 mixing creates well-dispersed deterministic episode
  // identifiers.
  std::uint64_t z = master + (episode + 1) * 0x9e3779b97f4a7c15ULL;
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  return z ^ (z >> 31);
}
SimulationResult run_simulations(const SimulationOptions &options,
                                 const ProviderFactory &factory,
                                 std::span<EpisodeResult> output,
                                 const FastHandEvaluator &evaluator) {
  if (!output.empty() && output.size() != options.episodes)
    throw std::invalid_argument("Output needs one slot per episode");
  if (!factory)
    throw std::invalid_argument("Missing provider factory");
  if (options.players < 2 || options.players > MAX_PLAYERS)
    throw std::invalid_argument("Invalid simulation player count");
  if (!options.episodes)
    return {};
  if (options.first_episode >
      std::numeric_limits<std::uint64_t>::max() - (options.episodes - 1))
    throw std::invalid_argument("Episode ID range overflows");
  unsigned workers = options.workers
                         ? options.workers
                         : std::max(1u, std::thread::hardware_concurrency());
  workers =
      static_cast<unsigned>(std::min<std::uint64_t>(workers, options.episodes));
  std::vector<std::unique_ptr<DecisionProvider>> providers;
  const std::size_t provider_count =
      static_cast<std::size_t>(workers) * options.players;
  providers.reserve(provider_count);
  for (std::size_t i = 0; i < provider_count; ++i) {
    auto provider = factory();
    if (!provider)
      throw std::invalid_argument("Provider factory returned null");
    providers.push_back(std::move(provider));
  }
  // Workers accumulate privately, avoiding a contended counter on every action.
  struct alignas(64) WorkerResult {
    // Separate workers' counters in memory to reduce interference between
    // CPU caches. Each worker writes only its own result and exception slot.
    SimulationResult total;
    std::exception_ptr error;
  };
  std::vector<WorkerResult> results(workers);
  std::atomic<bool> stop{false};
  const auto work = [&](unsigned worker) {
    try {
      PokerGame game(options.players, options.config, 0, evaluator);
      const std::size_t first_provider =
          static_cast<std::size_t>(worker) * options.players;
      for (int seat = 0; seat < options.players; ++seat)
        game.set_decision_provider(seat, *providers[first_provider + seat]);
      // Strided episodes need no shared scheduling lock and still preserve IDs.
      for (std::uint64_t id = worker;
           id < options.episodes && !stop.load(std::memory_order_relaxed);) {
        const auto seed =
            simulation_seed(options.seed, options.first_episode + id);
        game.reset(static_cast<std::uint32_t>(seed));
        // Seat-specific RNGs avoid sharing a strategy's hidden state across
        // players.
        for (int seat = 0; seat < options.players; ++seat)
          providers[first_provider + seat]->reset_for_episode(
              simulation_seed(seed, seat));
        EpisodeResult episode;
        while (!game.match_finished() &&
               (options.hands_per_episode <= 0 ||
                episode.hands <
                    static_cast<std::uint64_t>(options.hands_per_episode))) {
          game.play_one_hand();
          ++episode.hands;
        }
        episode.decisions = game.decisions_applied();
        std::uint64_t hash = simulation_seed(seed, episode.hands);
        for (int seat = 0; seat < options.players; ++seat) {
          episode.final_stacks[seat] = game.table().players[seat].cash;
          hash = simulation_seed(
              hash, static_cast<std::uint64_t>(episode.final_stacks[seat]));
        }
        if (!output.empty())
          // Rows use local IDs 0..episodes-1, even when first_episode is
          // nonzero.
          output[static_cast<std::size_t>(id)] = episode;
        results[worker].total.hands += episode.hands;
        results[worker].total.decisions += episode.decisions;
        results[worker].total.checksum += hash;
        if (options.episodes - id <= workers)
          break; // Avoid unsigned wraparound.
        id += workers;
      }
    } catch (...) {
      // Stop requesting more episodes, join workers, then rethrow in the
      // calling thread. Partially filled output should be discarded on failure.
      results[worker].error = std::current_exception();
      stop.store(true, std::memory_order_relaxed);
    }
  };
  if (workers == 1)
    work(0);
  else {
    // jthread joins even if launching a later worker throws an exception.
    std::vector<std::jthread> threads;
    threads.reserve(workers);
    for (unsigned worker = 0; worker < workers; ++worker)
      threads.emplace_back(work, worker);
  }
  SimulationResult total;
  for (const auto &worker : results) {
    if (worker.error)
      std::rethrow_exception(worker.error);
    total.hands += worker.total.hands;
    total.decisions += worker.total.decisions;
    total.checksum += worker.total.checksum;
  }
  return total;
}
BatchSimulator::BatchSimulator(std::size_t count, int players,
                               const GameConfig &config, std::uint64_t seed,
                               const FastHandEvaluator &evaluator) {
  games_.reserve(count);
  views_.reserve(count);
  ids_.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    games_.emplace_back(players, config,
                        static_cast<std::uint32_t>(simulation_seed(seed, i)),
                        evaluator);
    games_.back().begin_hand();
  }
}
std::span<const PlayerView> BatchSimulator::collect() {
  // These spans borrow reusable storage. Consume them before apply/reset;
  // games that finished are omitted until the caller explicitly resets them.
  if (pending_)
    return views_;
  views_.clear();
  ids_.clear();
  for (std::size_t id = 0; id < games_.size(); ++id) {
    if (games_[id].advance_to_decision()) {
      ids_.push_back(id);
      views_.push_back(games_[id].current_view());
    }
  }
  pending_ = !views_.empty();
  return views_;
}
void BatchSimulator::apply(std::span<const PlayerDecision> actions) {
  if (!pending_ || actions.size() != views_.size())
    throw std::invalid_argument("Actions do not match the pending batch");
  // Check the complete batch before modifying any game, making input mistakes
  // recoverable. Game-level application still checks its own cached actions.
  for (std::size_t row = 0; row < actions.size(); ++row)
    if (!validate_decision(views_[row].legal_actions, actions[row]))
      throw std::invalid_argument("Illegal action in batch");
  for (std::size_t row = 0; row < actions.size(); ++row)
    games_[ids_[row]].apply_action(actions[row]);
  pending_ = false;
  views_.clear();
  ids_.clear();
}
const PokerGame &BatchSimulator::game(std::size_t id) const {
  return games_.at(id);
}
void BatchSimulator::reset(std::size_t id, std::uint32_t seed) {
  if (pending_)
    throw std::logic_error("Apply the pending batch before resetting games");
  games_.at(id).reset(seed);
  games_[id].begin_hand();
}
} // namespace poker
