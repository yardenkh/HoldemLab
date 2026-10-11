// Offline generator for the lookup binary. Normal builds and game execution do
// not regenerate it.

#include "evaluation/FastHandTableFormat.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

// counts[0] counts twos, counts[12] counts aces. Suits are omitted because
// non-flush strength depends on ranks; flush possibilities are handled
// separately.
using RankCounts = std::array<std::uint8_t, 13>;

// Packed score layout, strongest fields first so ordinary integer comparison
// gives poker ordering:
// [category:4][r1:4][r2:4][r3:4][r4:4][r5:4]
[[nodiscard]] std::uint32_t
pack_score(const std::uint8_t category,
           const std::array<std::uint8_t, 5> &ranks = {}) noexcept {

  return (static_cast<std::uint32_t>(category) << 20) |
         (static_cast<std::uint32_t>(ranks[0]) << 16) |
         (static_cast<std::uint32_t>(ranks[1]) << 12) |
         (static_cast<std::uint32_t>(ranks[2]) << 8) |
         (static_cast<std::uint32_t>(ranks[3]) << 4) |
         static_cast<std::uint32_t>(ranks[4]);
}

[[nodiscard]] std::uint16_t rank_mask(const RankCounts &counts) noexcept {
  // Collapse repeated ranks into one bit each. For example, three aces still
  // set only bit 12; straight detection asks which ranks exist, not their
  // counts.
  std::uint16_t mask = 0;
  for (int rank = 0; rank < 13; ++rank) {
    if (counts[rank] != 0) {
      mask |= static_cast<std::uint16_t>(1u << rank);
    }
  }
  return mask;
}

// Returns the actual high-card value of the best straight: 5..14, or 0.
[[nodiscard]] std::uint8_t straight_high(const std::uint16_t mask) noexcept {
  std::uint8_t best = 0;

  // Wheel: A-2-3-4-5.
  constexpr std::uint16_t wheel =
      (1u << 12) | (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3);
  if ((mask & wheel) == wheel) {
    best = 5;
  }

  // Normal straights. rank index 0 means deuce, so high+2 is card value.
  for (int high = 4; high < 13; ++high) {
    const std::uint16_t five_bits =
        static_cast<std::uint16_t>(0x1Fu << (high - 4));
    if ((mask & five_bits) == five_bits) {
      best = static_cast<std::uint8_t>(high + 2);
    }
  }

  return best;
}

[[nodiscard]] std::array<std::uint8_t, 5>
highest_distinct_ranks(const RankCounts &counts, const int excluded_a = -1,
                       const int excluded_b = -1) noexcept {
  // Descending ranks become ordered kickers. Exclude a pair/trip/quad rank
  // when the category already uses it; zero-fill fields that are not needed.
  std::array<std::uint8_t, 5> result{};
  int out = 0;

  for (int rank = 12; rank >= 0 && out < 5; --rank) {
    if (rank != excluded_a && rank != excluded_b && counts[rank] != 0) {
      result[out++] = static_cast<std::uint8_t>(rank + 2);
    }
  }

  return result;
}

