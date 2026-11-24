#ifndef GAME_ENGINE_H
#define GAME_ENGINE_H

#include <string>
#include <vector>
#include <map>
#include <cstdint>

enum class PlayerRole {
    CITIZEN,
    SPY,
    MR_WHITE
};

struct Player {
    int64_t userId;
    std::string name;
    PlayerRole role;
    bool alive;
    bool hasVoted;
    int votesReceived;
};

struct PlayerAssignment {
    int64_t userId;
    std::string name;
    std::string secretPrompt;
};

struct VoteResult {
    bool roundEnded = false;
    bool matchOver = false;
    bool awaitingMrWhiteGuess = false;
    std::string feedbackMessage;
};

class GameEngine {
private:
    std::vector<Player> players;
    std::vector<int> speakingOrder;
    std::string currentCategory;
    std::string citizenWord;
    std::string spyWord;
    bool matchActive;
    int64_t pendingMrWhiteId;

    std::string toLowerCase(const std::string& text) const;
    void updateLeaderboard(const std::vector<int64_t>& winnerIds);
    std::string checkWinCondition();

public:
    GameEngine();

    void resetGame();
    bool loadCategory(const std::string& category);
    bool addPlayer(int64_t userId, const std::string& name);
    std::string getLobbyStatus() const;

    bool startMatch();
    std::vector<PlayerAssignment> getPlayerAssignments() const;
    std::string getSpeakingOrderText() const;

    bool isMatchActive() const;
    VoteResult castVote(int64_t voterId, int targetIndex);

    bool isAwaitingMrWhiteGuess(int64_t userId) const;
    std::string submitMrWhiteGuess(int64_t userId, const std::string& guess);

    std::string getLeaderboardText() const;
};

#endif
