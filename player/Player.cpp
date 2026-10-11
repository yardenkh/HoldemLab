// Resets hand-specific player state and stores newly dealt private cards.

#include "player/Player.hpp"
#include <stdexcept>

namespace poker {

void Player::reset_for_new_hand() {
  // Clear only hand-specific state. Keep cash and human/AI type across hands.
  // Old card entries need not be erased: cards_in_hand = 0 makes them unused.
  cards_in_hand = 0;
  all_in = false;
  in_hand = cash > 0;
}

void Player::receive_card(const Card &card) {
  // Store the card at the first unused position, then extend the valid count.
  if (cards_in_hand >= HOLE_CARDS_PER_PLAYER) {
    throw std::runtime_error("player already has 2 cards or more");
  }

  hand[cards_in_hand] = card;
  cards_in_hand++;
}

// Interpret this at a hand boundary: an all-in player can still win chips
// before the hand ends, even though their cash is currently zero.
bool Player::is_eliminated() const { return cash <= 0; }

} // namespace poker
