// Production seven-card scorer backed by immutable precomputed lookup arrays.

#pragma once

#include "FastHandTableFormat.hpp"
#include "card/Card.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace poker {

// A dense 1..7462 strength value. Larger values are always stronger.
// Zero is reserved for an invalid/uninitialized table entry.
using DenseHandScore = std::uint16_t;
using HandStrength = DenseHandScore;

enum class DenseHandRank : std::uint8_t {
  HighCard,
  OnePair,
  TwoPair,
  ThreeOfAKind,
  Straight,
  Flush,
  FullHouse,
  FourOfAKind,
  StraightFlush
};

// Scores seven cards by reading precomputed arrays, rather than evaluating all
// 21 five-card subsets. Once loaded, the arrays are read-only and can be shared
// by many games or worker threads. Valid cards are required at this boundary.
class FastHandEvaluator {
public:
  FastHandEvaluator() = delete;
  FastHandEvaluator(const FastHandEvaluator &) = delete;
  FastHandEvaluator &operator=(const FastHandEvaluator &) = delete;
  FastHandEvaluator(FastHandEvaluator &&) = default;
  FastHandEvaluator &operator=(FastHandEvaluator &&) = default;

  // Loads the pre-generated binary table once. Construct one evaluator and
  // reuse it for the lifetime of the poker engine / simulation.
  explicit FastHandEvaluator(const std::filesystem::path &table_file);

  // The production simulation hot path: exactly seven valid cards, no duplicate
  // physical cards, and no per-hand validation overhead.
  [[nodiscard]] DenseHandScore evaluate_unsafe(const Card &c0, const Card &c1,
                                               const Card &c2, const Card &c3,
                                               const Card &c4, const Card &c5,
                                               const Card &c6) const noexcept;

  [[nodiscard]] DenseHandScore
  evaluate(const std::array<Card, 7> &cards) const noexcept {
    return evaluate_unsafe(cards[0], cards[1], cards[2], cards[3], cards[4],
                           cards[5], cards[6]);
  }

  // Convenience overloads for callers that already have the seven cards in
  // hand. These avoid constructing a temporary array in the hot path.
  [[nodiscard]] DenseHandScore evaluate(const Card &c0, const Card &c1,
                                        const Card &c2, const Card &c3,
                                        const Card &c4, const Card &c5,
                                        const Card &c6) const noexcept {
    return evaluate_unsafe(c0, c1, c2, c3, c4, c5, c6);
  }

  [[nodiscard]] DenseHandScore
  evaluate_seven_cards(const std::array<Card, 7> &cards) const noexcept {
    return evaluate(cards);
  }

  // Converts the dense score to the same lexicographically comparable packed
  // representation used by the generator. Intended for UI/debugging/tests.
  [[nodiscard]] std::uint32_t packed_score(DenseHandScore score) const noexcept;

  [[nodiscard]] static DenseHandRank hand_rank(DenseHandScore score) noexcept;

private:
  // Rank-count hashes select an entry when no flush is present. Suit masks
  // select a flush entry. The final table converts dense strengths to the
  // packed HandScore format used by reference evaluation and game observers.
  std::vector<DenseHandScore> non_flush_table_;
  std::vector<DenseHandScore> flush_table_;
  std::vector<std::uint32_t> packed_score_table_;
};

// Lazily load one immutable table shared by games and worker threads. The
// default path is set by CMake; deployments can inject an evaluator instead.
const FastHandEvaluator &default_fast_hand_evaluator();

} // namespace poker
