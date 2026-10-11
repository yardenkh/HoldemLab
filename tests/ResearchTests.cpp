// Research API regressions for stepping, branching/replay, batching, sampling,
// and parallel determinism.

#include "simulation/CardSampler.hpp"
#include "simulation/Observation.hpp"
#include "simulation/Simulation.hpp"
#include <iostream>
#include <numeric>
#include <stdexcept>
using namespace poker;
namespace {
// These checks stay active in Release builds, unlike C assert().
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
// An invalid request must throw before it can change the game's state.
template <class Function> void rejects(Function f) {
  bool rejected = false;
  try {
    f();
  } catch (const std::exception &) {
    rejected = true;
  }
  check(rejected, "invalid operation was accepted");
}
// Keep betting predictable so both execution paths can be compared exactly.
struct Passive : DecisionProvider {
  PlayerDecision choose_action(const PlayerView &v) override {
    return {v.legal_actions.can_check ? ActionType::Check : ActionType::Call,
            0};
  }
};
// This provider tests deterministic per-episode policy RNG as well as game RNG.
struct RandomPolicy : DecisionProvider {
  std::mt19937_64 rng{0};
  void reset_for_episode(std::uint64_t seed) override { rng.seed(seed); }
  PlayerDecision choose_action(const PlayerView &v) override {
    std::array<PlayerDecision, 5> choices{};
    int count = 0;
    const auto &a = v.legal_actions;
    if (a.can_fold)
      choices[count++] = {ActionType::Fold, 0};
    if (a.can_check)
      choices[count++] = {ActionType::Check, 0};
    if (a.can_call)
      choices[count++] = {ActionType::Call, 0};
    if (a.can_raise)
      choices[count++] = {ActionType::RaiseTo,
                          std::uniform_int_distribution<int>(
                              a.min_raise_to, a.max_raise_to)(rng)};
    if (a.can_all_in)
      choices[count++] = {ActionType::AllIn, 0};
    check(count > 0, "decision requested without legal choices");
    return choices[std::uniform_int_distribution<int>(0, count - 1)(rng)];
  }
};
// Supply actions until the current hand ends; this does not start another hand.
void finish(PokerGame &g, DecisionProvider &p) {
  while (g.advance_to_decision())
    g.apply_action(p.choose_action(g.current_view()));
}
// A stepped hand must match synchronous play, and restoring a snapshot must
// reproduce the same outcome without sharing mutable state with another game.
void test_steps_and_snapshots() {
  Passive provider;
  PokerGame synchronous(3, {500, 10, 20}, 71), stepped(3, {500, 10, 20}, 71);
  for (int id = 0; id < 3; ++id)
    synchronous.set_decision_provider(id, provider);
  synchronous.play_one_hand();
  stepped.enable_history(true);
  check(stepped.begin_hand(), "begin failed");
  check(stepped.advance_to_decision(), "no initial actor");
  auto before = stepped.snapshot();
  const auto initial = stepped.current_view();
  rejects([&] { stepped.apply_action({ActionType::RaiseTo, -1}); });
  check(stepped.current_view().player_stacks == initial.player_stacks,
        "rejected action moved cash");
  check(stepped.action_history().empty(), "rejected action entered history");
  finish(stepped, provider);
  const auto payouts = stepped.last_payouts();
  const auto rewards = stepped.last_rewards();
  const auto board = stepped.table().community_cards;
  check(std::accumulate(rewards.begin(), rewards.end(), 0) == 0,
        "reward sum not zero");
  check(!stepped.action_history().empty(), "missing history");
  check(payouts == synchronous.last_payouts() &&
            board == synchronous.table().community_cards,
        "stepped and synchronous results differ");
  const auto history_count = stepped.action_history().size();
  stepped.restore(before);
  finish(stepped, provider);
  check(stepped.last_payouts() == payouts &&
            stepped.last_rewards() == rewards &&
            stepped.table().community_cards == board &&
            stepped.action_history().size() == history_count,
        "snapshot replay differs");
  // Branch a second game from the opaque snapshot, with no copied providers.
  PokerGame branch(3, {500, 10, 20});
  branch.restore(before);
  branch.apply_action({ActionType::Fold, 0});
  finish(branch, provider);
  check(branch.action_history()[0].decision.action == ActionType::Fold,
        "branch did not diverge");
  check(stepped.action_history()[0].decision.action != ActionType::Fold,
        "branch changed source game");
  rejects([&] { stepped.current_view(); });
  rejects([&] { stepped.apply_action({ActionType::Check, 0}); });
  stepped.reset(71);
  stepped.begin_hand();
  finish(stepped, provider);
  check(stepped.last_payouts() == payouts,
        "reset did not reproduce initial episode");
  std::array<int, 3> bad{-1, 100, 100};
  rejects([&] { stepped.reset_episode(bad, 0, 71); });
}
void test_batches() {
  Passive p;
  BatchSimulator batch(8, 3, {500, 10, 20}, 123);
  auto views = batch.collect();
  check(views.size() == 8 && batch.collect().data() == views.data(),
        "pending batch changed");
  std::vector<EncodedObservation> encoded(views.size());
  encode_observations(views, encoded, 20);
  check(encoded[0].values[15] == -1 && encoded[0].values[19] == -1,
        "undealt board exposed");
  check(encoded[0].legal_mask[2], "call absent from encoded legal mask");
  std::vector<PlayerDecision> actions(8, {ActionType::Call, 0});
  actions[7] = {ActionType::RaiseTo, -1};
  rejects([&] { batch.apply(actions); });
  check(batch.game(0).decisions_applied() == 0,
        "invalid batch partially applied");
  rejects([&] { batch.reset(0, 1); });
  while (!(views = batch.collect()).empty()) {
    actions.clear();
    for (const auto &v : views)
      actions.push_back(p.choose_action(v));
    batch.apply(actions);
  }
  for (std::size_t id = 0; id < 8; ++id) {
    PokerGame solo(3, {500, 10, 20},
                   static_cast<std::uint32_t>(simulation_seed(123, id)));
    solo.begin_hand();
    finish(solo, p);
    check(batch.game(id).last_payouts() == solo.last_payouts(),
          "batch result differs from single game");
    check(!batch.game(id).hand_in_progress(), "batch game did not finish");
  }
  batch.reset(3, 9);
  check(batch.collect().size() == 1 && batch.game_ids()[0] == 3,
        "reset wrong batch row");
}
void test_parallel() {
  SimulationOptions options;
  options.episodes = 200;
  options.hands_per_episode = 3;
  options.players = 6;
  options.config = {250, 5, 10};
  options.seed = 77;
  std::vector<EpisodeResult> single(200), parallel(200);
  auto factory = [] { return std::make_unique<RandomPolicy>(); };
  auto a = run_simulations(options, factory, single);
  options.workers = 4;
  auto b = run_simulations(options, factory, parallel);
  check(a.hands == b.hands && a.decisions == b.decisions &&
            a.checksum == b.checksum,
        "parallel scheduling changed seeded results");
  for (std::size_t i = 0; i < single.size(); ++i) {
    check(single[i].final_stacks == parallel[i].final_stacks &&
              single[i].hands == parallel[i].hands,
          "episode differs by worker count");
  }
  // Chunking an experiment must retain exactly the same episode IDs/results.
  options.episodes = 100;
  auto first = run_simulations(options, factory);
  options.first_episode = 100;
  auto second = run_simulations(options, factory);
  check(first.checksum + second.checksum == a.checksum &&
            first.hands + second.hands == a.hands,
        "chunking changed the experiment");
  struct Invalid : DecisionProvider {
    PlayerDecision choose_action(const PlayerView &) override {
      return {ActionType::RaiseTo, -1};
    }
  };
  rejects([&] {
    run_simulations(options, [] { return std::make_unique<Invalid>(); });
  });
  rejects([&] {
    run_simulations(options,
                    [] { return std::unique_ptr<DecisionProvider>{}; });
  });
}
void test_sampling() {
  std::array<Card, 2> known{Card{Face::Ace, Suit::Spades},
                            Card{Face::King, Suit::Hearts}};
  CardSampler a(known), b(known);
  std::mt19937 ra(7), rb(7);
  std::array<Card, 7> x{}, y{};
  for (int repeat = 0; repeat < 1000; ++repeat) {
    a.sample(x, ra);
    b.sample(y, rb);
    check(x == y, "sampler is not reproducible");
    std::array<bool, 52> seen{};
    for (auto card : x) {
      check(card != known[0] && card != known[1], "sampled a known card");
      int id =
          static_cast<int>(card.suit) * 13 + static_cast<int>(card.face) - 2;
      check(!seen[id], "sampled a duplicate card");
      seen[id] = true;
    }
  }
  std::array<Card, 51> too_many{};
  rejects([&] { a.sample(too_many, ra); });
  known[1] = known[0];
  rejects([&] { a.reset(known); });
}
void test_betting_edges() {
  // Preserve the current documented short-all-in rule: prior actors must call
  // again but cannot raise until a full raise. These cases use real chip moves.
  Table t{};
  table_init(t, 3, 500);
  PotManager pots;
  BettingRoundState state{};
  betting_state_reset(state, 0, 100);
  execute_decision(t, pots, state, 0, {ActionType::RaiseTo, 100});
  execute_decision(t, pots, state, 1, {ActionType::Call, 0});
  t.players[2].cash = 130;
  execute_decision(t, pots, state, 2, {ActionType::AllIn, 0});
  auto legal = get_legal_actions(t, pots, state, 0);
  check(legal.can_call && !legal.can_raise && legal.amount_to_call == 30,
        "short raise reopened incorrectly");
  // Two successive short raises still retain acted flags in this engine's
  // simplified reopening model. Record this contract explicitly for research.
  table_init(t, 4, 500);
  pots.reset_for_new_hand();
  betting_state_reset(state, 0, 100);
  execute_decision(t, pots, state, 0, {ActionType::RaiseTo, 100});
  execute_decision(t, pots, state, 1, {ActionType::Call, 0});
  t.players[2].cash = 150;
  t.players[3].cash = 200;
  execute_decision(t, pots, state, 2, {ActionType::AllIn, 0});
  execute_decision(t, pots, state, 3, {ActionType::AllIn, 0});
  legal = get_legal_actions(t, pots, state, 0);
  check(legal.can_call && !legal.can_raise && legal.amount_to_call == 100,
        "multiple short raises changed the documented reopening model");
  // Exact tied scores split odd chips clockwise after the dealer, including
  // the losing player's contributed chips in the common pot.
  table_init(t, 3, 100);
  pots.reset_for_new_hand();
  for (int id = 0; id < 3; ++id)
    pots.contribute(t, id, 1);
  pots.build_pots(t);
  std::array<HandScore, MAX_PLAYERS> scores{};
  scores[0] = scores[1] = 100;
  scores[2] = 50;
  const auto won = pots.distribute_pots(t, scores);
  check(won[0] == 1 && won[1] == 2 && won[2] == 0, "odd-chip split incorrect");
  // Unequal stacks create main/side pots, while an unmatched top contribution
  // returns to its contributor; the final reward still conserves chips.
  PokerGame g(3, {500, 10, 20}, 99);
  std::array<int, 3> stacks{40, 90, 200};
  g.reset_episode(stacks, 0, 99);
  g.begin_hand();
  while (g.advance_to_decision()) {
    const auto v = g.current_view();
    g.apply_action(
        {v.legal_actions.can_all_in ? ActionType::AllIn : ActionType::Call, 0});
  }
  int cash = 0;
  for (int id = 0; id < 3; ++id)
    cash += g.table().players[id].cash;
  // Here the largest stack acts last and cannot raise against all-in players,
  // so the extra 110 stays in its stack instead of appearing as a payout.
  check(cash == 330 && g.table().players[2].cash >= 110,
        "uncalled contribution lost");
  table_init(t, 3, 500);
  pots.reset_for_new_hand();
  pots.contribute(t, 0, 40);
  pots.contribute(t, 1, 90);
  pots.contribute(t, 2, 200);
  pots.build_pots(t);
  scores[0] = 300;
  scores[1] = 200;
  scores[2] = 100;
  const auto side_won = pots.distribute_pots(t, scores);
  check(side_won[0] == 120 && side_won[1] == 100 && side_won[2] == 110,
        "unmatched top contribution was not returned");
  // Blinds larger than stacks still finish and conserve the actual chip supply.
  PokerGame tiny(2, {5, 10, 20}, 9);
  tiny.begin_hand();
  check(!tiny.advance_to_decision() && !tiny.hand_in_progress(),
        "short blind runout requested a decision");
}
// Many seeded legal action sequences cover raises, folds, side pots and all-in
// transitions while checking cards, score compatibility and conservation.
struct Audit : GameObserver {
  int supply = 0;
  void on_player_action(int, const PlayerDecision &, const Table &t,
                        const PotManager &p,
                        const BettingRoundState &) override {
    int total = p.total_pot_amount();
    for (int id = 0; id < t.num_players; ++id) {
      check(t.players[id].cash >= 0, "negative cash");
      total += t.players[id].cash;
    }
    check(total == supply, "action lost chips");
  }
  void on_showdown(const Table &t, const PotManager &,
                   const std::array<HandScore, MAX_PLAYERS> &scores) override {
    std::array<bool, 52> seen{};
    auto add = [&](Card c) {
      int id = static_cast<int>(c.suit) * 13 + static_cast<int>(c.face) - 2;
      check(!seen[id], "duplicate dealt card");
      seen[id] = true;
    };
    for (int id = 0; id < t.num_players; ++id) {
      for (int c = 0; c < t.players[id].cards_in_hand; ++c)
        add(t.players[id].hand[c]);
      if (t.players[id].in_hand)
        check(scores[id] == evaluate_player_hand(t, id),
              "fast/reference score mismatch");
    }
    for (int c = 0; c < t.community_card_count; ++c)
      add(t.community_cards[c]);
  }
  void on_hand_finished(const Table &t,
                        const std::array<int, MAX_PLAYERS> &) override {
    int total = 0;
    for (int id = 0; id < t.num_players; ++id)
      total += t.players[id].cash;
    check(total == supply, "payout lost chips");
  }
};
void test_random_games() {
  for (int seats : {2, 3, 6, 8}) {
    PokerGame g(seats, {500, 10, 20});
    RandomPolicy p;
    Audit audit;
    audit.supply = seats * 500;
    g.set_observer(&audit);
    for (int seat = 0; seat < seats; ++seat)
      g.set_decision_provider(seat, p);
    for (int episode = 0; episode < 250; ++episode) {
      g.reset(episode);
      p.reset_for_episode(episode);
      g.run_match(5);
    }
  }
}
} // namespace
int main() {
  try {
    test_steps_and_snapshots();
    test_batches();
    test_parallel();
    test_sampling();
    test_betting_edges();
    test_random_games();
    std::cout << "All research simulation tests passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
