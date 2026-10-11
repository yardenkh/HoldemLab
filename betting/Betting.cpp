// Poker betting rules: determine legal choices, apply payments, and decide who
// acts next.

#include "betting/Betting.hpp"
#include "betting/BettingInternal.hpp" // Trusted application after validation.
#include <algorithm> // std::max keeps the amount owed from becoming negative.
#include <stdexcept> // std::invalid_argument reports an illegal decision.

namespace poker {

std::string poker_stage_to_string(PokerStage stage) {
  switch (stage) {
  case PokerStage::PreFlop:
    return "PreFlop";
    break;

  case PokerStage::Flop:
    return "Flop";
    break;

  case PokerStage::Turn:
    return "Turn";
    break;

  case PokerStage::River:
    return "River";
    break;

  case PokerStage::Showdown:
    return "Showdown";
    break;

  default:
    // Unknown stage values should fail clearly rather than return no string.
    throw std::invalid_argument("Unknown poker stage");
  }
}

void betting_state_reset(BettingRoundState &state, int current_bet,
                         int minimum_raise_size) {
  state.current_bet = current_bet;
  state.minimum_raise_size = minimum_raise_size;
  state.acted_since_last_full_raise.fill(false);
}

int amount_to_call(const PotManager &pot_manager,
                   const BettingRoundState &betting_state, int player_id) {

  int current_bet = betting_state.current_bet;
  int player_contribution = pot_manager.round_contribution(player_id);
  // Return the extra chips needed to match the current bet.
  // If the player already contributed enough, they owe zero.
  return std::max(0, current_bet - player_contribution);
}

LegalActions get_legal_actions(const Table &table,
                               const PotManager &pot_manager,
                               const BettingRoundState &betting_state,
                               int player_id) {
  // Start with no available actions. Return this for an invalid seat or a
  // player who has folded, gone all-in, or has no chips left to act with.
  LegalActions actions{};
  if (player_id < 0 || player_id >= table.num_players ||
      player_id >= MAX_PLAYERS) {
    return actions;
  }
  const Player &player = table.players[player_id];
  if (!player.in_hand || player.all_in || player.cash <= 0) {
    return actions;
  }

  // Raise targets are TOTAL contributions for this round, not extra chips.
  // For example, 50 already paid plus 150 cash means max_raise_to is 200.
  const int contributed = pot_manager.round_contribution(player_id);
  actions.amount_to_call =
      amount_to_call(pot_manager, betting_state, player_id);
  actions.min_raise_to =
      betting_state.current_bet + betting_state.minimum_raise_size;
  actions.max_raise_to = contributed + player.cash;

  // Another player must still be in the hand for betting to make sense.
  // Raising also needs an opponent with chips who can respond to the raise.
  bool has_opponent = false;
  bool opponent_can_act = false;
  for (int other_id = 0; other_id < table.num_players; ++other_id) {
    if (other_id == player_id || !table.players[other_id].in_hand) {
      continue;
    }
    has_opponent = true;
    const Player &opponent = table.players[other_id];
    if (!opponent.all_in && opponent.cash > 0) {
      opponent_can_act = true;
    }
  }

  // If everyone else folded, the hand is won without another betting action.
  // If all remaining opponents are all-in and this player owes nothing,
  // there is also nothing left to decide in this betting round.
  if (!has_opponent || (!opponent_can_act && actions.amount_to_call == 0)) {
    return actions;
  }

  actions.can_fold = true;
  actions.can_check = actions.amount_to_call == 0;
  // A call remains available even when it uses all the player's cash and
  // falls short of the target. execute_decision must limit payment to cash.
  actions.can_call = actions.amount_to_call > 0;

  // Under this project's rules, a short all-in raise does not let someone
  // who already acted raise again. A full raise resets these acted flags.
  const bool raising_is_open =
      !betting_state.acted_since_last_full_raise[player_id];
  actions.can_raise = opponent_can_act && raising_is_open &&
                      actions.max_raise_to >= actions.min_raise_to &&
                      actions.max_raise_to > betting_state.current_bet;

  // An all-in can be a full/partial call or a raise. Calls are allowed even
  // when raising is closed. An all-in above the target is a raise, so it
  // requires open raising rights and an opponent who can still respond.
  // Unlike can_raise, this also allows a raise smaller than the full minimum.
  const bool all_in_is_call = actions.max_raise_to <= betting_state.current_bet;
  actions.can_all_in = all_in_is_call || (opponent_can_act && raising_is_open);

  return actions;
}

bool validate_decision(const Table &table, const PotManager &pot_manager,
                       const BettingRoundState &betting_state, int player_id,
                       const PlayerDecision &decision) {
  // Use the same action rules as the UI and AI. This also rejects actions
  // for invalid seats, folded players, and players who cannot act.
  const LegalActions actions =
      get_legal_actions(table, pot_manager, betting_state, player_id);

  return validate_decision(actions, decision);
}

bool validate_decision(const LegalActions &actions,
                       const PlayerDecision &decision) {

  // Only RaiseTo uses decision.raise_to. For the other actions, that field
  // is ignored: calling and going all-in use amounts calculated from state.
  switch (decision.action) {
  case ActionType::Fold:
    return actions.can_fold;
  case ActionType::Check:
    return actions.can_check;
  case ActionType::Call:
    return actions.can_call;
  case ActionType::RaiseTo:
    // The requested TOTAL for this round must be a legal full raise and
    // affordable. Both limits are inclusive: exactly the minimum is valid,
    // and exactly the maximum means contributing all remaining cash.
    // A short all-in below the minimum must use ActionType::AllIn instead.
    return actions.can_raise && decision.raise_to >= actions.min_raise_to &&
           decision.raise_to <= actions.max_raise_to;
  case ActionType::AllIn:
    return actions.can_all_in;
  }

  // An unrecognized enum value is not a valid action.
  return false;
}

void execute_decision(Table &table, PotManager &pot_manager,
                      BettingRoundState &betting_state, int player_id,
                      const PlayerDecision &decision) {
  // Reject an illegal action before moving chips or changing any flags.
  // This also checks player_id before we use it to access the players array.
  if (!validate_decision(table, pot_manager, betting_state, player_id,
                         decision)) {
    throw std::invalid_argument("Cannot execute an illegal poker decision");
  }

  detail::apply_validated_decision(table, pot_manager, betting_state, player_id,
                                   decision);
}

void detail::apply_validated_decision(Table &table, PotManager &pot_manager,
                                      BettingRoundState &betting_state,
                                      int player_id,
                                      const PlayerDecision &decision) {

  Player &player = table.players[player_id];
  const int previous_bet = betting_state.current_bet;

  switch (decision.action) {
  case ActionType::Fold:
    // The player leaves the hand, but chips they already paid stay in the pot.
    player.in_hand = false;
    break;
  case ActionType::Check:
    // Checking costs nothing. We still record that the player acted below.
    break;
  case ActionType::Call:
    // Pay only the amount still owed. contribute limits payment to the
    // player's cash and marks them all-in if they have no chips left.
    pot_manager.contribute(
        table, player_id,
        amount_to_call(pot_manager, betting_state, player_id));
    break;
  case ActionType::RaiseTo:
    // raise_to is the TOTAL paid this round. If the target is 200 and the
    // player already paid 50, move only another 150 chips into the pot.
    pot_manager.contribute(table, player_id,
                           decision.raise_to -
                               pot_manager.round_contribution(player_id));
    break;
  case ActionType::AllIn:
    // Move every remaining chip. Depending on the resulting contribution,
    // this can be a partial call, an exact call, or a raise.
    pot_manager.contribute(table, player_id, player.cash);
    break;
  }

  const int new_contribution = pot_manager.round_contribution(player_id);
  if (new_contribution > previous_bet) {
    const int raise_size = new_contribution - previous_bet;
    betting_state.current_bet = new_contribution;

    if (raise_size >= betting_state.minimum_raise_size) {
      // A full raise sets the size of the next minimum raise and lets the
      // other players act and raise again. For example, raising from 100 to
      // 250 makes the next minimum raise 150, so the next target is 400.
      betting_state.minimum_raise_size = raise_size;
      betting_state.acted_since_last_full_raise.fill(false);
    }
    // A short all-in also increases the amount opponents must call, but
    // keeps the minimum raise and their existing raising rights unchanged.
  }

  // Set this AFTER any full-raise reset: the player who just raised has
  // already acted, while the other players now need to respond.
  betting_state.acted_since_last_full_raise[player_id] = true;
}

bool player_needs_action(const Table &table, const PotManager &pot_manager,
                         const BettingRoundState &betting_state,
                         int player_id) {
  // An invalid seat cannot take a turn. Check before accessing either array.
  if (player_id < 0 || player_id >= table.num_players ||
      player_id >= MAX_PLAYERS) {
    return false;
  }

  const Player &player = table.players[player_id];
  // Folded players are out of the hand. All-in players and players with no
  // cash cannot make another betting decision, even if they are below the bet.
  if (!player.in_hand || player.all_in || player.cash <= 0) {
    return false;
  }

  bool has_opponent = false;
  bool opponent_can_act = false;
  for (int other_id = 0; other_id < table.num_players; ++other_id) {
    if (other_id == player_id || !table.players[other_id].in_hand) {
      continue;
    }
    has_opponent = true;
    const Player &opponent = table.players[other_id];
    if (!opponent.all_in && opponent.cash > 0) {
      opponent_can_act = true;
      break;
    }
  }

  // If everybody else folded, this player has won: no further turn is needed.
  if (!has_opponent) {
    return false;
  }

  // Owing chips requires another decision even if the player already acted.
  // For example, after calling 100, a short all-in to 130 means they must
  // respond to the extra 30, although their acted flag is still true.
  if (amount_to_call(pot_manager, betting_state, player_id) > 0) {
    return true;
  }

  // With nothing owed, a turn is needed only if an opponent can still act
  // and this player has not acted yet. This gives the big blind their option
  // after everyone calls, but skips extra checks against all-in opponents.
  return opponent_can_act &&
         !betting_state.acted_since_last_full_raise[player_id];
}

bool betting_round_complete(const Table &table, const PotManager &pot_manager,
                            const BettingRoundState &betting_state) {
  // One player still needing a decision is enough to keep the round open.
  // Reuse player_needs_action so calls, raises, folds, and all-ins follow
  // the same rules when checking a player and when checking the whole round.
  for (int player_id = 0; player_id < table.num_players; ++player_id) {
    if (player_needs_action(table, pot_manager, betting_state, player_id)) {
      return false;
    }
  }

  // Nobody needs another turn. This also covers everyone being all-in or
  // only one player remaining after the others folded.
  return true;
}

int next_player_needing_action(const Table &table,
                               const PotManager &pot_manager,
                               const BettingRoundState &betting_state,
                               int after_player_id) {
  // There is no seat to search on an empty table. Also reject a player count
  // larger than the players array before using it to visit seats.
  if (table.num_players <= 0 || table.num_players > MAX_PLAYERS) {
    return -1;
  }

  // Bring the starting position into the table's seat range. Passing -1
  // starts just before seat 0, so seat 0 will be checked first.
  int player_id = after_player_id % table.num_players;
  if (player_id < 0) {
    player_id += table.num_players;
  }

  // Visit each seat once, starting AFTER the supplied position. For three
  // players, starting after seat 1 checks seats 2, 0, then 1. The starting
  // seat is checked last in case it is the only player who still owes chips.
  for (int checked = 0; checked < table.num_players; ++checked) {
    player_id = (player_id + 1) % table.num_players;
    if (player_needs_action(table, pot_manager, betting_state, player_id)) {
      return player_id;
    }
  }

  // Every seat was checked, and nobody needs another betting decision.
  return -1;
}

} // namespace poker
