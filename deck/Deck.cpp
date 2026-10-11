// Builds, shuffles, and consumes a deck using the game-provided random
// generator.

#include "deck/Deck.hpp"
#include <algorithm>
#include <stdexcept>

namespace poker {

// A new deck starts with all 52 distinct cards in a predictable order.
Deck::Deck() { reset(); }

void Deck::reset() {
  // Rebuild four suits with thirteen ranks each. Cards stay in this array
  // after dealing; next_card_index_ separates consumed cards from available
  // ones.
  int card_index = 0;

  for (int suit = static_cast<int>(Suit::Hearts);
       suit <= static_cast<int>(Suit::Spades); suit++) {
    for (int face = static_cast<int>(Face::Two);
         face <= static_cast<int>(Face::Ace); face++) {
      cards_[card_index] = {static_cast<Face>(face), static_cast<Suit>(suit)};
      card_index++;
    }
  }

  next_card_index_ = 0;
}

void Deck::shuffle(std::mt19937 &random_generator) {
  // The game supplies its RNG so seeded experiments can replay the deal.
  // Shuffle reuses the current array; reset first if an ordered start is
  // wanted.
  next_card_index_ = 0;
  std::shuffle(cards_.begin(), cards_.end(), random_generator);
}

Card Deck::deal_card() {
  // Advancing the position avoids removing or moving the remaining cards.
  if (next_card_index_ >= DECK_SIZE) {
    throw std::out_of_range("Cannot deal from an empty deck.");
  }
  return cards_[next_card_index_++];
}

// Consume a card without adding it to any player's hand or the public board.
void Deck::burn_card() { deal_card(); }

int Deck::cards_remaining() const { return DECK_SIZE - next_card_index_; }

int Deck::cards_dealt() const { return next_card_index_; }

} // namespace poker