// Evaluates ranks while deliberately ignoring flushes. This is exactly what the
// large direct-address table needs, because the runtime flush branch is handled
// before this table is accessed.
[[nodiscard]] std::uint32_t
evaluate_non_flush(const RankCounts &counts) noexcept {
  int quad = -1;
  // -1 means no matching rank has been found; rank indices themselves are
  // 0..12.
  std::array<int, 2> trips{-1, -1};
  int trip_count = 0;
  std::array<int, 3> pairs{-1, -1, -1};
  int pair_count = 0;

  for (int rank = 12; rank >= 0; --rank) {
    if (counts[rank] == 4) {
      quad = rank;
    } else if (counts[rank] == 3 && trip_count < 2) {
      trips[trip_count++] = rank;
    } else if (counts[rank] == 2 && pair_count < 3) {
      pairs[pair_count++] = rank;
    }
  }

  if (quad >= 0) {
    const auto kickers = highest_distinct_ranks(counts, quad);
    return pack_score(
        7, {static_cast<std::uint8_t>(quad + 2), kickers[0], 0, 0, 0});
  }

  if (trip_count >= 1 && (trip_count >= 2 || pair_count >= 1)) {
    // A second triple can supply the pair in a full house: AAA KKK 2 becomes
    // aces full of kings. Descending collection keeps the stronger triple
    // first.
    const int full_house_pair = trip_count >= 2 ? trips[1] : pairs[0];
    return pack_score(6, {static_cast<std::uint8_t>(trips[0] + 2),
                          static_cast<std::uint8_t>(full_house_pair + 2), 0, 0,
                          0});
  }

  if (const std::uint8_t high = straight_high(rank_mask(counts)); high != 0) {
    return pack_score(4, {high, 0, 0, 0, 0});
  }

  if (trip_count >= 1) {
    const auto kickers = highest_distinct_ranks(counts, trips[0]);
    return pack_score(3, {static_cast<std::uint8_t>(trips[0] + 2), kickers[0],
                          kickers[1], 0, 0});
  }

  if (pair_count >= 2) {
    const auto kickers = highest_distinct_ranks(counts, pairs[0], pairs[1]);
    return pack_score(2, {static_cast<std::uint8_t>(pairs[0] + 2),
                          static_cast<std::uint8_t>(pairs[1] + 2), kickers[0],
                          0, 0});
  }

  if (pair_count == 1) {
    const auto kickers = highest_distinct_ranks(counts, pairs[0]);
    return pack_score(1, {static_cast<std::uint8_t>(pairs[0] + 2), kickers[0],
                          kickers[1], kickers[2], 0});
  }

  return pack_score(0, highest_distinct_ranks(counts));
}

[[nodiscard]] std::uint32_t
evaluate_flush_mask(const std::uint16_t mask) noexcept {
  // Only same-suit cards are represented by this mask. Prefer a straight flush;
  // otherwise the five highest present ranks form the best ordinary flush.
  if (const std::uint8_t high = straight_high(mask); high != 0) {
    return pack_score(8, {high, 0, 0, 0, 0});
  }

  RankCounts counts{};
  for (int rank = 0; rank < 13; ++rank) {
    counts[rank] = static_cast<std::uint8_t>((mask >> rank) & 1u);
  }
  return pack_score(5, highest_distinct_ranks(counts));
}

template <typename Function>
void enumerate_rank_counts(const int total_cards, Function &&function) {
  // Visit rank-count patterns, not every physical card combination. Recursion
  // chooses 0..4 cards of each rank and accepts only patterns summing to the
  // requested count. Four is the limit because a deck has four suits per rank.
  RankCounts counts{};

  const auto recurse = [&](auto &&self, const int rank,
                           const int remaining) -> void {
    if (rank == 13) {
      if (remaining == 0) {
        function(counts);
      }
      return;
    }

    const int max_count = std::min(4, remaining);
    for (int count = 0; count <= max_count; ++count) {
      counts[rank] = static_cast<std::uint8_t>(count);
      self(self, rank + 1, remaining - count);
    }
    counts[rank] = 0;
  };

  recurse(recurse, 0, total_cards);
}

[[nodiscard]] std::vector<std::uint32_t> build_packed_score_universe() {
  // List all distinct five-card strength classes in weakest-to-strongest order.
  // Equivalent physical hands share one score; sorting/removing duplicates
  // leaves the 7,462 classes used to assign compact dense IDs.
  std::vector<std::uint32_t> scores;
  scores.reserve(8'000);

  enumerate_rank_counts(5, [&](const RankCounts &counts) {
    scores.push_back(evaluate_non_flush(counts));

    const std::uint16_t mask = rank_mask(counts);
    if (std::popcount(mask) == 5) {
      scores.push_back(evaluate_flush_mask(mask));
    }
  });

  std::sort(scores.begin(), scores.end());
  scores.erase(std::unique(scores.begin(), scores.end()), scores.end());

  if (scores.size() != poker::fast_eval::kDenseScoreCount) {
    throw std::runtime_error(
        "Generator bug: expected exactly 7,462 hand classes.");
  }

  return scores;
}

[[nodiscard]] std::uint16_t
dense_score(const std::vector<std::uint32_t> &score_universe,
            const std::uint32_t packed) {
  // Find the packed score's sorted position and use position+1 as its dense ID.

  const auto it =
      std::lower_bound(score_universe.begin(), score_universe.end(), packed);
  if (it == score_universe.end() || *it != packed) {
    throw std::runtime_error(
        "Generator bug: packed score not found in universe.");
  }

  // Dense score zero is deliberately unused.
  return static_cast<std::uint16_t>(std::distance(score_universe.begin(), it) +
                                    1);
}

[[nodiscard]] std::uint32_t additive_hash(const RankCounts &counts) noexcept {
  // The runtime adds a rank weight once per card. Multiplying weight by count
  // produces the same index here without creating physical card objects.
  std::uint32_t hash = 0;
  for (int rank = 0; rank < 13; ++rank) {
    hash += static_cast<std::uint32_t>(counts[rank]) *
            poker::fast_eval::kRankHash[rank];
  }
  return hash;
}

template <typename T>
void write_vector(std::ofstream &output, const std::vector<T> &data) {
  // Store contiguous array bytes after the header. The loader expects these
  // exact element sizes, array lengths, byte order and writing order.
  output.write(reinterpret_cast<const char *>(data.data()),
               static_cast<std::streamsize>(data.size() * sizeof(T)));
  if (!output) {
    throw std::runtime_error("Failed while writing table file.");
  }
}

} // namespace

