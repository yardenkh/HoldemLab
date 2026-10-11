// Set up human-versus-AI play. All poker rules belong to PokerGame.
#include "decision/HumanDecisionProvider.hpp"
#include "decision/SimpleAIDecisionProvider.hpp"
#include "game/PokerGame.hpp"
#include "ui/TerminalUI.hpp"
#include <exception>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
int main() {
  try {
    // Whole-line input avoids leaving cin stuck after malformed numbers.
    int players = 0;
    for (;;) {
      std::cout << "Number of players (2.." << poker::MAX_PLAYERS
                << "): " << std::flush;
      std::string line, extra;
      if (!std::getline(std::cin, line))
        return 0;
      std::istringstream input(line);
      if ((input >> players) && !(input >> extra) && players >= 2 &&
          players <= poker::MAX_PLAYERS)
        break;
      std::cout << "Please enter a whole number from 2 to "
                << poker::MAX_PLAYERS << ".\n";
    }
    // Declare borrowed dependencies first so they remain alive until game ends.
    poker::TerminalUI ui;
    poker::HumanDecisionProvider human(ui);
    poker::SimpleAIDecisionProvider ai;
    // Fresh seeds are useful for interactive play. Simulation callers still
    // provide explicit seeds and avoid per-game entropy queries.
    poker::PokerGame game(players, poker::GameConfig{}, std::random_device{}());
    game.set_decision_provider(0, human);
    for (int id = 1; id < players; ++id)
      game.set_decision_provider(id, ai);
    game.set_observer(&ui);
    std::cout << "You are player 0.\n";
    game.run_match();
    std::cout << "Match winner: player " << game.winner_id() << '\n';
    return 0;
  } catch (const std::exception &error) {
    // Includes closed terminal input and failure to initialize lookup tables.
    std::cerr << error.what() << '\n';
    return 1;
  }
}
