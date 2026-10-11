// Builds an independent player observation from public state and that player's
// private cards.

// Include the declaration and the table/betting types used by this function.
#include "view/PlayerView.hpp"
#include <algorithm> // std::copy_n copies only the cards actually dealt.
#include <stdexcept> // Report invalid seats or inconsistent card counts.

namespace poker {

PlayerView make_player_view(const Table &table, const PotManager &pot_manager,
                            const BettingRoundState &betting_state,
                            PokerStage stage, int player_id) {
  // Reject malformed table sizes before the action calculation visits seats.
  if (table.num_players < 0 || table.num_players > MAX_PLAYERS) {
    throw std::invalid_argument("Table player count is outside its capacity");
  }
  return make_player_view(
      table, pot_manager, betting_state, stage, player_id,
      get_legal_actions(table, pot_manager, betting_state, player_id));
}

PlayerView make_player_view(const Table &table, const PotManager &pot_manager,
                            const BettingRoundState &betting_state,
                            PokerStage stage, int player_id,
                            const LegalActions &actions) {
  // Check the table size and requested seat before reading the player arrays.
  if (table.num_players < 0 || table.num_players > MAX_PLAYERS) {
    throw std::invalid_argument("Table player count is outside its capacity");
  }
  if (player_id < 0 || player_id >= table.num_players) {
    throw std::out_of_range("Player view ID is not an active seat");
  }

  const Player &player = table.players[player_id];
  // Card counts determine how many entries we copy, so they must fit the
  // arrays.
  if (player.cards_in_hand < 0 ||
      player.cards_in_hand > HOLE_CARDS_PER_PLAYER ||
      table.community_card_count < 0 ||
      table.community_card_count > MAX_COMMUNITY_CARDS) {
    throw std::invalid_argument("Dealt card count is outside its capacity");
  }

  // Start with the defaults from PlayerView.hpp. Unused seats and undealt
  // cards keep their default values instead of copying stale table entries.
  PlayerView view{};
  view.player_id = player_id;
  view.num_players = table.num_players;
  view.stage = stage;

  // Copy only the selected player's private cards. Opponents' hands and the
  // deck never enter this view, so a UI or AI cannot inspect them through it.
  view.hole_card_count = player.cards_in_hand;
  std::copy_n(player.hand.begin(), view.hole_card_count,
              view.hole_cards.begin());

  // Everyone may see the community cards that have already been dealt.
  view.community_card_count = table.community_card_count;
  std::copy_n(table.community_cards.begin(), view.community_card_count,
              view.community_cards.begin());

  // Public information stays associated with its player ID. For example,
  // view.player_stacks[2] always describes table.players[2], even if they
  // folded.
  for (int other_id = 0; other_id < table.num_players; ++other_id) {
    const Player &other = table.players[other_id];
    view.player_stacks[other_id] = other.cash;
    view.players_in_hand[other_id] = other.in_hand;
    view.players_all_in[other_id] = other.all_in;
    view.round_contributions[other_id] =
        pot_manager.round_contribution(other_id);
    view.total_contributions[other_id] =
        pot_manager.total_contribution(other_id);
  }

  // Copy the public positions and amounts. The pot includes the whole hand;
  // current_bet is the contribution target for just this betting round.
  view.dealer_id = table.dealer_id;
  view.small_blind_id = table.small_blind_id;
  view.big_blind_id = table.big_blind_id;
  view.pot_amount = pot_manager.total_pot_amount();
  view.current_bet = betting_state.current_bet;

  // Reuse choices calculated for this state. Building the snapshot does not
  // move chips, recalculate legality, or change the game.
  view.legal_actions = actions;
  return view;
}

} // namespace poker