int main(int argc, char **argv) {
  // Offline tool only: generating data is not part of normal game execution.
  // An explicit output path lets callers generate a candidate table separately.
  try {
    const std::filesystem::path output_path =
        argc >= 2 ? std::filesystem::path(argv[1])
                  : std::filesystem::path("hand_tables.bin");

    const std::vector<std::uint32_t> score_universe =
        build_packed_score_universe();

    std::vector<std::uint16_t> non_flush(poker::fast_eval::kNonFlushTableSize,
                                         0);
    // Most hash positions are unused. Zero marks them as empty, allowing the
    // checks below to detect accidental collisions while filling legal states.
    std::vector<std::uint16_t> flush(poker::fast_eval::kFlushTableSize, 0);
    std::vector<std::uint32_t> packed_scores(
        poker::fast_eval::kPackedScoreCount, 0);

    for (std::size_t i = 0; i < score_universe.size(); ++i) {
      packed_scores[i + 1] = score_universe[i];
    }

    std::uint32_t non_flush_states = 0;
    enumerate_rank_counts(7, [&](const RankCounts &counts) {
      // Precompute the answer the runtime will later retrieve in one array
      // read.
      const std::uint32_t hash = additive_hash(counts);
      if (hash >= non_flush.size()) {
        throw std::runtime_error("Generator bug: additive hash outside table.");
      }
      if (non_flush[hash] != 0) {
        throw std::runtime_error(
            "Generator bug: additive rank hash collision.");
      }

      non_flush[hash] = dense_score(score_universe, evaluate_non_flush(counts));
      ++non_flush_states;
    });

    if (non_flush_states != 49'205u) {
      throw std::runtime_error(
          "Generator bug: expected 49,205 seven-card rank multisets.");
    }

    std::uint32_t flush_states = 0;
    for (std::uint32_t mask = 0; mask < poker::fast_eval::kFlushTableSize;
         ++mask) {
      // Only masks with five, six or seven ranks can represent a seven-card
      // flush.
      const int cards = std::popcount(mask);
      if (cards >= 5 && cards <= 7) {
        flush[mask] =
            dense_score(score_universe,
                        evaluate_flush_mask(static_cast<std::uint16_t>(mask)));
        ++flush_states;
      }
    }

    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    if (!output) {
      throw std::runtime_error("Could not create table file: " +
                               output_path.string());
    }

    const poker::fast_eval::TableFileHeader header{};
    // Header first, then the three lookup arrays. Flush/close errors matter:
    // a partially written file must not be treated as a usable table.
    output.write(reinterpret_cast<const char *>(&header), sizeof(header));
    write_vector(output, non_flush);
    write_vector(output, flush);
    write_vector(output, packed_scores);

    output.close();
    if (!output) {
      throw std::runtime_error("Failed while closing table file.");
    }

    const auto bytes = std::filesystem::file_size(output_path);
    std::cout << "Generated: " << output_path << '\n'
              << "File size: " << bytes << " bytes ("
              << (static_cast<double>(bytes) / (1024.0 * 1024.0)) << " MiB)\n"
              << "Non-flush rank states stored: " << non_flush_states << '\n'
              << "Flush masks stored: " << flush_states << '\n'
              << "Dense hand classes: " << score_universe.size() << '\n';

    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
