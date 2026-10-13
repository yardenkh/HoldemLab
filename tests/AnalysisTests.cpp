// Compare the sampling calculator with known outcomes and exact river equity.
#include "analysis/Analysis.hpp"
#include "evaluation/HandEvaluator.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>

using namespace poker;
namespace {
// Keep checks active in optimized builds, rather than relying on assert().
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
template <class Function> void rejects(Function function) {
  // Run a deliberately invalid request and require the documented exception.
  // A different exception is allowed to escape and fail the test instead of
  // being mistaken for successful input validation.
  bool threw = false;
  try {
    function();
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  check(threw, "Invalid equity request was accepted");
}
PlayerView river_view() {
  // Reusable valid starting observation: player 0 holds As Ks against one
  // unknown opponent on a complete 2h 3d 4c 8h 9c board. No chips or betting
  // state are needed because this calculator measures showdown pot share only.
  PlayerView view;
  view.num_players = 2;
  view.player_id = 0;
  view.players_in_hand[0] = view.players_in_hand[1] = true;
  view.hole_card_count = 2;
  view.hole_cards = {Card{Face::Ace, Suit::Spades},
                     Card{Face::King, Suit::Spades}};
  view.community_card_count = 5;
  view.community_cards = {
      Card{Face::Two, Suit::Hearts}, Card{Face::Three, Suit::Diamonds},
      Card{Face::Four, Suit::Clubs}, Card{Face::Eight, Suit::Hearts},
      Card{Face::Nine, Suit::Clubs}};
  return view;
}
void certain_outcomes(const Analysis &analysis) {
  // Start with cases whose correct equity is known without statistical error.
  // They test pot-share accounting independently of the random draws
  // themselves.
  auto view = river_view();
  view.num_players = MAX_PLAYERS;
  view.players_in_hand.fill(true);
  view.community_cards = {
      Card{Face::Queen, Suit::Spades}, Card{Face::Jack, Suit::Spades},
      Card{Face::Ten, Suit::Spades}, Card{Face::Two, Suit::Hearts},
      Card{Face::Three, Suit::Diamonds}};
  // As Ks in our hand plus Qs Js Ts on the board make a royal flush. Those
  // physical cards cannot be sampled by an opponent, so nobody can beat or tie
  // our hand. Equity must be exactly 1.0 even against seven random opponents.
  check(analysis.calculate_equity(view, 200, 42) == 1.0,
        "Royal flush did not always win");

  // Move all five royal-flush cards onto the board and give our player
  // unrelated private cards. Everyone now has the same unbeatable best
  // five-card hand, so two active players share 1/2 each, three share 1/3 each,
  // and so on.
  view.community_cards = {
      Card{Face::Ace, Suit::Spades}, Card{Face::King, Suit::Spades},
      Card{Face::Queen, Suit::Spades}, Card{Face::Jack, Suit::Spades},
      Card{Face::Ten, Suit::Spades}};
  view.hole_cards = {Card{Face::Two, Suit::Hearts},
                     Card{Face::Three, Suit::Diamonds}};
  view.player_id = 5; // The requesting player need not occupy seat zero.
  for (int players = 2; players <= MAX_PLAYERS; ++players) {
    // Keep eight occupied seats but vary how many are still competing. This
    // catches accidentally using num_players instead of the active-player
    // count.
    view.num_players = MAX_PLAYERS;
    view.players_in_hand.fill(false);
    view.players_in_hand[5] = true;
    // Always include the requesting player at seat 5, then add other seats
    // until exactly 'players' seats compete. Leave all other seats marked as
    // folded.
    int enabled = 1;
    for (int id = 0; id < MAX_PLAYERS && enabled < players; ++id)
      if (id != 5) {
        view.players_in_hand[id] = true;
        ++enabled;
      }
    // Mark every seat all-in deliberately: players_in_hand must still exclude
    // the folded seats, while active all-in players must remain eligible to
    // win. The small tolerance allows rounding when repeatedly adding fractions
    // such as 1/3, which cannot be represented exactly in a binary double.
    view.players_all_in.fill(true);
    check(std::abs(analysis.calculate_equity(view, 47, 19) - 1.0 / players) <
              1e-12,
          "Multiway tie, folded seats, or all-in eligibility is wrong");
  }
  view.players_in_hand.fill(false);
  // Finally leave only our player competing. Equity is 1.0 without needing to
  // draw or evaluate opponents, regardless of the cards in their unused slots.
  view.players_in_hand[5] = true;
  check(analysis.calculate_equity(view, 1) == 1.0,
        "Uncontested equity is not one");
}
void exact_reference(const Analysis &analysis) {
  // This test checks whether random opponent hands produce the correct average,
  // rather than merely returning a plausible number between zero and one.
  const auto view = river_view();
  std::array<Card, 7> ours{view.hole_cards[0], view.hole_cards[1]};
  for (int i = 0; i < 5; ++i)
    ours[i + 2] = view.community_cards[i];
  // Seven cards are known: our two plus the five board cards. The remaining
  // 52 - 7 = 45 cards form 45*44/2 = 990 distinct unordered opponent hands.
  // Enumerate all of them instead of sampling. The slower reference evaluator
  // also makes this check independent of the calculator's lookup scorer.
  std::array<Card, 45> unknown{};
  int count = 0;
  for (int suit = 0; suit < 4; ++suit)
    for (int rank = 2; rank <= 14; ++rank) {
      Card card{static_cast<Face>(rank), static_cast<Suit>(suit)};
      bool known = false;
      // Skip any physical card appearing in our own seven-card combination.
      // This independently constructs the unknown-card pool without
      // CardSampler.
      for (const auto &visible : ours)
        if (card == visible)
          known = true;
      if (!known)
        unknown[count++] = card;
    }
  const auto score = evaluate_seven_cards(ours);
  double total = 0;
  int combinations = 0;
  auto opponent = ours;
  // Both players share the same five board cards. Replace only the first two
  // positions with each opponent pair. Starting b at a+1 avoids duplicate cards
  // and avoids counting the same pair again in the opposite order.
  for (int a = 0; a < count; ++a)
    for (int b = a + 1; b < count; ++b) {
      opponent[0] = unknown[a];
      opponent[1] = unknown[b];
      const auto other_score = evaluate_seven_cards(opponent);
      // Record our exact share against this pair: 1 for a win, 1/2 for a tie,
      // and 0 for a loss. Dividing by 990 gives the exact uniform-hand equity.
      total += score > other_score ? 1.0 : score == other_score ? 0.5 : 0.0;
      ++combinations;
    }
  const auto estimate = analysis.calculate_equity(view, 50000, 123);
  // Monte Carlo is an estimate, so do not require equality with enumeration.
  // Allow one percentage point of error for this fixed 50,000-sample test.
  // This tolerance is a regression check, not a guarantee for every random
  // seed.
  check(std::abs(estimate - total / combinations) < 0.01,
        "Monte Carlo disagrees with exact river equity");
  check(estimate == analysis.calculate_equity(view, 50000, 123),
        "Identical seeds do not reproduce equity");
  // Compare against a freshly built original observation to ensure the caller's
  // private cards and visible board were not replaced by hypothetical samples.
  check(view.hole_cards == river_view().hole_cards &&
            view.community_cards == river_view().community_cards,
        "Equity calculation changed the input observation");
}
void validation_and_missing_board(const Analysis &analysis) {
  // Change one part of an otherwise valid request at a time. Resetting bad
  // before each case ensures a previous mistake cannot mask the next one.
  const auto valid = river_view();
  // Zero would cause division by zero; negative counts cannot describe samples.
  // (void) explicitly discards the result because these calls should throw.
  rejects([&] { (void)analysis.calculate_equity(valid, 0); });
  rejects([&] { (void)analysis.calculate_equity(valid, -1); });
  auto bad = valid;
  // A negative player ID must be rejected before indexing player arrays.
  bad.player_id = -1;
  rejects([&] { (void)analysis.calculate_equity(bad, 1); });
  bad = valid;
  bad.num_players = MAX_PLAYERS + 1;
  // Never read seats beyond the fixed capacity of the observation arrays.
  rejects([&] { (void)analysis.calculate_equity(bad, 1); });
  bad = valid;
  bad.hole_card_count = 1;
  // This version requires a complete two-card private hand for the player.
  rejects([&] { (void)analysis.calculate_equity(bad, 1); });
  bad = valid;
  bad.community_card_count = 6;
  // Hold'em has at most five community cards, even though six were requested.
  rejects([&] { (void)analysis.calculate_equity(bad, 1); });
  bad = valid;
  bad.players_in_hand[0] = false;
  // A folded player cannot ask for their share as an eligible showdown player.
  rejects([&] { (void)analysis.calculate_equity(bad, 1); });
  bad = valid;
  bad.hole_cards[1] = bad.hole_cards[0];
  // The same physical card cannot occupy both private-card positions.
  rejects([&] { (void)analysis.calculate_equity(bad, 1); });
  bad = valid;
  bad.community_cards[0] = bad.hole_cards[0];
  // A private card also cannot appear among the visible community cards.
  rejects([&] { (void)analysis.calculate_equity(bad, 1); });
  bad = valid;
  bad.hole_cards[0].face = static_cast<Face>(255);
  // Casting creates an invalid rank on purpose. Validation must catch it before
  // the unchecked evaluator tries to turn that rank into a lookup position.
  rejects([&] { (void)analysis.calculate_equity(bad, 1); });

  // Test pre-flop, flop, and turn by exposing 0, 3, and 4 board cards. Put
  // invalid ranks into the unused slots on purpose. A correct calculation
  // ignores those slots when excluding known cards and replaces them before
  // scoring a hand. Using all eight players also exercises the largest opponent
  // sample buffer.
  for (int visible : {0, 3, 4}) {
    auto view = valid;
    view.num_players = MAX_PLAYERS;
    view.players_in_hand.fill(true);
    view.community_card_count = visible;
    for (int i = visible; i < 5; ++i)
      view.community_cards[i].face = static_cast<Face>(255);
    const auto equity = analysis.calculate_equity(view, 1000, 71);
    // These cases have no fixed analytical answer in this test. Require a
    // finite valid pot share, rather than guessing a particular equity for each
    // stage.
    check(std::isfinite(equity) && equity >= 0 && equity <= 1,
          "Incomplete board produced invalid equity");
  }
}
// Explicit ranges let tests construct exact showdown outcomes without exposing
// the actual engine's opponent cards. This returns a range owned by one seat.
OpponentRange fixed_range(int id, Card first, Card second) {
  return {id, {{first, second}}};
}
void range_equity(const Analysis &analysis) {
  auto view = river_view();
  // No supplied ranges must preserve the original fast path and seeded result.
  check(analysis.calculate_equity(view, 500, 123, {}) ==
            analysis.calculate_equity(view, 500, 123),
        "Empty ranges changed uniform equity");
  std::array<OpponentRange, 1> ranges{
      fixed_range(1, {Face::Ace, Suit::Hearts}, {Face::Ace, Suit::Clubs})};
  check(analysis.calculate_equity(view, 50, 42, ranges) == 0,
        "Fixed pocket aces should beat our ace-high hand");
  ranges[0].hands.push_back(
      {Card{Face::Queen, Suit::Hearts}, Card{Face::Jack, Suit::Clubs}});
  // Exactly one of these two hands beats us, so the reference equity is 1/2.
  const auto half = analysis.calculate_equity(view, 10000, 71, ranges);
  check(std::abs(half - 0.5) < 0.02,
        "Uniform range choices have incorrect equity");
  // Reversed/repeated entries must not turn a hand into a more likely outcome.
  ranges[0].hands.push_back(ranges[0].hands[0]);
  ranges[0].hands.push_back({ranges[0].hands[0][1], ranges[0].hands[0][0]});
  check(analysis.calculate_equity(view, 10000, 71, ranges) == half,
        "Duplicate range hands changed their weight");
  // Add blocked combinations. They must be discarded before drawing hands.
  ranges[0].hands.push_back(
      {view.hole_cards[0], Card{Face::Five, Suit::Clubs}});
  ranges[0].hands.push_back(
      {view.community_cards[0], Card{Face::Six, Suit::Clubs}});
  check(analysis.calculate_equity(view, 10000, 71, ranges) == half,
        "Known-card conflicts changed the legal range distribution");

  view.num_players = 3;
  view.players_in_hand[2] = true;
  view.hole_cards = {Card{Face::Queen, Suit::Spades},
                     Card{Face::Queen, Suit::Hearts}};
  std::array<OpponentRange, 2> overlapping{
      OpponentRange{
          1,
          {{Card{Face::Ace, Suit::Spades}, Card{Face::Ace, Suit::Hearts}},
           {Card{Face::Seven, Suit::Hearts}, Card{Face::Seven, Suit::Clubs}}}},
      OpponentRange{
          2,
          {{Card{Face::Ace, Suit::Spades}, Card{Face::Ace, Suit::Hearts}},
           {Card{Face::Jack, Suit::Spades}, Card{Face::Jack, Suit::Hearts}}}}};
  // Four proposed assignments exist, but AA/AA is impossible. Of the other
  // three, only 77/JJ lets our QQ win: correct equity is 1/3. A biased sampler
  // keeping opponent 1's choice and renormalizing opponent 2 would yield 1/4.
  const auto third = analysis.calculate_equity(view, 30000, 321, overlapping);
  check(std::abs(third - 1.0 / 3) < 0.015,
        "Overlapping ranges were sampled with bias");
  check(third == analysis.calculate_equity(view, 30000, 321, overlapping),
        "Range equity is not reproducible");
  std::swap(overlapping[0], overlapping[1]);
  check(third == analysis.calculate_equity(view, 30000, 321, overlapping),
        "Range list order changed prepared sampling");

  // One ranged opponent and one random all-in opponent tie on a royal board.
  // Seat identity matters: this range belongs to seat 2, not array position 0.
  view.community_cards = {
      Card{Face::Ace, Suit::Spades}, Card{Face::King, Suit::Spades},
      Card{Face::Queen, Suit::Spades}, Card{Face::Jack, Suit::Spades},
      Card{Face::Ten, Suit::Spades}};
  view.hole_cards = {Card{Face::Two, Suit::Hearts},
                     Card{Face::Three, Suit::Clubs}};
  view.players_all_in[1] = true;
  ranges[0] =
      fixed_range(2, {Face::Four, Suit::Hearts}, {Face::Five, Suit::Clubs});
  check(std::abs(analysis.calculate_equity(view, 100, 13, ranges) - 1.0 / 3) <
            1e-12,
        "Mixed ranged/random opponents or all-in eligibility failed");
}
void invalid_ranges(const Analysis &analysis) {
  const auto view = river_view();
  const auto good =
      fixed_range(1, {Face::Ace, Suit::Hearts}, {Face::Ace, Suit::Clubs});
  std::array<OpponentRange, 1> ranges{good};
  // Reset each invalid case so a prior error cannot accidentally mask it.
  ranges[0].player_id = 0;
  rejects([&] { (void)analysis.calculate_equity(view, 1, 42, ranges); });
  ranges[0] = good;
  ranges[0].player_id = 2;
  rejects([&] { (void)analysis.calculate_equity(view, 1, 42, ranges); });
  ranges[0] = good;
  ranges[0].hands.clear();
  rejects([&] { (void)analysis.calculate_equity(view, 1, 42, ranges); });
  ranges[0] = fixed_range(1, view.hole_cards[0], {Face::Five, Suit::Clubs});
  rejects([&] { (void)analysis.calculate_equity(view, 1, 42, ranges); });
  ranges[0] =
      fixed_range(1, {Face::Ace, Suit::Clubs}, {Face::Ace, Suit::Clubs});
  rejects([&] { (void)analysis.calculate_equity(view, 1, 42, ranges); });
  ranges[0] = good;
  ranges[0].hands[0][0].face = static_cast<Face>(255);
  rejects([&] { (void)analysis.calculate_equity(view, 1, 42, ranges); });
  ranges[0] = good;
  rejects([&] { (void)analysis.calculate_equity(view, 0, 42, ranges); });
  auto folded = view;
  folded.players_in_hand[1] = false;
  rejects([&] { (void)analysis.calculate_equity(folded, 1, 42, ranges); });
  std::array<OpponentRange, 2> duplicate{good, good};
  rejects([&] { (void)analysis.calculate_equity(view, 1, 42, duplicate); });
  auto three = view;
  three.num_players = 3;
  three.players_in_hand[2] = true;
  duplicate[1].player_id = 2;
  // Each range is legal alone, but both demand the very same physical aces.
  // Joint compatibility checking must report impossibility before sampling.
  rejects([&] { (void)analysis.calculate_equity(three, 1, 42, duplicate); });
}

void range_runouts_and_limit(const Analysis &analysis) {
  auto view = river_view();
  view.community_card_count = 3;
  view.community_cards[0] = {Face::Queen, Suit::Spades};
  view.community_cards[1] = {Face::Jack, Suit::Spades};
  view.community_cards[2] = {Face::Ten, Suit::Spades};
  std::array<OpponentRange, 1> fixed{
      fixed_range(1, {Face::Six, Suit::Hearts}, {Face::Seven, Suit::Clubs})};
  // Missing turn/river cards are drawn around both our visible cards and the
  // fixed opponent hand. Our royal flush stays unbeatable on every runout.
  check(analysis.calculate_equity(view, 1000, 61, fixed) == 1.0,
        "Range equity failed to complete the board");

  view = river_view();
  view.num_players = MAX_PLAYERS;
  view.players_in_hand.fill(true);
  const Card common{Face::Ace, Suit::Clubs};
  std::vector<Card> available;
  for (int suit = 0; suit < 4; ++suit)
    for (int rank = 2; rank <= 14; ++rank) {
      Card card{static_cast<Face>(rank), static_cast<Suit>(suit)};
      bool excluded = card == common;
      for (const auto &known : view.hole_cards)
        excluded |= card == known;
      for (const auto &known : view.community_cards)
        excluded |= card == known;
      if (!excluded)
        available.push_back(card);
    }
  std::array<OpponentRange, MAX_PLAYERS - 1> difficult;
  for (int id = 1; id < MAX_PLAYERS; ++id) {
    auto &range = difficult[id - 1];
    range.player_id = id;
    // Most hands demand the same ace. Each range also has one unique pair,
    // proving a legal assignment exists if every opponent takes that pair.
    for (const auto &card : available)
      range.hands.push_back({common, card});
    range.hands.push_back(
        {available[2 * (id - 1)], available[2 * (id - 1) + 1]});
  }
  // Almost every independent proposal collides. The fixed-seed test verifies
  // that exhaustion reports a runtime error instead of hanging or returning
  // an unfinished estimate. This is not the impossible-assignment error above.
  bool stopped = false;
  try {
    (void)analysis.calculate_equity(view, 1, 42, difficult);
  } catch (const std::runtime_error &) {
    stopped = true;
  }
  check(stopped, "Difficult range sampling did not enforce its attempt limit");
}

// Analytical examples check the meaning and units of ICM, not just its bounds.
void icm_known_results() {
  const std::array<int, 3> stacks{5000, 3000, 2000};
  const std::array<std::int64_t, 3> prizes{500, 300, 200};
  const auto values = Analysis::calculate_icm(stacks, prizes);
  // Independently calculated example: player 0 finishes first with probability
  // 1/2 and second with (3/10)*(5/7)+(2/10)*(5/8)=19/56. Third is the
  // remainder. Multiplying those probabilities by 500/300/200 yields these
  // expected prizes.
  check(std::abs(values[0] - 383.9285714285714) < 1e-10 &&
            std::abs(values[1] - 327.5) < 1e-10 &&
            std::abs(values[2] - 288.5714285714286) < 1e-10,
        "ICM differs from independently calculated three-player values");
  check(std::abs(std::accumulate(values.begin(), values.end(), 0.0) - 1000) <
            1e-10,
        "ICM does not preserve the total prize pool");
  for (int id = 3; id < MAX_PLAYERS; ++id)
    check(values[id] == 0, "Unused ICM result positions are not zero");

  // Scaling all chip stacks changes no probabilities: only stack ratios matter.
  const std::array<int, 3> scaled{50000, 30000, 20000};
  const auto larger = Analysis::calculate_icm(scaled, prizes);
  const std::array<int, 3> reversed{2000, 3000, 5000};
  const auto reordered = Analysis::calculate_icm(reversed, prizes);
  for (int id = 0; id < 3; ++id) {
    check(std::abs(larger[id] - values[id]) < 1e-10,
          "Scaling stacks changed ICM value");
    check(std::abs(reordered[2 - id] - values[id]) < 1e-10,
          "ICM output did not follow input player order");
  }
  // Winner-takes-all is linear chip share, whereas multi-place prizes are not.
  const std::array<std::int64_t, 1> winner_only{1000};
  const auto linear = Analysis::calculate_icm(stacks, winner_only);
  check(linear[0] == 500 && linear[1] == 300 && linear[2] == 200,
        "Winner-takes-all does not equal proportional chip value");
  const std::array<std::int64_t, 3> equal_prizes{100, 100, 100};
  const auto guaranteed = Analysis::calculate_icm(stacks, equal_prizes);
  for (int id = 0; id < 3; ++id)
    check(std::abs(guaranteed[id] - 100) < 1e-10,
          "Equal prizes should give everyone the same guaranteed amount");
  const std::array<int, 1> survivor{123};
  check(Analysis::calculate_icm(survivor, winner_only)[0] == 1000,
        "Sole survivor did not receive the full remaining first prize");
  const std::array<int, 2> heads_up{5000, 3000};
  const std::array<std::int64_t, 2> heads_up_prizes{700, 300};
  const auto duel = Analysis::calculate_icm(heads_up, heads_up_prizes);
  // Both players are guaranteed 300; the winner receives another 400.
  // Player 0 wins that increment with probability 5/8: 300 + 400*5/8 = 550.
  check(duel[0] == 550 && duel[1] == 450, "Heads-up ICM values are wrong");
  const auto unpaid = Analysis::calculate_icm(stacks, {});
  check(std::accumulate(unpaid.begin(), unpaid.end(), 0.0) == 0,
        "Empty prizes produced nonzero ICM value");
  const std::array<std::int64_t, 2> zero_prizes{0, 0};
  const auto zero = Analysis::calculate_icm(stacks, zero_prizes);
  check(std::accumulate(zero.begin(), zero.end(), 0.0) == 0,
        "Zero-valued prizes produced money");
}

// Exhaustive enumeration gives a separately implemented reference for all eight
// players, while symmetry and large stacks test numerical edge conditions.
void icm_full_enumeration() {
  // Build an independent reference by explicitly visiting every finishing
  // order, using long-double probabilities and a boolean placed[] array, not
  // subset DP. Eight players have 40,320 orders: manageable for a correctness
  // test.
  const std::array<int, MAX_PLAYERS> stacks{11, 7, 5, 3, 2, 1, 9, 4};
  const std::array<std::int64_t, 5> prizes{1000, 700, 400, 250, 100};
  std::array<bool, MAX_PLAYERS> placed{};
  std::array<long double, MAX_PLAYERS> expected{};
  const auto enumerate = [&](auto &&self, int position,
                             long double path_probability) -> void {
    if (position == MAX_PLAYERS)
      return;
    int remaining = 0;
    for (int id = 0; id < MAX_PLAYERS; ++id)
      if (!placed[id])
        remaining += stacks[id];
    for (int id = 0; id < MAX_PLAYERS; ++id) {
      if (placed[id])
        continue;
      const auto probability = path_probability * stacks[id] / remaining;
      if (position < static_cast<int>(prizes.size()))
        expected[id] += probability * prizes[position];
      placed[id] = true;
      self(self, position + 1, probability);
      placed[id] = false;
    }
  };
  enumerate(enumerate, 0, 1.0L);
  const auto result = Analysis::calculate_icm(stacks, prizes);
  for (int id = 0; id < MAX_PLAYERS; ++id)
    check(std::abs(result[id] - static_cast<double>(expected[id])) < 1e-9,
          "Subset ICM differs from full eight-player enumeration");

  // Equal stacks must each receive the same fraction of the entire prize pool.
  std::array<int, MAX_PLAYERS> equal;
  equal.fill(100);
  const auto symmetric = Analysis::calculate_icm(equal, prizes);
  const double share = 2450.0 / MAX_PLAYERS;
  for (double value : symmetric)
    check(std::abs(value - share) < 1e-10,
          "Equal stacks have unequal ICM value");
  // Valid int stacks can sum beyond INT_MAX. The implementation sums in
  // int64_t.
  const std::array<int, 3> huge{std::numeric_limits<int>::max(),
                                std::numeric_limits<int>::max(),
                                std::numeric_limits<int>::max()};
  const std::array<std::int64_t, 1> prize{900};
  const auto large = Analysis::calculate_icm(huge, prize);
  for (int id = 0; id < 3; ++id)
    check(std::abs(large[id] - 300) < 1e-10, "Large stack sums overflowed ICM");
}

// Invalid counts, survivor stacks, prize ordering and overflow must be rejected
// before the recurrence divides by remaining chips or accesses a prize
// position.
void icm_invalid_inputs() {
  const std::array<int, 2> valid{100, 200};
  const std::array<std::int64_t, 2> prizes{700, 300};
  rejects([&] { (void)Analysis::calculate_icm({}, {}); });
  const std::array<int, MAX_PLAYERS + 1> too_many{1, 1, 1, 1, 1, 1, 1, 1, 1};
  rejects([&] { (void)Analysis::calculate_icm(too_many, {}); });
  const std::array<int, 2> zero{0, 100}, negative{-1, 100};
  rejects([&] { (void)Analysis::calculate_icm(zero, prizes); });
  rejects([&] { (void)Analysis::calculate_icm(negative, prizes); });
  const std::array<std::int64_t, 3> excessive{700, 300, 100};
  rejects([&] { (void)Analysis::calculate_icm(valid, excessive); });
  const std::array<std::int64_t, 2> increasing{300, 700}, below_zero{700, -1};
  rejects([&] { (void)Analysis::calculate_icm(valid, increasing); });
  rejects([&] { (void)Analysis::calculate_icm(valid, below_zero); });
  const std::array<std::int64_t, 2> overflow{
      std::numeric_limits<std::int64_t>::max(), 1};
  rejects([&] { (void)Analysis::calculate_icm(valid, overflow); });
}

} // namespace
int main() {
  try {
    // Reuse one Analysis instance across every case, just as an AI would. The
    // seeded sampling state belongs to each call, not to this shared object.
    const Analysis analysis;
    certain_outcomes(analysis);
    exact_reference(analysis);
    validation_and_missing_board(analysis);
    range_equity(analysis);
    invalid_ranges(analysis);
    range_runouts_and_limit(analysis);
    icm_known_results();
    icm_full_enumeration();
    icm_invalid_inputs();
    std::cout << "Analysis tests passed\n";
  } catch (const std::exception &error) {
    // Report the first failed check and return a failing exit status for CTest.
    std::cerr << error.what() << '\n';
    return 1;
  }
}
