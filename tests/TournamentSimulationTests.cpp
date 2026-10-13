// Parallel tournament experiments must agree with direct seeded tournament
// runs.
#include "simulation/TournamentSimulation.hpp"
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
namespace poker {
void simulation_check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
struct TournamentRandomPolicy : DecisionProvider {
  std::mt19937_64 rng;
  void reset_for_episode(std::uint64_t seed) override { rng.seed(seed); }
  PlayerDecision choose_action(const PlayerView &view) override {
    std::array<PlayerDecision, 5> choices{};
    int count = 0;
    const auto &a = view.legal_actions;
    if (a.can_fold)
      choices[count++] = {ActionType::Fold, 0};
    if (a.can_check)
      choices[count++] = {ActionType::Check, 0};
    if (a.can_call)
      choices[count++] = {ActionType::Call, 0};
    if (a.can_raise)
      choices[count++] = {ActionType::RaiseTo, a.min_raise_to};
    if (a.can_all_in)
      choices[count++] = {ActionType::AllIn, 0};
    return choices[std::uniform_int_distribution<int>(0, count - 1)(rng)];
  }
};
bool same_episode(const TournamentEpisodeResult &a,
                  const TournamentEpisodeResult &b) {
  if (a.decisions != b.decisions || a.final_stacks != b.final_stacks ||
      a.standings.hands_played != b.standings.hands_played ||
      a.standings.winner_id != b.standings.winner_id ||
      a.standings.players_remaining != b.standings.players_remaining ||
      a.standings.num_players != b.standings.num_players)
    return false;
  for (int id = 0; id < MAX_PLAYERS; ++id) {
    const auto &x = a.standings.players[id], &y = b.standings.players[id];
    if (x.first_place != y.first_place || x.last_place != y.last_place ||
        x.prize != y.prize || x.eliminated_on_hand != y.eliminated_on_hand)
      return false;
  }
  return true;
}
void parallel_and_direct() {
  TournamentSimulationOptions options;
  options.episodes = 40;
  options.players = 4;
  options.first_episode = 19;
  options.config.starting_chips = 100;
  options.config.blind_levels = {{5, 10, 2}, {100, 200, 1}};
  options.config.prizes = {500, 301, 199};
  const TournamentProviderFactory factory = [](int) {
    return std::make_unique<TournamentRandomPolicy>();
  };
  std::vector<TournamentEpisodeResult> one(40), many(40);
  const auto single = run_tournament_simulations(options, factory, one);
  options.workers = 4;
  const auto parallel = run_tournament_simulations(options, factory, many);
  simulation_check(single.tournaments == 40 &&
                       single.checksum == parallel.checksum &&
                       single.hands == parallel.hands &&
                       single.decisions == parallel.decisions &&
                       single.total_prizes == parallel.total_prizes &&
                       single.wins == parallel.wins,
                   "Worker count changed tournament experiment results");
  for (std::size_t id = 0; id < one.size(); ++id) {
    simulation_check(same_episode(one[id], many[id]),
                     "Parallel output row changed");
    simulation_check(one[id].standings.players_remaining == 1,
                     "Episode was returned before tournament completion");
    std::int64_t prizes = 0;
    for (int seat = 0; seat < options.players; ++seat)
      prizes += one[id].standings.players[seat].prize;
    simulation_check(prizes == 1000, "Episode lost prize money");
  }
  simulation_check(std::accumulate(single.total_prizes.begin(),
                                   single.total_prizes.end(),
                                   std::int64_t{0}) == 40000,
                   "Aggregate prize rewards are wrong");
  simulation_check(std::accumulate(single.wins.begin(), single.wins.end(),
                                   std::uint64_t{0}) == 40,
                   "Aggregate win counts are wrong");

  // Chunk IDs preserve the same scenarios and rows as the corresponding full
  // run.
  options.episodes = 7;
  options.first_episode += 12;
  std::vector<TournamentEpisodeResult> chunk(7);
  run_tournament_simulations(options, factory, chunk);
  for (std::size_t id = 0; id < chunk.size(); ++id)
    simulation_check(same_episode(chunk[id], one[id + 12]),
                     "Chunking changed tournament seeds");

  // Independently reproduce the first scenario through the public wrapper API.
  const auto seed = simulation_seed(options.seed, 19);
  Tournament direct(4, options.config, static_cast<std::uint32_t>(seed));
  std::array<TournamentRandomPolicy, 4> providers;
  for (int id = 0; id < 4; ++id) {
    providers[id].reset_for_episode(simulation_seed(seed, id));
    direct.set_decision_provider(id, providers[id]);
  }
  direct.run();
  TournamentEpisodeResult expected;
  expected.standings = direct.result();
  expected.decisions = direct.decisions_applied();
  for (int id = 0; id < 4; ++id)
    expected.final_stacks[id] = direct.table().players[id].cash;
  simulation_check(same_episode(expected, one[0]),
                   "Runner disagrees with direct tournament play");
  // Reusing a completed wrapper must reset ranks, prizes, levels, and counters.
  direct.reset(static_cast<std::uint32_t>(seed));
  simulation_check(!direct.finished() && direct.result().hands_played == 0 &&
                       direct.blind_level_index() == 0 &&
                       direct.decisions_applied() == 0,
                   "Tournament reset retained completed state");
  for (int id = 0; id < 4; ++id)
    providers[id].reset_for_episode(simulation_seed(seed, id));
  direct.run();
  simulation_check(direct.result().winner_id == expected.standings.winner_id &&
                       direct.result().hands_played ==
                           expected.standings.hands_played,
                   "Reset failed to reproduce tournament");
}
void invalid_inputs_and_factories() {
  TournamentSimulationOptions options;
  options.players = 2;
  options.episodes = 2;
  options.config.starting_chips = 100;
  options.config.blind_levels = {{100, 200, 1}};
  options.config.prizes = {100};
  std::vector<int> requested;
  const TournamentProviderFactory factory = [&](int seat) {
    requested.push_back(seat);
    return std::make_unique<TournamentRandomPolicy>();
  };
  options.workers = 2;
  run_tournament_simulations(options, factory);
  simulation_check(requested == std::vector<int>{0, 1, 0, 1},
                   "Factory was not seat aware or serial");
  bool rejected = false;
  try {
    run_tournament_simulations(
        options, [](int) { return std::unique_ptr<DecisionProvider>{}; });
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  simulation_check(rejected, "Null provider was accepted");
  options.config.prizes = {std::numeric_limits<std::int64_t>::max()};
  rejected = false;
  try {
    run_tournament_simulations(options, factory);
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  simulation_check(rejected, "Aggregate prize overflow was accepted");
  options.config.prizes = {100};
  options.episodes = 0;
  simulation_check(run_tournament_simulations(options, factory).tournaments ==
                       0,
                   "Zero episodes played a tournament");
}
} // namespace poker
int main() {
  try {
    poker::parallel_and_direct();
    poker::invalid_inputs_and_factories();
    std::cout << "Tournament simulation tests passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
