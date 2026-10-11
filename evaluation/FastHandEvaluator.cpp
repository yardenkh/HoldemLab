// Loads lookup data once and indexes it by rank counts or same-suit rank masks.

#include "FastHandEvaluator.hpp"

#include <bit>
#include <fstream>
#include <stdexcept>
#include <string>

namespace poker {

const FastHandEvaluator &default_fast_hand_evaluator() {
  // C++ initializes a function-local static once, including concurrent calls.
#ifdef POKER_DEFAULT_HAND_TABLE
  static const FastHandEvaluator evaluator(POKER_DEFAULT_HAND_TABLE);
#else
  static const FastHandEvaluator evaluator("data/hand_tables.bin");
#endif
  return evaluator;
}
namespace {

[[nodiscard]] int rank_index(const Face face) noexcept {
  // Face values are 2..14; subtracting two gives array positions 0..12.
  return static_cast<int>(face) - 2;
}

[[nodiscard]] int suit_index(const Suit suit) noexcept {
  // Suit values 0..3 directly select the matching suit mask below.
  return static_cast<int>(suit);
}

template <typename T>
void read_exact(std::ifstream &input, std::vector<T> &destination) {
  // Read the already-sized array as raw bytes. A short read means the binary
  // file was truncated; never continue with partially populated lookup data.
  input.read(reinterpret_cast<char *>(destination.data()),
             static_cast<std::streamsize>(destination.size() * sizeof(T)));

  if (!input) {
    throw std::runtime_error("FastHandEvaluator: truncated hand table file.");
  }
}

} // namespace

FastHandEvaluator::FastHandEvaluator(const std::filesystem::path &table_file) {
  std::ifstream input(table_file, std::ios::binary);
  if (!input) {
    throw std::runtime_error("FastHandEvaluator: could not open table file: " +
                             table_file.string());
  }

  fast_eval::TableFileHeader header{};
  // Validate the file signature, format version, byte order and array sizes
  // before loading data. The generator and runtime must agree on this layout.
  input.read(reinterpret_cast<char *>(&header), sizeof(header));

  const std::array<char, 8> expected_magic{'P', 'K', 'R', 'E',
                                           'V', 'A', 'L', '1'};
  if (!input || header.magic != expected_magic ||
      header.version != fast_eval::kTableVersion ||
      header.endian_marker != fast_eval::kEndianMarker ||
      header.non_flush_entries != fast_eval::kNonFlushTableSize ||
      header.flush_entries != fast_eval::kFlushTableSize ||
      header.packed_score_entries != fast_eval::kPackedScoreCount) {
    throw std::runtime_error(
        "FastHandEvaluator: incompatible hand table file.");
  }

  // Allocate only at initialization. Per-hand evaluation then uses array reads.
  non_flush_table_.resize(header.non_flush_entries);
  flush_table_.resize(header.flush_entries);
  packed_score_table_.resize(header.packed_score_entries);

  read_exact(input, non_flush_table_);
  read_exact(input, flush_table_);
  read_exact(input, packed_score_table_);
}

DenseHandScore FastHandEvaluator::evaluate_unsafe(
    const Card &c0, const Card &c1, const Card &c2, const Card &c3,
    const Card &c4, const Card &c5, const Card &c6) const noexcept {

  std::uint32_t non_flush_hash = 0;
  // One 13-bit mask per suit. Bit 0 represents a two, bit 12 represents an ace.
  // Setting a bit records that this rank is present in the corresponding suit.
  std::array<std::uint16_t, 4> suit_masks{};

  const Card cards[] = {c0, c1, c2, c3, c4, c5, c6};
  for (const Card &card : cards) {
    const int rank = rank_index(card.face);
    const int suit = suit_index(card.suit);

    // Add one rank weight per card, so repeated ranks contribute repeatedly.
    // The chosen weights give each legal seven-card rank pattern its own index.
    non_flush_hash += fast_eval::kRankHash[rank];
    suit_masks[suit] |= static_cast<std::uint16_t>(1u << rank);
  }

  // popcount counts set bits. Five ranks in a suit means a flush; the small
  // flush table already knows whether the best hand is a flush or straight
  // flush.
  if (std::popcount(suit_masks[0]) >= 5) {
    return flush_table_[suit_masks[0]];
  }
  if (std::popcount(suit_masks[1]) >= 5) {
    return flush_table_[suit_masks[1]];
  }
  if (std::popcount(suit_masks[2]) >= 5) {
    return flush_table_[suit_masks[2]];
  }
  if (std::popcount(suit_masks[3]) >= 5) {
    return flush_table_[suit_masks[3]];
  }

  // No flush: fetch the precomputed best hand for these seven ranks.
  return non_flush_table_[non_flush_hash];
}

std::uint32_t
FastHandEvaluator::packed_score(const DenseHandScore score) const noexcept {
  // Dense strength values are compact ordered IDs, not packed rank/kicker bits.
  // Convert only when a caller needs the existing public HandScore
  // representation.
  return packed_score_table_[score];
}

DenseHandRank
FastHandEvaluator::hand_rank(const DenseHandScore score) noexcept {
  // The dense scores are sorted by poker strength, and all hand categories
  // occupy contiguous ranges. These boundaries are the exact counts of the
  // 7,462 distinct five-card poker equivalence classes.
  if (score <= 1'277)
    return DenseHandRank::HighCard;
  if (score <= 4'137)
    return DenseHandRank::OnePair;
  if (score <= 4'995)
    return DenseHandRank::TwoPair;
  if (score <= 5'853)
    return DenseHandRank::ThreeOfAKind;
  if (score <= 5'863)
    return DenseHandRank::Straight;
  if (score <= 7'140)
    return DenseHandRank::Flush;
  if (score <= 7'296)
    return DenseHandRank::FullHouse;
  if (score <= 7'452)
    return DenseHandRank::FourOfAKind;
  return DenseHandRank::StraightFlush;
}

} // namespace poker
