// Foundational regressions for card formatting, decks, players, evaluators, and
// table helpers.

#include "card/Card.hpp"
#include "deck/Deck.hpp"
#include "evaluation/FastHandEvaluator.hpp"
#include "evaluation/HandEvaluator.hpp"
#include "player/Player.hpp"
#include "table/Table.hpp"

#include <array>
#include <filesystem>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

namespace {

using poker::Card;
using poker::Face;
using poker::HandRank;
using poker::Suit;

int failures = 0;

void check(bool condition, const std::string &message) {
  // Accumulate failures so one run can report several independent problems.
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    failures++;
  }
}

template <typename Function>
void check_throws(Function function, const std::string &message) {
  // Invalid inputs must fail clearly rather than silently changing state.
  try {
    function();
    check(false, message);
  } catch (const std::exception &) {
  }
}

std::array<Card, 5> cards(Card a, Card b, Card c, Card d, Card e) {
  return {a, b, c, d, e};
}

void test_cards() {
  check(poker::face_to_string(Face::Ten) == "10", "ten face formatting");
  check(poker::suit_to_string(Suit::Spades) == "S", "spade formatting");
  check(poker::card_to_string({Face::Ace, Suit::Hearts}) == "AH",
        "card formatting");
  check(Card{Face::Ace, Suit::Hearts} == Card{Face::Ace, Suit::Hearts},
        "equal cards compare equal");
  check(Card{Face::Ace, Suit::Hearts} != Card{Face::King, Suit::Hearts},
        "different cards compare unequal");
}

void test_deck() {
  poker::Deck deck;
  check(deck.cards_remaining() == 52, "new deck has 52 cards");
  check(deck.cards_dealt() == 0, "new deck has dealt zero cards");
  for (int i = 0; i < 52; i++)
    deck.deal_card();
  check(deck.cards_remaining() == 0, "deck is empty after 52 cards");
  check_throws([&] { deck.deal_card(); }, "dealing past deck throws");
  deck.reset();
  check(deck.cards_remaining() == 52, "reset restores deck");
  deck.burn_card();
  check(deck.cards_dealt() == 1, "burn consumes one card");
  std::mt19937 generator(7);
  deck.shuffle(generator);
  check(deck.cards_dealt() == 0, "shuffle resets dealt position");
}

void test_player() {
  poker::Player player;
  player.cash = 100;
  player.all_in = true;
  player.receive_card({Face::Ace, Suit::Spades});
  player.receive_card({Face::King, Suit::Spades});
  check_throws([&] { player.receive_card({Face::Queen, Suit::Spades}); },
               "third hole card throws");
  player.reset_for_new_hand();
  check(player.cards_in_hand == 0 && player.in_hand && !player.all_in,
        "reset restores hand state");
  player.cash = 0;
  player.reset_for_new_hand();
  check(!player.in_hand && player.is_eliminated(),
        "zero-chip player is eliminated");
}

