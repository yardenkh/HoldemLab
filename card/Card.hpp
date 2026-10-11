// Defines physical cards, rank/suit values, comparison, and short display
// labels.

#ifndef POKER_CARD_HPP
#define POKER_CARD_HPP

#include <cstdint> // Card fields fit in one byte each, reducing deck/view copies.
#include <string>

namespace poker {

// Suit values are 0..3, which also lets the evaluator select one suit mask.
enum class Suit : std::uint8_t { Hearts, Diamonds, Clubs, Spades };

// Ranks have their familiar poker values: Two = 2 through Ace = 14.
// Ace also participates in the special A-2-3-4-5 straight during evaluation.
enum class Face : std::uint8_t {
  Two = 2,
  Three,
  Four,
  Five,
  Six,
  Seven,
  Eight,
  Nine,
  Ten,
  Jack,
  Queen,
  King,
  Ace
};

// One physical card is identified by both rank and suit. Default construction
// produces the two of clubs, not an "unknown card"; card counts determine which
// entries in a hand or board array are actually dealt and safe to display.
struct Card {
  Face face = Face::Two;
  Suit suit = Suit::Clubs;
};

// Short labels for display: Ace -> "A", Hearts -> "H", and ace of hearts ->
// "AH". Conversion functions return an empty string for an unrecognized enum
// value.
std::string face_to_string(Face face);
std::string suit_to_string(Suit suit);
std::string card_to_string(const Card &card);

// Equality compares physical cards. These operators do not compare hand
// strength.
bool operator==(const Card &lhs, const Card &rhs);
bool operator!=(const Card &lhs, const Card &rhs);

} // namespace poker

#endif
