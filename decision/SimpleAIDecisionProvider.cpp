// Checks for free, calls inexpensive bets, and otherwise folds; it does not
// assess hand strength.

// Include the class declaration whose choose_action function is defined below.
#include "decision/SimpleAIDecisionProvider.hpp"
#include <stdexcept> // std::out_of_range reports an invalid view player ID.

namespace poker {

PlayerDecision SimpleAIDecisionProvider::choose_action(const PlayerView &view) {
  // The selected player's ID is used to read their stack. Reject an invalid
  // view instead of reading outside the array or choosing for an unused seat.
  if (view.num_players < 1 || view.num_players > MAX_PLAYERS ||
      view.player_id < 0 || view.player_id >= view.num_players) {
    throw std::out_of_range("AI view does not identify an active seat");
  }

  // Prefer a free check, regardless of the cards or the size of the stack.
  if (view.legal_actions.can_check) {
    return {ActionType::Check, 0};
  }

  // Call only when it costs at most 25% of the chips still available.
  // Integer division avoids floating point and rounds the chip limit down:
  // a stack of 101 permits a call of 25, but not 26.
  const int stack = view.player_stacks[view.player_id];
  if (view.legal_actions.can_call &&
      view.legal_actions.amount_to_call <= stack / 4) {
    return {ActionType::Call, 0};
  }

  // Expensive calls are declined. The game should ask for a decision only
  // when this player needs a turn, so folding is available in this situation.
  // raise_to is zero because only RaiseTo decisions use that field.
  return {ActionType::Fold, 0};
}

} // namespace poker
