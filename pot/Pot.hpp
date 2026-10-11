// One main/side-pot layer and the seats allowed to win that layer.

#include "config/PokerConfig.hpp"
#include <array>

namespace poker {

// One contribution layer, either the main pot or a side pot. A hand can have
// several layers because all-in players may have paid different total amounts.
struct Pot {
  // Total chips in this layer, including contributions from folded players.
  int amount = 0;
  // eligible_players[2] is true if player 2 contributed to this layer and is
  // still in the hand. The array stores permission to win, not hand strength.
  std::array<bool, MAX_PLAYERS> eligible_players{};
};

} // namespace poker
