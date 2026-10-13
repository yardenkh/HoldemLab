// Keep tournament experiment orchestration separate from ordinary hand batches.
#include "simulation/TournamentSimulation.hpp"
#include <algorithm>
#include <atomic>
#include <exception>
#include <limits>
#include <stdexcept>
#include <thread>
namespace poker {
TournamentSimulationResult
run_tournament_simulations(const TournamentSimulationOptions &options,
                           const TournamentProviderFactory &factory,
                           std::span<TournamentEpisodeResult> output,
                           const FastHandEvaluator &evaluator) {
  if (!factory || (!output.empty() && output.size() != options.episodes))
    throw std::invalid_argument(
        "Missing tournament factory or incorrect output size");
  // Validate the tournament even for zero episodes; configuration errors should
  // not depend on a scheduling choice. This object also validates integer
  // limits.
  Tournament validated(options.players, options.config, 0, evaluator);
  if (!options.episodes)
    return {};
  if (options.first_episode >
      std::numeric_limits<std::uint64_t>::max() - (options.episodes - 1))
    throw std::invalid_argument("Tournament episode ID range overflows");
  std::int64_t pool = 0;
  for (auto prize : options.config.prizes)
    pool += prize;
  // No seat's accumulated prize can exceed all prize pools combined.
  if (pool &&
      options.episodes > static_cast<std::uint64_t>(
                             std::numeric_limits<std::int64_t>::max() / pool))
    throw std::invalid_argument("Aggregate tournament prizes would overflow");
  unsigned workers = options.workers
                         ? options.workers
                         : std::max(1u, std::thread::hardware_concurrency());
  workers =
      static_cast<unsigned>(std::min<std::uint64_t>(workers, options.episodes));
  std::vector<std::unique_ptr<DecisionProvider>> providers;
  providers.reserve(static_cast<std::size_t>(workers) * options.players);
  for (unsigned worker = 0; worker < workers; ++worker)
    for (int seat = 0; seat < options.players; ++seat) {
      auto provider = factory(seat);
      if (!provider)
        throw std::invalid_argument("Tournament factory returned null");
      providers.push_back(std::move(provider));
    }
  // Each worker updates only its own totals and its own episode output rows.
  struct alignas(64) WorkerResult {
    TournamentSimulationResult total;
    std::exception_ptr error;
  };
  std::vector<WorkerResult> results(workers);
  std::atomic<bool> stop{false};
  // Counter overflow is an error, not a silently wrapped experiment metric.
  // The checksum intentionally wraps, but hand/decision totals must remain
  // exact.
  const auto add_count = [](std::uint64_t &target, std::uint64_t amount) {
    if (amount > std::numeric_limits<std::uint64_t>::max() - target)
      throw std::overflow_error("Tournament experiment counter overflow");
    target += amount;
  };
  const auto work = [&](unsigned worker) {
    try {
      Tournament tournament(options.players, options.config, 0, evaluator);
      const auto first_provider =
          static_cast<std::size_t>(worker) * options.players;
      for (int seat = 0; seat < options.players; ++seat)
        tournament.set_decision_provider(seat,
                                         *providers[first_provider + seat]);
      // Strided IDs and seed splitting make worker scheduling irrelevant to
      // each tournament's cards, policy seeds, and final output row.
      for (std::uint64_t id = worker;
           id < options.episodes && !stop.load(std::memory_order_relaxed);) {
        const auto seed =
            simulation_seed(options.seed, options.first_episode + id);
        tournament.reset(static_cast<std::uint32_t>(seed));
        for (int seat = 0; seat < options.players; ++seat)
          providers[first_provider + seat]->reset_for_episode(
              simulation_seed(seed, seat));
        tournament.run();
        TournamentEpisodeResult episode;
        episode.standings = tournament.result();
        episode.decisions = tournament.decisions_applied();
        auto hash = simulation_seed(seed, episode.standings.hands_played);
        for (int seat = 0; seat < options.players; ++seat) {
          episode.final_stacks[seat] = tournament.table().players[seat].cash;
          const auto &finish = episode.standings.players[seat];
          hash = simulation_seed(hash, finish.first_place);
          hash = simulation_seed(hash, finish.last_place);
          hash =
              simulation_seed(hash, static_cast<std::uint64_t>(finish.prize));
          hash = simulation_seed(hash, finish.eliminated_on_hand);
          hash = simulation_seed(hash, episode.final_stacks[seat]);
          results[worker].total.total_prizes[seat] += finish.prize;
          results[worker].total.wins[seat] +=
              episode.standings.winner_id == seat;
        }
        hash = simulation_seed(hash, episode.decisions);
        if (!output.empty())
          output[static_cast<std::size_t>(id)] = episode;
        auto &total = results[worker].total;
        ++total.tournaments;
        add_count(total.hands, episode.standings.hands_played);
        add_count(total.decisions, episode.decisions);
        total.checksum +=
            hash; // Unsigned checksum addition deliberately wraps.
        if (options.episodes - id <= workers)
          break;
        id += workers;
      }
    } catch (...) {
      // Worker failures are returned to the caller after every thread joins.
      results[worker].error = std::current_exception();
      stop.store(true, std::memory_order_relaxed);
    }
  };
  if (workers == 1)
    work(0);
  else {
    std::vector<std::jthread> threads;
    threads.reserve(workers);
    for (unsigned worker = 0; worker < workers; ++worker)
      threads.emplace_back(work, worker);
  }
  TournamentSimulationResult total;
  for (const auto &worker : results) {
    if (worker.error)
      std::rethrow_exception(worker.error);
    total.tournaments += worker.total.tournaments;
    add_count(total.hands, worker.total.hands);
    add_count(total.decisions, worker.total.decisions);
    total.checksum += worker.total.checksum;
    for (int seat = 0; seat < options.players; ++seat) {
      total.total_prizes[seat] += worker.total.total_prizes[seat];
      total.wins[seat] += worker.total.wins[seat];
    }
  }
  return total;
}
} // namespace poker
