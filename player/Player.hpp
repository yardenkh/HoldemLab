// State of one seat: private cards, remaining chips, controller type, and hand
// status.

#ifndef POKER_PLAYER_HPP
#define POKER_PLAYER_HPP

#include "card/Card.hpp"
#include "config/PokerConfig.hpp"
#include <array>

namespace poker {

/**
 * @brief Identifies whether a player is controlled by a human or by AI logic.
 */
enum class PlayerType { Human, AI };

/**
 * @brief Stores the state of one player at the poker table.
 *
 * The player's chip stack persists between hands, while fields such as
 * hand, cards_in_hand, in_hand, and all_in are updated for each hand.
 */
struct Player {
  // The player's two private hole cards.
  std::array<Card, HOLE_CARDS_PER_PLAYER> hand{};

  // Number of valid cards currently stored in hand.
  int cards_in_hand = 0;

  // Number of chips currently owned by the player.
  int cash = 0;

  // Determines which type of decision provider controls this player.
  PlayerType type = PlayerType::AI;

  // True while the player is still participating in the current hand.
  // Becomes false after folding.
  bool in_hand = false;

  // True when the player has committed all remaining chips.
  // An all-in player is still considered to be in the hand.
  bool all_in = false;

  /**
   * @brief Resets all state that belongs only to the current hand.
   *
   * The player's cash and player type remain unchanged.
   */
  void reset_for_new_hand();

  /**
   * @brief Gives one hole card to the player.
   *
   * @param card The card to add to the player's hand.
   *
   * @throws std::runtime_error if the player already has two hole cards.
   */
  void receive_card(const Card &card);

  /**
   * @brief Checks whether the player has been eliminated from the game.
   *
   * @return True if the player has no chips remaining.
   */
  bool is_eliminated() const;
};
} // namespace poker

#endif