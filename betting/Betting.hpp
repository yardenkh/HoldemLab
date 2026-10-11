// Betting action types, shared round state, legality, execution, and
// turn-search helpers.

#ifndef POKER_BETTING_HPP
#define POKER_BETTING_HPP

// MAX_PLAYERS sets the size of the per-player betting-state array.
#include "config/PokerConfig.hpp"

// Betting functions use PotManager to track contributions and Table to
// access the players and their current state.
#include "pot/PotManager.hpp"
#include "table/Table.hpp"

#include <array>  // std::array stores whether each player has acted.
#include <string> // std::string is returned by poker_stage_to_string().

namespace poker {

// Which part of the hand is being played. PreFlop is before community cards;
// Flop, Turn, and River reveal 3, 4, and 5 community cards respectively.
// Showdown is when the remaining players' hands are compared.
enum class PokerStage { PreFlop, Flop, Turn, River, Showdown };

// Convert a stage to a readable name for the UI, such as "Flop".
std::string poker_stage_to_string(PokerStage stage);

// The action a player chooses:
// Fold: leave this hand; chips already contributed stay in the pot.
// Check: stay in the hand without adding chips, when nothing is owed.
// Call: match the current bet, or put in the remaining cash if it is not
// enough. RaiseTo: increase the player's total contribution for THIS betting
// round. AllIn: contribute all the player's remaining cash.
enum class ActionType { Fold, Check, Call, RaiseTo, AllIn };

// A human or AI's chosen action. This describes a request; it does not move
// chips by itself. validate_decision checks it before execute_decision applies
// it.
struct PlayerDecision {
  // Default to folding until a different action is selected.
  ActionType action = ActionType::Fold;

  // Used only when action is RaiseTo. This is a total for the current round,
  // not the number of extra chips to add. For example, if the player already
  // contributed 50, raise_to = 200 means adding another 150 chips.
  int raise_to = 0;
};

// The actions currently allowed for ONE player, plus the amounts needed by
// the UI or AI. A true can_* field means that action is available.
// The false/zero defaults describe an empty result before it is calculated.
struct LegalActions {
  // Whether this player may fold and leave the hand.
  bool can_fold = false;
  // Whether this player may check without contributing more chips.
  bool can_check = false;
  // Whether this player may call the current bet.
  bool can_call = false;
  // Whether this player may make a full raise to a larger round total.
  bool can_raise = false;
  // Whether this player may commit all their remaining cash.
  bool can_all_in = false;

  // Additional chips needed to match the current bet. A player with less
  // cash can only contribute their remaining cash as a partial all-in call.
  int amount_to_call = 0;
  // Smallest total for a full RaiseTo action in this round. A short all-in
  // can be below this amount and is handled separately by the AllIn action.
  int min_raise_to = 0;
  // Largest round total this player can afford:
  // their contribution so far in this round + their remaining cash.
  int max_raise_to = 0;
};

// Shared betting information for the current round, such as the flop round.
// PotManager records contributions; this struct records the betting target
// and which players have already responded to it.
struct BettingRoundState {
  // Round contribution players must match to call. This is a per-player
  // target, not the total pot amount and not a contribution across the whole
  // hand.
  int current_bet = 0;
  // Minimum increase above current_bet needed for a full raise.
  // For example, current_bet = 100 and minimum_raise_size = 100 means
  // the next full raise must reach at least 200 in this round.
  int minimum_raise_size = 0;

  // acted_since_last_full_raise[player_id] says whether that player has
  // acted since the last full raise. A full raise reopens betting for others;
  // a short all-in raise does not reopen it in the same way.
  // {} starts all entries at false. Acting once alone does not finish a
  // player's round: they may still owe chips after another player's raise.
  std::array<bool, MAX_PLAYERS> acted_since_last_full_raise{};
};

// Start/reset a betting round with the supplied target and minimum raise size,
// and mark all players as not yet having acted. This resets betting state;
// it does not clear PotManager's contribution records or change player cash.
void betting_state_reset(BettingRoundState &state, int current_bet,
                         int minimum_raise_size);

// Return how many more chips player_id needs to match current_bet:
// current_bet - that player's contribution in THIS round, never below zero.
// For example, a target of 100 and a round contribution of 40 means owing 60.
int amount_to_call(const PotManager &pot_manager,
                   const BettingRoundState &betting_state, int player_id);

// Calculate the allowed actions and chip limits for player_id using the
// player's cash, hand status, contributions, and the current betting state.
// const references mean this calculation does not change those objects.
LegalActions get_legal_actions(const Table &table,
                               const PotManager &pot_manager,
                               const BettingRoundState &betting_state,
                               int player_id);

// Return true if the requested decision is allowed for this player now.
// Check both the action and, for RaiseTo, its requested round total.
// This checks the request without applying it or moving chips.
bool validate_decision(const Table &table, const PotManager &pot_manager,
                       const BettingRoundState &betting_state, int player_id,
                       const PlayerDecision &decision);

// Validate a choice against legal actions already computed for the SAME state.
// This lets the game reuse the actions supplied in the current PlayerView.
bool validate_decision(const LegalActions &actions,
                       const PlayerDecision &decision);

// Apply a legal decision: update the player's hand/all-in status, contribute
// chips through PotManager as needed, and update the round's betting state.
// An illegal decision throws std::invalid_argument without changing state.
// These references are not const because applying an action changes state.
void execute_decision(Table &table, PotManager &pot_manager,
                      BettingRoundState &betting_state, int player_id,
                      const PlayerDecision &decision);

// Check whether player_id still needs a turn in this betting round.
// Folded and all-in players cannot act. Other players may need to act because
// they have not responded yet or their round contribution is below current_bet.
// If every opponent is all-in, only an outstanding call/fold decision is
// needed.
bool player_needs_action(const Table &table, const PotManager &pot_manager,
                         const BettingRoundState &betting_state, int player_id);

// Return true when no further betting actions are needed in this round.
// Normally, players who can act must have responded and matched current_bet.
// If only one player can still act, the round ends once they owe nothing;
// they do not need to place another bet against opponents who are all-in.
bool betting_round_complete(const Table &table, const PotManager &pot_manager,
                            const BettingRoundState &betting_state);

// Search seat order AFTER after_player_id for the next player who needs a turn.
// Wrap from the last seat to seat 0. Return -1 if nobody needs another action.
// Pass after_player_id = -1 to begin the search at seat 0.
int next_player_needing_action(const Table &table,
                               const PotManager &pot_manager,
                               const BettingRoundState &betting_state,
                               int after_player_id);

} // namespace poker

#endif // POKER_BETTING_HPP
