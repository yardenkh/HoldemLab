// Measures seven-card evaluation cost on the same pregenerated hands for both
// evaluators.

#include "card/Card.hpp"
#include "evaluation/FastHandEvaluator.hpp"
#include "evaluation/HandEvaluator.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <random>
#include <vector>

namespace {

std::filesystem::path
resolve_table_path(const std::filesystem::path &executable_path) {
  // Support both a CMake-copied table beside this tool and project-root
  // launches.
  const std::filesystem::path executable_dir = executable_path.parent_path();
  const std::filesystem::path local_table = executable_dir / "hand_tables.bin";
  if (std::filesystem::exists(local_table)) {
    return local_table;
  }

  const std::filesystem::path project_root = std::filesystem::path(".");
  const std::filesystem::path root_table =
      project_root / "data" / "hand_tables.bin";
  return std::filesystem::exists(root_table) ? root_table : local_table;
}

std::vector<std::array<poker::Card, 7>> generate_hands(std::size_t count,
                                                       std::mt19937 &rng) {
  // Generate the input dataset before timing either evaluator. Both scorers
  // receive exactly these same hands, without timing RNG or file loading.
  std::vector<std::array<poker::Card, 7>> hands;
  hands.reserve(count);

  for (std::size_t i = 0; i < count; ++i) {
    std::array<bool, 52> used{};
    // used[id] prevents the same physical card appearing twice within one hand.
    std::array<poker::Card, 7> hand{};

    for (int j = 0; j < 7; ++j) {
      // Bounded sampling avoids the small bias in taking rng() modulo 52.
      int index = std::uniform_int_distribution<int>(0, 51)(rng);
      while (used[index]) {
        index =
            static_cast<int>(std::uniform_int_distribution<int>(0, 51)(rng));
      }
      used[index] = true;

      const int rank = index % 13;
      const int suit = index / 13;
      hand[j] = poker::Card{static_cast<poker::Face>(rank + 2),
                            static_cast<poker::Suit>(suit)};
    }

    hands.push_back(hand);
  }

  return hands;
}

} // namespace

int main(int argc, char **argv) {
  const std::filesystem::path executable_path =
      argc > 0 ? std::filesystem::path(argv[0]) : std::filesystem::path();
  const std::filesystem::path table_path = resolve_table_path(executable_path);
  if (!std::filesystem::exists(table_path)) {
    std::cerr << "Missing hand_tables.bin: " << table_path << '\n';
    return 1;
  }

  std::mt19937 rng(1337u);
  const auto hands = generate_hands(200'000, rng);
  poker::FastHandEvaluator fast(table_path);

  std::uint64_t checksum = 0;
  // Keep and print accumulated scores so evaluation results are observable
  // and the optimizer cannot simply remove the timed scoring work.
  const auto start_fast = std::chrono::steady_clock::now();
  for (const auto &hand : hands) {
    checksum += fast.evaluate(hand);
  }
  const auto end_fast = std::chrono::steady_clock::now();

  const auto fast_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           end_fast - start_fast)
                           .count();
  const double fast_ns_per_hand =
      static_cast<double>(fast_ns) / static_cast<double>(hands.size());

  std::uint64_t reference_checksum = 0;
  // Dense and packed scores use different numeric scales, so these two
  // checksums are not expected to match. RandomValidation compares converted
  // scores for correctness; this executable measures evaluation speed only.
  const auto start_ref = std::chrono::steady_clock::now();
  for (const auto &hand : hands) {
    reference_checksum += poker::evaluate_seven_cards(hand);
  }
  const auto end_ref = std::chrono::steady_clock::now();
  const auto ref_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(end_ref - start_ref)
          .count();
  const double ref_ns_per_hand =
      static_cast<double>(ref_ns) / static_cast<double>(hands.size());

  const double speedup = ref_ns_per_hand / fast_ns_per_hand;

  std::cout << "Fast evaluator: " << fast_ns_per_hand << " ns/hand, "
            << (1'000'000'000.0 / fast_ns_per_hand) << " hands/sec\n";
  std::cout << "Reference evaluator: " << ref_ns_per_hand << " ns/hand, "
            << (1'000'000'000.0 / ref_ns_per_hand) << " hands/sec\n";
  std::cout << "Speedup: " << speedup << "x\n";
  std::cout << "Checksum: " << checksum << " / " << reference_checksum << '\n';
  return 0;
}