void test_evaluator() {
  using poker::evaluate_five_cards;
  check(poker::hand_rank_from_score(evaluate_five_cards(
            cards({Face::Ace, Suit::Clubs}, {Face::King, Suit::Diamonds},
                  {Face::Nine, Suit::Hearts}, {Face::Seven, Suit::Spades},
                  {Face::Two, Suit::Clubs}))) == HandRank::HighCard,
        "high card evaluation");
  check(poker::hand_rank_from_score(evaluate_five_cards(
            cards({Face::Ace, Suit::Clubs}, {Face::Ace, Suit::Diamonds},
                  {Face::Nine, Suit::Hearts}, {Face::Seven, Suit::Spades},
                  {Face::Two, Suit::Clubs}))) == HandRank::OnePair,
        "one pair evaluation");
  check(poker::hand_rank_from_score(evaluate_five_cards(
            cards({Face::Ace, Suit::Clubs}, {Face::Ace, Suit::Diamonds},
                  {Face::Nine, Suit::Hearts}, {Face::Nine, Suit::Spades},
                  {Face::Two, Suit::Clubs}))) == HandRank::TwoPair,
        "two pair evaluation");
  check(poker::hand_rank_from_score(evaluate_five_cards(
            cards({Face::Ace, Suit::Clubs}, {Face::Ace, Suit::Diamonds},
                  {Face::Ace, Suit::Hearts}, {Face::Nine, Suit::Spades},
                  {Face::Two, Suit::Clubs}))) == HandRank::ThreeOfAKind,
        "three of a kind evaluation");
  check(poker::hand_rank_from_score(evaluate_five_cards(
            cards({Face::Ace, Suit::Clubs}, {Face::King, Suit::Diamonds},
                  {Face::Queen, Suit::Hearts}, {Face::Jack, Suit::Spades},
                  {Face::Ten, Suit::Clubs}))) == HandRank::Straight,
        "straight evaluation");
  check(poker::hand_rank_from_score(evaluate_five_cards(
            cards({Face::Ace, Suit::Hearts}, {Face::Jack, Suit::Hearts},
                  {Face::Nine, Suit::Hearts}, {Face::Seven, Suit::Hearts},
                  {Face::Two, Suit::Hearts}))) == HandRank::Flush,
        "flush evaluation");
  check(poker::hand_rank_from_score(evaluate_five_cards(
            cards({Face::Ace, Suit::Clubs}, {Face::Ace, Suit::Diamonds},
                  {Face::Ace, Suit::Hearts}, {Face::King, Suit::Spades},
                  {Face::King, Suit::Clubs}))) == HandRank::FullHouse,
        "full house evaluation");
  check(poker::hand_rank_from_score(evaluate_five_cards(
            cards({Face::Ace, Suit::Clubs}, {Face::Ace, Suit::Diamonds},
                  {Face::Ace, Suit::Hearts}, {Face::Ace, Suit::Spades},
                  {Face::King, Suit::Clubs}))) == HandRank::FourOfAKind,
        "four of a kind evaluation");
  check(poker::hand_rank_from_score(evaluate_five_cards(
            cards({Face::Ace, Suit::Hearts}, {Face::King, Suit::Hearts},
                  {Face::Queen, Suit::Hearts}, {Face::Jack, Suit::Hearts},
                  {Face::Ten, Suit::Hearts}))) == HandRank::StraightFlush,
        "straight flush evaluation");
  check(poker::hand_rank_to_string(HandRank::FullHouse) == "Full House",
        "hand rank formatting");
  check(poker::hand_score_to_string(evaluate_five_cards(
            cards({Face::Ace, Suit::Clubs}, {Face::Ace, Suit::Diamonds},
                  {Face::Nine, Suit::Hearts}, {Face::Seven, Suit::Spades},
                  {Face::Two, Suit::Clubs}))) == "One Pair: A | Kickers: 9 7 2",
        "hand score formatting");

  std::array<Card, 7> seven = {
      Card{Face::Two, Suit::Clubs},   Card{Face::Three, Suit::Diamonds},
      Card{Face::Four, Suit::Hearts}, Card{Face::Five, Suit::Spades},
      Card{Face::Six, Suit::Clubs},   Card{Face::Ace, Suit::Diamonds},
      Card{Face::King, Suit::Hearts}};
  check(poker::hand_rank_from_score(poker::evaluate_seven_cards(seven)) ==
            HandRank::Straight,
        "seven-card evaluation selects best hand");
}

void test_fast_evaluator_matches_reference() {
  // Convert lookup IDs to packed scores before comparing with the reference.
  const std::filesystem::path table_path =
      std::filesystem::path("hand_tables.bin");
  check(std::filesystem::exists(table_path), "lookup table is present");
  poker::FastHandEvaluator evaluator(table_path);

  std::mt19937 rng(123456);
  for (int i = 0; i < 10'000; ++i) {
    std::array<Card, 7> hand{};
    std::array<bool, 52> used{};
    for (int j = 0; j < 7; ++j) {
      int index = static_cast<int>(rng() % 52);
      while (used[index]) {
        index = static_cast<int>(rng() % 52);
      }
      used[index] = true;

      const int rank = index % 13;
      const int suit = index / 13;
      hand[j] = Card{static_cast<Face>(rank + 2), static_cast<Suit>(suit)};
    }

    const auto reference = poker::evaluate_seven_cards(hand);
    const auto fast = evaluator.evaluate(hand);
    const auto packed = evaluator.packed_score(fast);
    check(packed == reference,
          "fast dense score matches brute-force seven-card evaluator");
  }
}

