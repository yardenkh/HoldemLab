// Samples unknown physical cards for rollouts while excluding known cards and
// duplicate draws.

#pragma once
#include "card/Card.hpp"
#include <array>
#include <random>
#include <span>
#include <stdexcept>

namespace poker {
// Reusable sampler for equity rollouts: exclude visible/known cards once, then
// draw only the missing cards. Each sample is without replacement; subsequent
// calls reuse the same buffer and require no full-deck shuffle or allocation.
class CardSampler {
public:
  explicit CardSampler(std::span<const Card> known = {}) { reset(known); }
  void reset(std::span<const Card> known) {
    std::array<bool, 52> excluded{};
    for (Card card : known) {
      const int rank = static_cast<int>(card.face) - 2;
      const int suit = static_cast<int>(card.suit);
      if (rank < 0 || rank >= 13 || suit < 0 || suit >= 4)
        throw std::invalid_argument("Invalid known card");
      const int id = suit * 13 + rank;
      if (excluded[id])
        throw std::invalid_argument("Duplicate known card");
      excluded[id] = true;
    }
    count_ = 0;
    for (int id = 0; id < 52; ++id)
      if (!excluded[id])
        cards_[count_++] = {static_cast<Face>(id % 13 + 2),
                            static_cast<Suit>(id / 13)};
  }
  template <class Generator>
  void sample(std::span<Card> output, Generator &rng) {
    if (output.size() > static_cast<std::size_t>(count_))
      throw std::invalid_argument("Not enough unknown cards to sample");
    // A partial Fisher-Yates shuffle works from any current permutation.
    // Bounded distributions avoid the bias introduced by rng() % range.
    for (std::size_t i = 0; i < output.size(); ++i) {
      const int selected = std::uniform_int_distribution<int>(
          static_cast<int>(i), count_ - 1)(rng);
      std::swap(cards_[i], cards_[selected]);
      output[i] = cards_[i];
    }
  }
  int remaining_cards() const { return count_; }

private:
  std::array<Card, 52> cards_{};
  int count_ = 0;
};
} // namespace poker
