// Applies table-level preparation and dealing; betting decisions live in
// Betting.cpp.

#include "table/Table.hpp"

namespace poker {

void table_init(Table &table, int num_players, int starting_chips) {
  // Set up equal stacks and an initial button at seat 0. The caller supplies
  // a valid seat count; PokerGame checks that before calling this helper.
  table.num_players = num_players;
  for (int i = 0; i < num_players; i++) {
    table.players[i].cash = starting_chips;
    table.players[i].reset_for_new_hand();
  }

  table.community_card_count = 0;

  table.deck.reset();
  table.dealer_id = 0;
  if (num_players == 2) {
    table.small_blind_id = 0;
    table.big_blind_id = 1;
  } else {
    table.small_blind_id = 1;
    table.big_blind_id = 2;
  }
}

int count_players_with_chips(const Table &table) {
  // Count funded seats, regardless of whether they folded this particular hand.
  // bool converts to 1 for true and 0 for false when added to the count.
  int players_with_chips = 0;
  int num_players = table.num_players;
  for (int i = 0; i < num_players; i++) {
    players_with_chips += table.players[i].cash > 0;
  }
  return players_with_chips;
}

int count_players_in_hand(const Table &table) {
  // Include all-in players: they cannot bet again but still compete at
  // showdown.
  int players_in_hand = 0;
  int num_players = table.num_players;
  for (int i = 0; i < num_players; i++) {
    players_in_hand += table.players[i].in_hand;
  }
  return players_in_hand;
}

int count_players_able_to_act(const Table &table) {
  // Being in_hand is not enough: another action also needs chips and no all-in
  // flag.
  int players_able_to_act = 0;
  int num_players = table.num_players;
  for (int i = 0; i < num_players; i++) {
    const Player &p = table.players[i];
    players_able_to_act += (p.in_hand && !p.all_in && p.cash > 0);
  }
  return players_able_to_act;
}

int find_only_remaining_player(const Table &table) {
  // -1 means either nobody remains or more than one remains. Return a seat ID
  // only when the hand can be awarded without comparing any cards.
  int remaining_player = -1;

  for (int i = 0; i < table.num_players; i++) {
    if (!table.players[i].in_hand)
      continue;

    // If we already found one player, then there is more than one remaining.
    if (remaining_player != -1)
      return -1;

    remaining_player = i;
  }

  return remaining_player;
}

int next_player_with_chips(const Table &table, int player_id) {
  // Visit other seats clockwise. % wraps from the last seat back to seat 0.
  // This table helper excludes the starting seat, unlike the betting search
  // that may return to a starting player who still owes chips.
  int num_players = table.num_players;

  for (int i = 1; i < num_players; i++) {
    int next_player = (player_id + i) % num_players;
    const Player &player = table.players[next_player];

    if (player.cash > 0)
      return next_player;
  }

  return -1;
}

int next_player_in_hand(const Table &table, int player_id) {
  // This search includes all-in players but skips folded/eliminated seats.
  int num_players = table.num_players;

  for (int i = 1; i < num_players; i++) {
    int next_player = (player_id + i) % num_players;
    const Player &player = table.players[next_player];

    if (player.in_hand)
      return next_player;
  }

  return -1;
}

int next_player_able_to_act(const Table &table, int player_id) {
  // Find an opponent able to bet; this does not check whether that player has
  // already acted or matched the bet. Betting.cpp handles those extra rules.
  int num_players = table.num_players;

  for (int i = 1; i < num_players; i++) {
    int next_player = (player_id + i) % num_players;
    const Player &player = table.players[next_player];

    if (player.in_hand && !player.all_in && player.cash > 0)
      return next_player;
  }

  return -1;
}

void table_prepare_new_hand(Table &table, std::mt19937 &random_generator) {
  // Convenience helper: rotate at preparation time. PokerGame prepares its
  // own hand because it rotates AFTER payout instead; do not call both paths.
  table.deck.shuffle(random_generator); // Also resets the next deal position.
  table.community_card_count = 0;

  for (int i = 0; i < table.num_players; i++) {
    table.players[i].reset_for_new_hand();
  }

  rotate_dealer(table);
  assign_blinds(table);
}

void rotate_dealer(Table &table) {
  // The next funded seat receives the button; eliminated seats are skipped.
  table.dealer_id = next_player_with_chips(table, table.dealer_id);
}

void assign_blinds(Table &table) {
  // Apply heads-up positions whenever TWO funded players remain, even if the
  // table still has more seat slots. The dealer is the small blind heads-up.
  if (table.num_players == 2 || count_players_with_chips(table) == 2) {
    table.small_blind_id = table.dealer_id;
    table.big_blind_id = next_player_with_chips(table, table.dealer_id);
  } else {
    table.small_blind_id = next_player_with_chips(table, table.dealer_id);
    table.big_blind_id = next_player_with_chips(table, table.small_blind_id);
  }
}

void deal_hole_cards(Table &table) {
  // Deal one card to every participant, then repeat for their second card.
  // cards_in_hand tracks the valid entries; folded/eliminated seats are
  // skipped.
  for (int card_number = 0; card_number < HOLE_CARDS_PER_PLAYER;
       card_number++) {
    for (int player_id = 0; player_id < table.num_players; player_id++) {
      if (table.players[player_id].in_hand) {
        table.players[player_id].receive_card(table.deck.deal_card());
      }
    }
  }
}

void deal_flop(Table &table) {
  // Burn one card privately, then expose three community cards.
  table.deck.burn_card();
  for (int i = 0; i < 3; i++) {
    table.community_cards[table.community_card_count++] =
        table.deck.deal_card();
  }
}

void deal_turn(Table &table) {
  // Called after the flop: burn one, then append the fourth board card.
  table.deck.burn_card();
  table.community_cards[table.community_card_count++] = table.deck.deal_card();
}

void deal_river(Table &table) {
  // Called after the turn: burn one, then append the fifth board card.
  table.deck.burn_card();
  table.community_cards[table.community_card_count++] = table.deck.deal_card();
}

} // namespace poker
