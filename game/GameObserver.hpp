// Optional trusted event listener for displays, debugging, and research result
// collection.

#ifndef POKER_GAME_OBSERVER_HPP
#define POKER_GAME_OBSERVER_HPP

#include "betting/Betting.hpp" // Event callbacks describe betting and table state.

namespace poker {

// Optional listener for game events. Override only the callbacks you need.
// These callbacks receive the full table, so observers are trusted game tools;
// player strategies receive the restricted PlayerView instead.
class GameObserver {
public:
  // Support destruction through a pointer to this interface.
  virtual ~GameObserver() = default;
  // Called after preparing and dealing a hand, before posting the blinds.
  virtual void on_hand_started(int /*hand_number*/, const Table &,
                               const PotManager &) {}
  // Called before actions in each stage, including Showdown.
  virtual void on_stage_started(PokerStage, const Table &, const PotManager &) {
  }
  // Called after applying an action; references describe the updated state.
  virtual void on_player_action(int /*player_id*/, const PlayerDecision &,
                                const Table &, const PotManager &,
                                const BettingRoundState &) {}
  // Called with evaluated hands and built pots, before any payouts.
  virtual void on_showdown(const Table &, const PotManager &,
                           const std::array<HandScore, MAX_PLAYERS> &) {}
  // Called after payout; payouts[player_id] is that player's amount won.
  virtual void on_hand_finished(const Table &,
                                const std::array<int, MAX_PLAYERS> &) {}
};

} // namespace poker

#endif // POKER_GAME_OBSERVER_HPP
