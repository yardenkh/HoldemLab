// Reference poker hand scoring and readable score descriptions used by tests
// and tools.

#ifndef POKER_HAND_EVALUATOR_HPP
#define POKER_HAND_EVALUATOR_HPP

#include "card/Card.hpp"
#include <array>
#include <cstdint>
#include <string>

namespace poker {

// A declaration is enough here; the implementation reads the full Table.
struct Table;

/**
 * @brief Represents the strength category of a 5-card poker hand.
 *
 * The enum values are ordered from weakest to strongest so their numeric
 * values can be used directly when constructing a HandScore.
 */
enum class HandRank {
  HighCard,
  OnePair,
  TwoPair,
  ThreeOfAKind,
  Straight,
  Flush,
  FullHouse,
  FourOfAKind,
  StraightFlush
};

/**
 * @brief Encoded value used to compare poker hands.
 *
 * A larger HandScore always represents a stronger hand.
 */
using HandScore = std::uint32_t;

/**
 * @brief Evaluates exactly five cards and returns their poker hand score.
 *
 * The returned score contains both the hand rank and all required
 * tie-breaking values.
 *
 * @param cards The five cards to evaluate.
 * @return Encoded score representing the strength of the hand.
 */
HandScore evaluate_five_cards(const std::array<Card, 5> &cards);

/**
 * @brief Extracts the hand rank from an encoded HandScore.
 *
 * @param score The encoded hand score.
 * @return The corresponding HandRank.
 */
HandRank hand_rank_from_score(HandScore score);

/**
 * @brief Converts a hand rank to a readable string.
 *
 * @param rank The hand rank to convert.
 * @return A readable name such as "Two Pair" or "Straight Flush".
 */
std::string hand_rank_to_string(HandRank rank);

/**
 * @brief Converts a complete HandScore to a readable description.
 *
 * The returned string may include both the hand rank and its relevant
 * tie-breaking card values.
 *
 * @param score The encoded hand score.
 * @return A readable description of the hand.
 */
std::string hand_score_to_string(HandScore score);

/**
 * @brief Evaluates seven cards and returns the score of the best possible
 *        5-card poker hand.
 *
 * The function checks every 5-card combination that can be formed from the
 * seven cards and returns the highest HandScore.
 *
 * @param cards The seven cards to evaluate.
 * @return The score of the strongest 5-card hand that can be formed.
 */
HandScore evaluate_seven_cards(const std::array<Card, 7> &cards);
// Evaluate a player's two private cards together with a complete five-card
// board. Invalid seats or incomplete hands throw instead of using undealt
// cards.
HandScore evaluate_player_hand(const Table &table, int player_id);
} // namespace poker

#endif
