// Information one player may see when choosing an action; opponents' private
// cards are excluded.

#ifndef POKER_PLAYER_VIEW_HPP
#define POKER_PLAYER_VIEW_HPP

// Provides PokerStage, LegalActions, and the types used to build this view.
#include "betting/Betting.hpp"

namespace poker {

// A snapshot of the information one player may use to choose an action.
// It contains that player's private cards and the public table information,
// but no opponents' private cards. A human UI or AI can use this snapshot
// without receiving access to the complete Table.
struct PlayerView {
  // The player this view belongs to. For example, 2 means table.players[2].
  // -1 means no player has been selected yet.
  int player_id = -1;
  // Number of seats in use, including players who folded this hand.
  int num_players = 0;

  // Current part of the hand: PreFlop, Flop, Turn, River, or Showdown.
  PokerStage stage = PokerStage::PreFlop;

  // Only this player's private cards. Read the first hole_card_count cards;
  // the remaining array entries do not represent dealt cards.
  std::array<Card, HOLE_CARDS_PER_PLAYER> hole_cards{};
  // Number of private cards actually dealt to this player, normally two.
  int hole_card_count = 0;

  // Public cards shared by all players. Read only the first
  // community_card_count entries, even though the array has room for five.
  std::array<Card, MAX_COMMUNITY_CARDS> community_cards{};
  // Normally 0 before the flop, 3 on the flop, 4 on the turn, and 5 on the
  // river.
  int community_card_count = 0;

  // Each position in the following player arrays refers to the same player ID.
  // For example, player_stacks[2] is player 2's remaining cash, and
  // players_in_hand[2] says whether player 2 is still in this hand.
  // Use positions 0 through num_players - 1; other entries are unused.
  // {} starts numeric entries at zero and bool entries at false.

  // Chips each player still has available, excluding chips already in the pot.
  std::array<int, MAX_PLAYERS> player_stacks{};
  // True for players still competing for the pot; false after they fold.
  std::array<bool, MAX_PLAYERS> players_in_hand{};
  // True for players who committed all their chips. They can still win a pot
  // if they are in the hand, but cannot take another betting action.
  std::array<bool, MAX_PLAYERS> players_all_in{};

  // Chips each player paid during THIS betting round. These amounts are
  // compared with current_bet to determine how much a player still owes.
  std::array<int, MAX_PLAYERS> round_contributions{};
  // Chips each player paid across the ENTIRE hand, including earlier rounds
  // and contributions from players who later folded.
  std::array<int, MAX_PLAYERS> total_contributions{};

  // Player IDs identifying the dealer button and the two blind positions.
  // -1 means that position has not been assigned in this view yet.
  int dealer_id = -1;
  // Player who posts the small blind; also the dealer in a two-player hand.
  int small_blind_id = -1;
  // Player who posts the big blind.
  int big_blind_id = -1;

  // Total chips contributed by all players across the whole hand, including
  // all side pots. This is not just the amount paid in the current round.
  int pot_amount = 0;
  // Total round contribution needed to match the current bet. For example,
  // current_bet = 100 and round_contributions[player_id] = 40 means owing 60.
  int current_bet = 0;

  // Actions allowed for THIS player, plus the call amount and raise limits.
  // The UI can use these to show available choices; the AI can use them to
  // choose a legal decision. LegalActions supplies its own false/zero defaults.
  LegalActions legal_actions;
};

// Build a snapshot for player_id from the table, contribution records, and
// current betting state. stage supplies the current part of the hand.
// Copy only this player's private cards, along with the public information,
// and calculate their legal actions. The const references mean the source
// objects are read without being changed.
// Throws std::out_of_range for an invalid player_id, or std::invalid_argument
// if the table's player count or dealt card counts exceed their array limits.
PlayerView make_player_view(const Table &table, const PotManager &pot_manager,
                            const BettingRoundState &betting_state,
                            PokerStage stage, int player_id);

// Internal overload for an engine that already calculated this seat's actions.
// actions must describe the supplied state, which must not change during use.
PlayerView make_player_view(const Table &table, const PotManager &pot_manager,
                            const BettingRoundState &betting_state,
                            PokerStage stage, int player_id,
                            const LegalActions &actions);

} // namespace poker

#endif // POKER_PLAYER_VIEW_HPP
