// Calculates reference scores directly from ranks and suits; validates lookup
// results in tests.

#include "evaluation/HandEvaluator.hpp"
#include "table/Table.hpp" // Read hole cards and the board for player evaluation.
#include <algorithm>       // Copy cards into the seven-card evaluation input.
#include <stdexcept> // Reject invalid seats and incomplete showdown cards.

namespace poker {

namespace {

/**
 * @brief Packs a hand rank and its tie-breaking values into one HandScore.
 *
 * Higher ranks and higher tie-breaking values produce larger scores.
 */
HandScore make_hand_score(HandRank rank, int value1 = 0, int value2 = 0,
                          int value3 = 0, int value4 = 0, int value5 = 0) {
  // Each rank/kicker uses four bits. The hand category occupies the highest
  // field, so normal integer comparison checks category before all kickers.
  // For example, any pair beats any high card, then larger pair ranks break
  // ties.
  return (static_cast<HandScore>(rank) << 20) |
         (static_cast<HandScore>(value1) << 16) |
         (static_cast<HandScore>(value2) << 12) |
         (static_cast<HandScore>(value3) << 8) |
         (static_cast<HandScore>(value4) << 4) | static_cast<HandScore>(value5);
}

} // namespace

HandScore evaluate_five_cards(const std::array<Card, 5> &cards) {
  // Count how many times each face appears.
  // Card faces use values 2-14, so indices 0 and 1 are unused.
  std::array<int, 15> face_count{};

  for (const Card &card : cards) {
    face_count[static_cast<int>(card.face)]++;
  }

  // Check whether all cards have the same suit.
  bool is_flush = true;

  for (int i = 1; i < 5; i++) {
    if (cards[i].suit != cards[0].suit) {
      is_flush = false;
      break;
    }
  }

  // Find the highest card of a straight.
  // A value of 0 means that the hand is not a straight.
  int straight_high = 0;

  for (int high = static_cast<int>(Face::Ace);
       high >= static_cast<int>(Face::Six); high--) {
    bool found_straight = true;

    for (int face = high; face > high - 5; face--) {
      if (face_count[face] == 0) {
        found_straight = false;
        break;
      }
    }

    if (found_straight) {
      straight_high = high;
      break;
    }
  }

  // Special case: A 2 3 4 5 is a valid 5-high straight.
  if (straight_high == 0 && face_count[static_cast<int>(Face::Ace)] > 0 &&
      face_count[static_cast<int>(Face::Five)] > 0 &&
      face_count[static_cast<int>(Face::Four)] > 0 &&
      face_count[static_cast<int>(Face::Three)] > 0 &&
      face_count[static_cast<int>(Face::Two)] > 0) {
    straight_high = static_cast<int>(Face::Five);
  }

  int four = 0;
  int three = 0;

  std::array<int, 2> pairs{};
  int pair_count = 0;

  std::array<int, 5> kickers{};
  int kicker_count = 0;

  // Scan from Ace down to Two so pairs and kickers are automatically
  // stored from strongest to weakest.
  for (int face = static_cast<int>(Face::Ace);
       face >= static_cast<int>(Face::Two); face--) {
    if (face_count[face] == 4) {
      four = face;
    } else if (face_count[face] == 3) {
      three = face;
    } else if (face_count[face] == 2) {
      pairs[pair_count++] = face;
    } else if (face_count[face] == 1) {
      kickers[kicker_count++] = face;
    }
  }

  // Straight Flush
  if (straight_high != 0 && is_flush) {
    return make_hand_score(HandRank::StraightFlush, straight_high);
  }

  // Four of a Kind
  if (four != 0) {
    return make_hand_score(HandRank::FourOfAKind, four, kickers[0]);
  }

  // Full House
  if (three != 0 && pair_count == 1) {
    return make_hand_score(HandRank::FullHouse, three, pairs[0]);
  }

  // Flush
  if (is_flush) {
    return make_hand_score(HandRank::Flush, kickers[0], kickers[1], kickers[2],
                           kickers[3], kickers[4]);
  }

  // Straight
  if (straight_high != 0) {
    return make_hand_score(HandRank::Straight, straight_high);
  }

  // Three of a Kind
  if (three != 0) {
    return make_hand_score(HandRank::ThreeOfAKind, three, kickers[0],
                           kickers[1]);
  }

  // Two Pair
  if (pair_count == 2) {
    return make_hand_score(HandRank::TwoPair, pairs[0], pairs[1], kickers[0]);
  }

  // One Pair
  if (pair_count == 1) {
    return make_hand_score(HandRank::OnePair, pairs[0], kickers[0], kickers[1],
                           kickers[2]);
  }

  // High Card
  return make_hand_score(HandRank::HighCard, kickers[0], kickers[1], kickers[2],
                         kickers[3], kickers[4]);
}

HandRank hand_rank_from_score(HandScore score) {
  // Remove the five lower rank/kicker fields to recover the hand category.
  return static_cast<HandRank>((score >> 20) & 0xF);
}

std::string hand_rank_to_string(HandRank rank) {
  switch (rank) {
  case HandRank::HighCard:
    return "High Card";

  case HandRank::OnePair:
    return "One Pair";

  case HandRank::TwoPair:
    return "Two Pair";

  case HandRank::ThreeOfAKind:
    return "Three of a Kind";

  case HandRank::Straight:
    return "Straight";

  case HandRank::Flush:
    return "Flush";

  case HandRank::FullHouse:
    return "Full House";

  case HandRank::FourOfAKind:
    return "Four of a Kind";

  case HandRank::StraightFlush:
    return "Straight Flush";
  }

  return "Unknown Hand";
}

std::string hand_score_to_string(HandScore score) {
  // Extract four-bit fields for display. Their meanings depend on category:
  // a pair uses value1 for its rank; the following fields hold its kickers.
  HandRank rank = hand_rank_from_score(score);

  int value1 = (score >> 16) & 0xF;
  int value2 = (score >> 12) & 0xF;
  int value3 = (score >> 8) & 0xF;
  int value4 = (score >> 4) & 0xF;
  int value5 = score & 0xF;

  auto face_to_text = [](int value) {
    return face_to_string(static_cast<Face>(value));
  };

  switch (rank) {
  case HandRank::HighCard:
    return "High Card: " + face_to_text(value1) + " " + face_to_text(value2) +
           " " + face_to_text(value3) + " " + face_to_text(value4) + " " +
           face_to_text(value5);

  case HandRank::OnePair:
    return "One Pair: " + face_to_text(value1) +
           " | Kickers: " + face_to_text(value2) + " " + face_to_text(value3) +
           " " + face_to_text(value4);

  case HandRank::TwoPair:
    return "Two Pair: " + face_to_text(value1) + " and " +
           face_to_text(value2) + " | Kicker: " + face_to_text(value3);

  case HandRank::ThreeOfAKind:
    return "Three of a Kind: " + face_to_text(value1) +
           " | Kickers: " + face_to_text(value2) + " " + face_to_text(value3);

  case HandRank::Straight:
    return "Straight: " + face_to_text(value1) + "-high";

  case HandRank::Flush:
    return "Flush: " + face_to_text(value1) + " " + face_to_text(value2) + " " +
           face_to_text(value3) + " " + face_to_text(value4) + " " +
           face_to_text(value5);

  case HandRank::FullHouse:
    return "Full House: " + face_to_text(value1) + " over " +
           face_to_text(value2);

  case HandRank::FourOfAKind:
    return "Four of a Kind: " + face_to_text(value1) +
           " | Kicker: " + face_to_text(value2);

  case HandRank::StraightFlush:
    return "Straight Flush: " + face_to_text(value1) + "-high";
  }

  return "Unknown Hand";
}

HandScore evaluate_seven_cards(const std::array<Card, 7> &cards) {
  // Reference implementation for tests: omit every possible pair of cards
  // and score the five that remain. There are 7*6/2 = 21 such combinations.
  // The simulation uses FastHandEvaluator instead to avoid this repeated work.
  std::array<Card, 5> cards_to_evaluate;

  HandScore max_score = 0;

  for (int i = 0; i < 7; i++) {
    for (int j = i + 1; j < 7; j++) {
      int index = 0;
      // i and j are the two omitted positions, not card ranks or player IDs.
      for (int k = 0; k < 7; k++) {
        if (k == i || k == j)
          continue;

        cards_to_evaluate[index] = cards[k];
        index++;
      }
      HandScore current_score = evaluate_five_cards(cards_to_evaluate);
      if (current_score > max_score)
        max_score = current_score;
    }
  }

  return max_score;
}

HandScore evaluate_player_hand(const Table &table, int player_id) {
  // Reference convenience wrapper. Tests use it to compare the production
  // lookup result against a separately calculated seven-card score.
  // Only a valid seat with two hole cards and a complete board can be
  // evaluated.
  if (table.num_players < 0 || table.num_players > MAX_PLAYERS ||
      player_id < 0 || player_id >= table.num_players) {
    throw std::out_of_range("Hand evaluation ID is not an active seat");
  }
  const Player &player = table.players[player_id];
  if (player.cards_in_hand != HOLE_CARDS_PER_PLAYER ||
      table.community_card_count != MAX_COMMUNITY_CARDS) {
    throw std::invalid_argument(
        "Player evaluation needs two hole cards and five board cards");
  }
  // Keep the seven-card input on the stack and reuse the existing evaluator.
  std::array<Card, 7> cards{};
  std::copy(player.hand.begin(), player.hand.end(), cards.begin());
  std::copy(table.community_cards.begin(), table.community_cards.end(),
            cards.begin() + HOLE_CARDS_PER_PLAYER);
  return evaluate_seven_cards(cards);
}

} // namespace poker
