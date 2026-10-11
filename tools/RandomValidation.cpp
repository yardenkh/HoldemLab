// Compares fast and reference scores on one million reproducible, valid random
// seven-card hands.

#include "card/Card.hpp"
#include "evaluation/FastHandEvaluator.hpp"
#include "evaluation/HandEvaluator.hpp"

#include <array>
#include <filesystem>
#include <iostream>
#include <random>
#include <string>

namespace {

std::filesystem::path
resolve_table_path(const std::filesystem::path &executable_path) {
  // Locate existing generated data; this checker does not regenerate the table.
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

std::array<poker::Card, 7> generate_random_hand(std::mt19937 &rng) {
  // Map card IDs 0..51 to four suits of thirteen ranks. Reject repeats within
  // this hand so the unchecked production evaluator receives valid cards.
  std::array<bool, 52> used{};
  std::array<poker::Card, 7> hand{};

  for (int i = 0; i < 7; ++i) {
    // Every card ID has the same probability before duplicate rejection.
    int index = std::uniform_int_distribution<int>(0, 51)(rng);
    while (used[index]) {
      index = static_cast<int>(std::uniform_int_distribution<int>(0, 51)(rng));
    }
    used[index] = true;

    const int rank = index % 13;
    const int suit = index / 13;
    hand[i] = poker::Card{static_cast<poker::Face>(rank + 2),
                          static_cast<poker::Suit>(suit)};
  }

  return hand;
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

  poker::FastHandEvaluator evaluator(table_path);
  std::mt19937 rng(20240831u);
  // A fixed seed makes any reported mismatch repeatable during debugging.

  for (int i = 0; i < 1'000'000; ++i) {
    const auto hand = generate_random_hand(rng);
    const auto reference = poker::evaluate_seven_cards(hand);
    const auto fast_score = evaluator.evaluate(hand);
    const auto packed = evaluator.packed_score(fast_score);
    // Compare identical score formats, not dense IDs against packed bit fields.

    if (packed != reference) {
      std::cerr << "Mismatch on random hand " << i << '\n';
      for (const auto &card : hand) {
        std::cerr << poker::card_to_string(card) << ' ';
      }
      std::cerr << "\nReference: " << reference << " Fast: " << packed << '\n';
      return 2;
    }
  }

  std::cout << "Random differential validation passed: 1,000,000 hands\n";
  return 0;
}