void test_fast_category_winners() {
  poker::FastHandEvaluator evaluator(std::filesystem::path("hand_tables.bin"));

  const std::array<Card, 7> straight_flush = {
      Card{Face::Two, Suit::Hearts},  Card{Face::Three, Suit::Hearts},
      Card{Face::Four, Suit::Hearts}, Card{Face::Five, Suit::Hearts},
      Card{Face::Six, Suit::Hearts},  Card{Face::Jack, Suit::Spades},
      Card{Face::King, Suit::Clubs}};

  const std::array<Card, 7> plain_straight = {
      Card{Face::Seven, Suit::Spades}, Card{Face::Eight, Suit::Clubs},
      Card{Face::Nine, Suit::Hearts},  Card{Face::Ten, Suit::Diamonds},
      Card{Face::Jack, Suit::Spades},  Card{Face::King, Suit::Clubs},
      Card{Face::Five, Suit::Hearts}};

  check(evaluator.evaluate(straight_flush) > evaluator.evaluate(plain_straight),
        "straight flush beats an ordinary straight");

  const std::array<Card, 7> full_house = {
      Card{Face::King, Suit::Clubs},   Card{Face::King, Suit::Hearts},
      Card{Face::King, Suit::Spades},  Card{Face::Nine, Suit::Clubs},
      Card{Face::Nine, Suit::Hearts},  Card{Face::Two, Suit::Spades},
      Card{Face::Five, Suit::Diamonds}};
  const std::array<Card, 7> flush = {
      Card{Face::Ace, Suit::Hearts},    Card{Face::Queen, Suit::Hearts},
      Card{Face::Nine, Suit::Hearts},   Card{Face::Four, Suit::Hearts},
      Card{Face::Three, Suit::Hearts},  Card{Face::Two, Suit::Spades},
      Card{Face::Eight, Suit::Diamonds}};
  check(evaluator.evaluate(full_house) > evaluator.evaluate(flush),
        "full house beats flush");
}

void test_table() {
  // Exercise seat skipping, blind positions, and card/burn counts independently
  // of the game loop, making table-helper regressions easier to locate.
  poker::Table table;
  poker::table_init(table, 4, 1000);
  check(table.num_players == 4 && poker::count_players_with_chips(table) == 4,
        "table initializes players");
  check(poker::count_players_in_hand(table) == 4 &&
            poker::count_players_able_to_act(table) == 4,
        "table initializes active state");
  table.players[1].cash = 0;
  table.players[1].in_hand = false;
  table.players[2].in_hand = false;
  table.players[3].all_in = true;
  check(poker::count_players_with_chips(table) == 3,
        "table counts players with chips");
  check(poker::count_players_in_hand(table) == 2,
        "table counts players in hand");
  check(poker::count_players_able_to_act(table) == 1,
        "table counts players able to act");
  check(poker::find_only_remaining_player(table) == -1,
        "multiple players are not one remaining player");
  table.players[0].in_hand = false;
  check(poker::find_only_remaining_player(table) == 3,
        "finds only remaining player");
  check(poker::next_player_with_chips(table, 3) == 0,
        "next chip player wraps around");
  check(poker::next_player_in_hand(table, 0) == 3,
        "next hand player skips folded players");
  check(poker::next_player_able_to_act(table, 0) == -1,
        "next acting player skips all-in and folded players");

  poker::table_init(table, 4, 1000);
  std::mt19937 generator(11);
  poker::table_prepare_new_hand(table, generator);
  check(table.dealer_id == 1 && table.small_blind_id == 2 &&
            table.big_blind_id == 3,
        "four-player dealer and blinds rotate");
  poker::deal_hole_cards(table);
  check(table.deck.cards_dealt() == 8 && table.players[0].cards_in_hand == 2,
        "hole cards deal two cards to each player");
  poker::deal_flop(table);
  check(table.community_card_count == 3 && table.deck.cards_dealt() == 12,
        "flop deals three cards and burns one");
  poker::deal_turn(table);
  check(table.community_card_count == 4 && table.deck.cards_dealt() == 14,
        "turn deals one card and burns one");
  poker::deal_river(table);
  check(table.community_card_count == 5 && table.deck.cards_dealt() == 16,
        "river deals one card and burns one");

  poker::table_init(table, 2, 1000);
  poker::assign_blinds(table);
  check(table.small_blind_id == table.dealer_id &&
            table.big_blind_id != table.dealer_id,
        "heads-up blinds use dealer as small blind");
}

} // namespace

int main() {
  test_cards();
  test_deck();
  test_player();
  test_evaluator();
  test_fast_evaluator_matches_reference();
  test_fast_category_winners();
  test_table();

  if (failures != 0) {
    std::cerr << failures << " test checks failed\n";
    return 1;
  }
  std::cout << "All poker tests passed\n";
  return 0;
}
namespace poker {} // namespace poker
