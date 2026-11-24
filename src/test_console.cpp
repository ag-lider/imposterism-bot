#include <iostream>
#include <cassert>
#include <string>
#include "GameEngine.h"

void runAutomatedTests() {
    GameEngine engine;

    assert(engine.loadCategory("invalid_category") == false);
    assert(engine.loadCategory("food") == true);

    assert(engine.startMatch() == false);

    assert(engine.addPlayer(101, "Ali") == true);
    assert(engine.addPlayer(101, "Ali") == false);
    assert(engine.addPlayer(102, "Vali") == true);
    assert(engine.startMatch() == false);

    assert(engine.addPlayer(103, "Hasan") == true);
    assert(engine.addPlayer(104, "Husan") == true);
    assert(engine.startMatch() == true);

    auto assignments = engine.getPlayerAssignments();
    assert(assignments.size() == 4);

    VoteResult selfVote = engine.castVote(101, 1);
    assert(selfVote.roundEnded == false);

    VoteResult invalidVote = engine.castVote(101, 99);
    assert(invalidVote.roundEnded == false);

    std::cout << "All automated unit tests passed successfully!\n\n";
}

int main() {
    runAutomatedTests();

    GameEngine game;
    std::string category;
    std::cout << "Choose category (general, food, tech, uzbek): ";
    std::cin >> category;

    if (!game.loadCategory(category)) {
        std::cout << "Failed to load category. Make sure you run from the TGP root folder.\n";
        return 1;
    }

    int playerCount = 0;
    std::cout << "Enter number of players (3-10): ";
    std::cin >> playerCount;
    if (playerCount < 3 || playerCount > 10) {
        std::cout << "Invalid player count.\n";
        return 1;
    }

    for (int i = 0; i < playerCount; ++i) {
        std::string name;
        std::cout << "Enter name for Player " << (i + 1) << ": ";
        std::cin >> name;
        game.addPlayer(1000 + i, name);
    }

    game.startMatch();
    std::cout << "\n--- SECRET ROLES & WORDS ---\n";
    for (const auto& p : game.getPlayerAssignments()) {
        std::cout << "[" << p.name << "] -> " << p.secretPrompt << "\n\n";
    }

    std::cout << game.getSpeakingOrderText() << "\n";

    while (game.isMatchActive()) {
        for (int i = 0; i < playerCount; ++i) {
            if (!game.isMatchActive()) {
                break;
            }
            int64_t voterId = 1000 + i;
            if (game.isAwaitingMrWhiteGuess(voterId)) {
                std::string guess;
                std::cout << "Mr. White, enter your word guess: ";
                std::cin >> guess;
                std::cout << game.submitMrWhiteGuess(voterId, guess) << "\n\n";
                break;
            }
            int target = 0;
            std::cout << "Player #" << (i + 1) << ", vote for player number (1-" << playerCount << "): ";
            std::cin >> target;
            VoteResult res = game.castVote(voterId, target);
            std::cout << res.feedbackMessage << "\n";
            if (res.awaitingMrWhiteGuess) {
                std::string guess;
                std::cout << "Mr. White, enter your final word guess: ";
                std::cin >> guess;
                std::cout << game.submitMrWhiteGuess(voterId, guess) << "\n\n";
            }
            if (res.roundEnded || res.matchOver) {
                break;
            }
        }
    }

    std::cout << "\n" << game.getLeaderboardText() << "\n";
    return 0;
}
