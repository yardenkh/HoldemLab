// Tournament regression checks use real engine hands and deterministic
// policies.
#include "tournament/Tournament.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <vector>
namespace poker {
// Exceptions keep checks enabled in Release, unlike C assert().
void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
template <class Exception, class Function> void rejects(Function function) {
  bool rejected = false;
  try {
    function();
  } catch (const Exception &) {
    rejected = true;
  }
  check(rejected, "Expected tournament operation to be rejected");
}
struct Policy : DecisionProvider {
  enum Mode { Passive, Fold, AllIn, Invalid } mode = Passive;
  PlayerDecision choose_action(const PlayerView &view) override {
    if (mode == Invalid)
      return {ActionType::RaiseTo, -1};
    if (mode == Fold)
      return {ActionType::Fold, 0};
    if (mode == AllIn && view.legal_actions.can_all_in)
      return {ActionType::AllIn, 0};
    return {view.legal_actions.can_check ? ActionType::Check : ActionType::Call,
            0};
  }
};
void attach(Tournament &tournament, Policy &policy) {
  for (int id = 0; id < tournament.result().num_players; ++id)
    tournament.set_decision_provider(id, policy);
}
struct BlindRecorder : GameObserver {
  std::vector<std::pair<int, int>> payments;
  void on_stage_started(PokerStage stage, const Table &t,
                        const PotManager &pots) override {
    if (stage == PokerStage::PreFlop)
      payments.push_back({pots.round_contribution(t.small_blind_id),
                          pots.round_contribution(t.big_blind_id)});
  }
};
void blind_progression() {
  TournamentConfig config;
  config.starting_chips = 10000;
  config.blind_levels = {{5, 10, 2}, {10, 20, 1}, {20, 40, 1}};
  config.prizes = {100};
  Policy passive;
  BlindRecorder observer;
  Tournament tournament(3, config, 71);
  attach(tournament, passive);
  tournament.set_observer(&observer);
  tournament.run(2);
  check(tournament.result().hands_played == 2 &&
            tournament.blind_level_index() == 0,
        "First level ended at the wrong hand");
  tournament.run(3);
  // The final level persists; its duration is not an artificial end condition.
  check(observer.payments ==
            std::vector<std::pair<int, int>>{
                {5, 10}, {5, 10}, {10, 20}, {20, 40}, {20, 40}},
        "Incorrect blind progression or observer forwarding");
  check(!tournament.finished() && tournament.result().hands_played == 5,
        "Bounded run did not pause without finishing the tournament");
  for (int id = 0; id < 3; ++id)
    check(tournament.result().players[id].first_place == 0,
          "Active player was assigned a final position");
}
void tied_finishes() {
  TournamentConfig config;
  config.starting_chips = 100;
  config.blind_levels = {{5, 10, 10}};
  config.prizes = {1000, 302, 200, 0};
  Policy allin;
  allin.mode = Policy::AllIn;
  bool checked = false;
  for (std::uint32_t seed = 0; seed < 100 && !checked; ++seed) {
    Tournament tournament(4, config, seed);
    attach(tournament, allin);
    tournament.play_one_hand();
    if (!tournament.finished())
      continue; // A split pot may leave several players.
    checked = true;
    const auto &result = tournament.result();
    std::int64_t total = 0;
    int tied_index = 0;
    for (int id = 0; id < 4; ++id) {
      const auto &player = result.players[id];
      total += player.prize;
      if (id == result.winner_id) {
        check(player.first_place == 1 && player.last_place == 1 &&
                  player.prize == 1000,
              "Winner was not paid first prize");
      } else {
        // Equal stacks BEFORE posting blinds tie across second through fourth.
        // 302 + 200 + 0 = 502 units, shared as 168, 167, 167 by ascending seat
        // ID.
        check(player.first_place == 2 && player.last_place == 4 &&
                  player.prize == (tied_index++ == 0 ? 168 : 167) &&
                  player.eliminated_on_hand == 1,
              "Equal-stack tie or integer prize remainder is wrong");
      }
    }
    check(total == 1502, "Prize splitting lost or created money");
    check(!tournament.play_one_hand(), "Finished tournament played again");
    tournament.run();
    check(tournament.result().hands_played == 1,
          "Finished run changed results");
  }
  check(checked, "No deterministic simultaneous-elimination scenario found");
}
void unequal_finishes() {
  TournamentConfig config;
  config.starting_chips = 100;
  config.blind_levels = {{5, 10, 10}};
  config.prizes = {1000, 300, 100};
  Policy fold;
  fold.mode = Policy::Fold;
  Policy allin;
  allin.mode = Policy::AllIn;
  bool checked = false;
  for (std::uint32_t seed = 0; seed < 100 && !checked; ++seed) {
    Tournament tournament(4, config, seed);
    attach(tournament, fold);
    tournament.play_one_hand();
    std::array<int, MAX_PLAYERS> before{};
    for (int id = 0; id < 4; ++id)
      before[id] = tournament.table().players[id].cash;
    // A fold-around hand creates unequal stacks: two untouched seats, a short
    // small blind, and the big blind who won. Capture those settled stacks.
    attach(tournament, allin);
    tournament.play_one_hand();
    if (!tournament.finished())
      continue;
    checked = true;
    std::vector<int> eliminated;
    for (int id = 0; id < 4; ++id)
      if (id != tournament.winner_id())
        eliminated.push_back(id);
    std::sort(eliminated.begin(), eliminated.end(),
              [&](int a, int b) { return before[a] > before[b]; });
    int first = 0;
    while (first < 3) {
      int end = first + 1;
      while (end < 3 && before[eliminated[end]] == before[eliminated[first]])
        ++end;
      for (int index = first; index < end; ++index) {
        const auto &standing = tournament.result().players[eliminated[index]];
        check(standing.first_place == first + 2 &&
                  standing.last_place == end + 1 &&
                  standing.eliminated_on_hand == 2,
              "Simultaneous elimination did not use pre-blind stack sizes");
      }
      first = end;
    }
  }
  check(checked, "No unequal-stack simultaneous elimination found");
}
void validation_and_retry() {
  TournamentConfig config;
  config.starting_chips = 1000;
  config.blind_levels = {{5, 10, 2}};
  config.prizes = {100, 50};
  auto bad = config;
  bad.prizes.clear();
  rejects<std::invalid_argument>([&] { Tournament t(2, bad); });
  bad = config;
  bad.prizes = {50, 100};
  rejects<std::invalid_argument>([&] { Tournament t(2, bad); });
  bad = config;
  bad.prizes = {std::numeric_limits<std::int64_t>::max(), 1};
  rejects<std::invalid_argument>([&] { Tournament t(2, bad); });
  bad = config;
  bad.blind_levels.clear();
  rejects<std::invalid_argument>([&] { Tournament t(2, bad); });
  bad = config;
  bad.blind_levels[0].hands_per_level = 0;
  rejects<std::invalid_argument>([&] { Tournament t(2, bad); });
  bad = config;
  bad.blind_levels.push_back({1, 2, 10});
  rejects<std::invalid_argument>([&] { Tournament t(2, bad); });
  Policy invalid;
  invalid.mode = Policy::Invalid;
  Policy passive;
  Tournament interrupted(2, config, 42), uninterrupted(2, config, 42);
  attach(interrupted, invalid);
  attach(uninterrupted, passive);
  rejects<std::invalid_argument>([&] { interrupted.play_one_hand(); });
  check(interrupted.result().hands_played == 0,
        "Failed hand was counted as complete");
  attach(interrupted, passive);
  interrupted.play_one_hand();
  uninterrupted.play_one_hand();
  for (int id = 0; id < 2; ++id)
    check(interrupted.table().players[id].cash ==
              uninterrupted.table().players[id].cash,
          "Retry restarted a hand or applied blinds twice");

  // Public engine setter validates before mutation and refuses mid-hand edits.
  PokerGame game(2, {1000, 5, 10}, 42);
  rejects<std::invalid_argument>([&] { game.set_blinds(0, 20); });
  rejects<std::invalid_argument>([&] { game.set_blinds(20, 10); });
  game.set_blinds(10, 20);
  game.begin_hand();
  game.advance_to_decision();
  check(game.current_view().current_bet == 20, "New blinds were not posted");
  rejects<std::logic_error>([&] { game.set_blinds(20, 40); });
}
void complete_and_reproducible() {
  TournamentConfig config;
  config.starting_chips = 100;
  config.blind_levels = {{5, 10, 1}, {100, 200, 1}};
  config.prizes = {500, 300, 200};
  Policy passive;
  Tournament a(8, config, 42), b(8, config, 42);
  attach(a, passive);
  attach(b, passive);
  // Final blinds exceed many stacks, so short-blind all-ins run through the
  // real engine. Multiple hands must still settle and conserve total chips.
  a.run();
  b.run();
  check(a.finished() && a.result().players_remaining == 1 &&
            a.winner_id() == b.winner_id(),
        "Tournament did not finish reproducibly");
  check(a.result().hands_played == b.result().hands_played,
        "Hand counts differ with identical seeds");
  std::int64_t total = 0;
  for (int id = 0; id < 8; ++id) {
    const auto &x = a.result().players[id], &y = b.result().players[id];
    check(x.first_place > 0 && x.first_place == y.first_place &&
              x.last_place == y.last_place && x.prize == y.prize &&
              x.eliminated_on_hand == y.eliminated_on_hand,
          "Final standings differ with identical seeds");
    total += x.prize;
  }
  check(total == 1000 && a.table().players[a.winner_id()].cash == 800,
        "Tournament did not conserve prize money or poker chips");
}
} // namespace poker
int main() {
  try {
    poker::blind_progression();
    poker::tied_finishes();
    poker::unequal_finishes();
    poker::validation_and_retry();
    poker::complete_and_reproducible();
    std::cout << "Tournament tests passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
