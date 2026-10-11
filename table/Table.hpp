// Complete table state and helpers for seat positions, participant counts, and
// dealing.

#ifndef POKER_TABLE_HPP
#define POKER_TABLE_HPP

#include "config/PokerConfig.hpp"
#include "deck/Deck.hpp"
#include "player/Player.hpp"
#include <array>
#include <random>

namespace poker {

/**
 * @brief Stores the complete state of a poker table.
 */
struct Table {
  // Number of active seats in the table.
  int num_players = 0;

  // All players seated at the table.
  std::array<Player, MAX_PLAYERS> players{};

  // Deck used for the current hand.
  Deck deck;

  // Community cards currently visible on the board.
  std::array<Card, MAX_COMMUNITY_CARDS> community_cards{};

  // Number of community cards currently dealt.
  int community_card_count = 0;

  // Player currently holding the dealer button.
  int dealer_id = -1;

  // Player currently posting the small blind.
  int small_blind_id = -1;

  // Player currently posting the big blind.
  int big_blind_id = -1;
};

/**
 * @brief Initializes a table and its players.
 *
 * @param table The table to initialize.
 * @param num_players Number of players seated at the table.
 * @param starting_chips Number of chips given to each player.
 */
void table_init(Table &table, int num_players, int starting_chips);

/**
 * @brief Counts players who still have chips remaining.
 *
 * @param table The table to inspect.
 * @return Number of players whose chip stack is greater than zero.
 */
int count_players_with_chips(const Table &table);

/**
 * @brief Counts players who are still participating in the current hand.
 *
 * Folded players are not counted.
 *
 * @param table The table to inspect.
 * @return Number of players still in the hand.
 */
int count_players_in_hand(const Table &table);

/**
 * @brief Counts players who are currently able to take a betting action.
 *
 * Players who folded or are all-in are not able to act.
 *
 * @param table The table to inspect.
 * @return Number of players able to act.
 */
int count_players_able_to_act(const Table &table);

/**
 * @brief Finds the only player remaining in the current hand.
 *
 * This is useful when every other player has folded.
 *
 * @param table The table to inspect.
 * @return The remaining player's ID, or -1 if there is not exactly one.
 */
int find_only_remaining_player(const Table &table);

/**
 * @brief Finds the next player, in table order, who still has chips.
 *
 * The search wraps around from the end of the player array back to the
 * beginning.
 *
 * @param table The table to inspect.
 * @param player_id The player ID to start searching after.
 * @return The ID of the next player with chips, or -1 if none exists.
 */
int next_player_with_chips(const Table &table, int player_id);

/**
 * @brief Finds the next player, in table order, who is still in the hand.
 *
 * The search wraps around from the end of the player array back to the
 * beginning.
 *
 * @param table The table to inspect.
 * @param player_id The player ID to start searching after.
 * @return The ID of the next player still in the hand, or -1 if none exists.
 */
int next_player_in_hand(const Table &table, int player_id);

/**
 * @brief Finds the next player, in table order, who is able to act.
 *
 * Folded and all-in players are skipped. The search wraps around from the
 * end of the player array back to the beginning.
 *
 * @param table The table to inspect.
 * @param player_id The player ID to start searching after.
 * @return The ID of the next player able to act, or -1 if none exists.
 */
int next_player_able_to_act(const Table &table, int player_id);

/**
 * @brief Prepares the table for a new hand.
 *
 * Resets the deck and hand-specific player state, shuffles the deck,
 * clears the community cards, rotates the dealer, and assigns the blinds.
 *
 * @param table The table to prepare.
 * @param random_generator Random generator used to shuffle the deck.
 */
void table_prepare_new_hand(Table &table, std::mt19937 &random_generator);

/**
 * @brief Moves the dealer button to the next player who still has chips.
 *
 * @param table The table whose dealer position should be updated.
 */
void rotate_dealer(Table &table);

/**
 * @brief Assigns the small blind and big blind positions.
 *
 * For heads-up play, the dealer is also the small blind.
 *
 * @param table The table whose blind positions should be assigned.
 */
void assign_blinds(Table &table);

/**
 * @brief Deals two private hole cards to every player still participating.
 *
 * Cards are dealt one at a time in table order.
 *
 * @param table The table whose players should receive hole cards.
 */
void deal_hole_cards(Table &table);

/**
 * @brief Deals the flop.
 *
 * Burns one card and then deals three community cards.
 *
 * @param table The table to deal the flop on.
 */
void deal_flop(Table &table);

/**
 * @brief Deals the turn.
 *
 * Burns one card and then deals one community card.
 *
 * @param table The table to deal the turn on.
 */
void deal_turn(Table &table);

/**
 * @brief Deals the river.
 *
 * Burns one card and then deals one community card.
 *
 * @param table The table to deal the river on.
 */
void deal_river(Table &table);

} // namespace poker

#endif