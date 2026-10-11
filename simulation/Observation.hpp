// Optional model-input encoding into caller-owned buffers, separate from poker
// rules.

#pragma once
#include "view/PlayerView.hpp"
#include <cmath>
#include <span>
#include <stdexcept>

namespace poker {
// Optional example encoding, separate from poker rules. Layout version 1:
// 0..5: player, seat count, stage, dealer, small blind seat, big blind seat.
// 6..10: pot, bet, call, min raise target, max raise target (divided by
// chip_scale). 11..12: hole/board counts. 13..19: own hole + visible board card
// IDs (0..51), or -1 for an undealt card. 20..59: five fields per seat: cash,
// round paid, total paid (scaled), in_hand, all_in. Unused seats remain zero.
// Card/seat/stage IDs are categorical inputs, not an asserted numeric ordering
// for a neural network. A model can embed or one-hot encode them separately.
struct EncodedObservation {
  std::array<float, 20 + 5 * MAX_PLAYERS> values{};
  // Fold, Check, Call, RaiseTo, AllIn. Raise size still requires range
  // handling.
  std::array<bool, 5> legal_mask{};
};
inline void encode_observations(std::span<const PlayerView> views,
                                std::span<EncodedObservation> output,
                                float chip_scale = 1.0f) {
  if (views.size() != output.size() || !std::isfinite(chip_scale) ||
      chip_scale <= 0)
    throw std::invalid_argument("Invalid observation buffer or chip scale");
  for (std::size_t row = 0; row < views.size(); ++row) {
    const auto &v = views[row];
    auto &out = output[row];
    out = {};
    auto &x = out.values;
    x[0] = static_cast<float>(v.player_id);
    x[1] = static_cast<float>(v.num_players);
    x[2] = static_cast<float>(v.stage);
    x[3] = static_cast<float>(v.dealer_id);
    x[4] = static_cast<float>(v.small_blind_id);
    x[5] = static_cast<float>(v.big_blind_id);
    x[6] = v.pot_amount / chip_scale;
    x[7] = v.current_bet / chip_scale;
    x[8] = v.legal_actions.amount_to_call / chip_scale;
    x[9] = v.legal_actions.min_raise_to / chip_scale;
    x[10] = v.legal_actions.max_raise_to / chip_scale;
    x[11] = static_cast<float>(v.hole_card_count);
    x[12] = static_cast<float>(v.community_card_count);
    // Read only visible cards. The encoder cannot access opponents or the deck.
    const auto card_id = [](Card card) {
      return static_cast<float>(static_cast<int>(card.suit) * 13 +
                                static_cast<int>(card.face) - 2);
    };
    for (int i = 0; i < 2; ++i)
      x[13 + i] = i < v.hole_card_count ? card_id(v.hole_cards[i]) : -1;
    for (int i = 0; i < 5; ++i)
      x[15 + i] =
          i < v.community_card_count ? card_id(v.community_cards[i]) : -1;
    for (int id = 0; id < MAX_PLAYERS; ++id) {
      x[20 + id * 5] = v.player_stacks[id] / chip_scale;
      x[21 + id * 5] = v.round_contributions[id] / chip_scale;
      x[22 + id * 5] = v.total_contributions[id] / chip_scale;
      x[23 + id * 5] = v.players_in_hand[id];
      x[24 + id * 5] = v.players_all_in[id];
    }
    out.legal_mask = {v.legal_actions.can_fold, v.legal_actions.can_check,
                      v.legal_actions.can_call, v.legal_actions.can_raise,
                      v.legal_actions.can_all_in};
  }
}
} // namespace poker
