// Owns a fixed 52-card deck and tracks which cards have been dealt or burned.

#ifndef POKER_DECK_HPP
#define POKER_DECK_HPP

#include "card/Card.hpp"
#include "config/PokerConfig.hpp"
#include <array>
#include <random>

namespace poker {

/**
 * @brief Represents a standard 52-card poker deck.
 *
 * The deck stores all cards in a fixed-size array.
 * Cards are not removed when dealt; instead, next_card_index_ tracks
 * the next undealt card.
 */
class Deck {
public:
  /**
   * @brief Creates a complete 52-card deck.
   */
  Deck();

  /**
   * @brief Restores the deck to its complete ordered state.
   *
   * Resets next_card_index_ so all 52 cards are available again, make a brand
   * new deck.
   */
  void reset();

  /**
   * @brief Shuffles the entire deck.
   * set the next_card_index_ to 0
   *
   * @param random_generator Random-number generator used by std::shuffle.
   */
  void shuffle(std::mt19937 &random_generator);

  /**
   * @brief Deals the next available card from the deck.
   *
   * The card is not physically removed from cards_. Instead,
   * next_card_index_ is advanced.
   *
   * @return The next card in the deck.
   *
   * @throws std::runtime_error if no cards remain.
   */
  Card deal_card();

  /**
   * @brief Burns the next available card.
   *
   * Used before dealing the flop, turn, and river.
   * The burned card is skipped by advancing next_card_index_.
   *
   * @throws std::runtime_error if no cards remain.
   */
  void burn_card();

  /**
   * @brief Returns the number of undealt cards remaining.
   *
   * @return Number of cards still available in the deck.
   */
  int cards_remaining() const;

  /**
   * @brief Returns the number of cards already dealt or burned.
   *
   * @return Number of cards consumed from the deck.
   */
  int cards_dealt() const;

private:
  // Stores all 52 cards. Cards stay in this array even after being dealt.
  std::array<Card, DECK_SIZE> cards_{};

  // Index of the next card that will be dealt or burned.
  int next_card_index_ = 0;
};

} // namespace poker

#endif