// Formats cards for people and compares card identity without evaluating poker
// hands.

#include "card/Card.hpp"

namespace poker {

std::string face_to_string(Face face) {
  // Keep text formatting outside the simulation's hand-evaluation calculations.
  switch (face) {
  case Face::Two:
    return "2";
  case Face::Three:
    return "3";
  case Face::Four:
    return "4";
  case Face::Five:
    return "5";
  case Face::Six:
    return "6";
  case Face::Seven:
    return "7";
  case Face::Eight:
    return "8";
  case Face::Nine:
    return "9";
  case Face::Ten:
    return "10";
  case Face::Jack:
    return "J";
  case Face::Queen:
    return "Q";
  case Face::King:
    return "K";
  case Face::Ace:
    return "A";
  }

  return "";
}

std::string suit_to_string(Suit suit) {
  // Suit labels distinguish otherwise identical ranks, such as AH and AS.
  switch (suit) {
  case Suit::Clubs:
    return "C";
  case Suit::Diamonds:
    return "D";
  case Suit::Hearts:
    return "H";
  case Suit::Spades:
    return "S";
  }

  return "";
}

std::string card_to_string(const Card &card) {
  return face_to_string(card.face) + suit_to_string(card.suit);
}

bool operator==(const Card &lhs, const Card &rhs) {
  return lhs.face == rhs.face && lhs.suit == rhs.suit;
}

bool operator!=(const Card &lhs, const Card &rhs) { return !(lhs == rhs); }

} // namespace poker
