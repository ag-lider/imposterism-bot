#include "GameEngine.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <random>
#include <cctype>
#include <ctime>

GameEngine::GameEngine() : matchActive(false), pendingMrWhiteId(0) {}

std::string GameEngine::toLowerCase(const std::string& text) const {
    std::string result = text;
    for (char& c : result) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return result;
}

void GameEngine::resetGame() {
    players.clear();
    speakingOrder.clear();
    citizenWord.clear();
    spyWord.clear();
    matchActive = false;
    pendingMrWhiteId = 0;
}

bool GameEngine::loadCategory(const std::string& category) {
    std::string cleanCat = toLowerCase(category);
    for (char c : cleanCat) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
            return false;
        }
    }

    std::ifstream file("words/" + cleanCat + ".txt");
    if (!file.is_open()) {
        return false;
    }

    std::vector<std::pair<std::string, std::string>> wordPairs;
    std::string line;
    while (std::getline(file, line)) {
        size_t pipePos = line.find('|');
        if (pipePos != std::string::npos) {
            std::string left = line.substr(0, pipePos);
            std::string right = line.substr(pipePos + 1);
            while (!right.empty() && (right.back() == '\r' || right.back() == '\n')) {
                right.pop_back();
            }
            if (!left.empty() && !right.empty()) {
                wordPairs.push_back({left, right});
            }
        }
    }
    file.close();

    if (wordPairs.empty()) {
        return false;
    }

    std::mt19937 rng(static_cast<unsigned int>(std::time(nullptr)));
    std::uniform_int_distribution<size_t> dist(0, wordPairs.size() - 1);
    size_t idx = dist(rng);

    currentCategory = cleanCat;
    citizenWord = wordPairs[idx].first;
    spyWord = wordPairs[idx].second;
    return true;
}

bool GameEngine::addPlayer(int64_t userId, const std::string& name) {
    if (matchActive || players.size() >= 10) {
        return false;
    }
    for (const auto& p : players) {
        if (p.userId == userId) {
            return false;
        }
    }
    players.push_back({userId, name, PlayerRole::CITIZEN, true, false, 0});
    return true;
}

std::string GameEngine::getLobbyStatus() const {
    std::ostringstream oss;
    oss << "Players Joined (" << players.size() << "/10):\n";
    for (size_t i = 0; i < players.size(); ++i) {
        oss << (i + 1) << ". " << players[i].name << "\n";
    }
    if (players.size() < 3) {
        oss << "\nNeed at least " << (3 - players.size()) << " more player(s) to start.";
    } else {
        oss << "\nReady! Press 'Start Match' when everyone is in.";
    }
    return oss.str();
}
bool GameEngine::startMatch() {
    if (matchActive || players.size() < 3 || citizenWord.empty()) {
        return false;
    }

    size_t count = players.size();
    std::vector<PlayerRole> roles(count, PlayerRole::CITIZEN);

    size_t spyCount = (count >= 7) ? 2 : 1;
    for (size_t i = 0; i < spyCount; ++i) {
        roles[i] = PlayerRole::SPY;
    }
    if (count >= 4) {
        roles[spyCount] = PlayerRole::MR_WHITE;
    }

    std::random_device rd;
    std::mt19937 rng(rd());
    std::shuffle(roles.begin(), roles.end(), rng);

    speakingOrder.clear();
    for (size_t i = 0; i < count; ++i) {
        players[i].role = roles[i];
        players[i].alive = true;
        players[i].hasVoted = false;
        players[i].votesReceived = 0;
        speakingOrder.push_back(static_cast<int>(i));
    }

    std::shuffle(speakingOrder.begin(), speakingOrder.end(), rng);
    matchActive = true;
    pendingMrWhiteId = 0;
    return true;
}

std::vector<PlayerAssignment> GameEngine::getPlayerAssignments() const {
    std::vector<PlayerAssignment> list;
    for (const auto& p : players) {
        PlayerAssignment item;
        item.userId = p.userId;
        item.name = p.name;
        if (p.role == PlayerRole::CITIZEN) {
            item.secretPrompt = "Role: CITIZEN\nYour Secret Word: " + citizenWord + "\nGive a 1-word clue without making it too obvious!";
        } else if (p.role == PlayerRole::SPY) {
            item.secretPrompt = "Role: SPY\nYour Word: " + spyWord + "\nBlend in with the Citizens without getting caught!";
        } else {
            item.secretPrompt = "Role: MR. WHITE\nYou have NO word! Listen carefully to others' clues and bluff your way through.";
        }
        list.push_back(item);
    }
    return list;
}

std::string GameEngine::getSpeakingOrderText() const {
    std::ostringstream oss;
    oss << "Clue Speaking Order:\n";
    int step = 1;
    for (int idx : speakingOrder) {
        if (players[idx].alive) {
            oss << step++ << ". " << players[idx].name << " (#" << (idx + 1) << ")\n";
        }
    }
    return oss.str();
}

bool GameEngine::isMatchActive() const {
    return matchActive;
}

VoteResult GameEngine::castVote(int64_t voterId, int targetIndex) {
    VoteResult res;
    if (!matchActive || pendingMrWhiteId != 0) {
        res.feedbackMessage = "Voting is not open right now.";
        return res;
    }

    int voterIdx = -1;
    for (size_t i = 0; i < players.size(); ++i) {
        if (players[i].userId == voterId) {
            voterIdx = static_cast<int>(i);
            break;
        }
    }

    if (voterIdx == -1 || !players[voterIdx].alive) {
        res.feedbackMessage = "Only living players in this match can vote.";
        return res;
    }
    if (players[voterIdx].hasVoted) {
        res.feedbackMessage = players[voterIdx].name + ", you have already voted this round!";
        return res;
    }

    int targetIdx = targetIndex - 1;
    if (targetIdx < 0 || targetIdx >= static_cast<int>(players.size()) || !players[targetIdx].alive) {
        res.feedbackMessage = "Invalid player number. Choose a living player's number.";
        return res;
    }
    if (targetIdx == voterIdx) {
        res.feedbackMessage = "You cannot vote for yourself!";
        return res;
    }

    players[voterIdx].hasVoted = true;
    players[targetIdx].votesReceived++;

    int aliveCount = 0;
    int votedCount = 0;
    for (const auto& p : players) {
        if (p.alive) {
            aliveCount++;
            if (p.hasVoted) {
                votedCount++;
            }
        }
    }

    if (votedCount < aliveCount) {
        res.feedbackMessage = players[voterIdx].name + " voted for " + players[targetIdx].name +
                              " (" + std::to_string(votedCount) + "/" + std::to_string(aliveCount) + " votes cast).";
        return res;
    }

    int maxVotes = -1;
    std::vector<int> topCandidates;
    for (size_t i = 0; i < players.size(); ++i) {
        if (players[i].alive) {
            if (players[i].votesReceived > maxVotes) {
                maxVotes = players[i].votesReceived;
                topCandidates.clear();
                topCandidates.push_back(static_cast<int>(i));
            } else if (players[i].votesReceived == maxVotes) {
                topCandidates.push_back(static_cast<int>(i));
            }
        }
    }

    for (auto& p : players) {
        p.hasVoted = false;
        p.votesReceived = 0;
    }

    if (topCandidates.size() > 1) {
        res.feedbackMessage = "It's a tie! Everyone must revote using /vote <player_number>.";
        return res;
    }

    int eliminatedIdx = topCandidates[0];
    players[eliminatedIdx].alive = false;
    res.roundEnded = true;

    if (players[eliminatedIdx].role == PlayerRole::MR_WHITE) {
        pendingMrWhiteId = players[eliminatedIdx].userId;
        res.awaitingMrWhiteGuess = true;
        res.feedbackMessage = players[eliminatedIdx].name + " was MR. WHITE!\n" +
                              players[eliminatedIdx].name + ", type your final guess for the Citizens' word in chat right now!";
        return res;
    }

    std::string roleName = (players[eliminatedIdx].role == PlayerRole::SPY) ? "a SPY" : "a CITIZEN";
    std::string winMsg = checkWinCondition();
    if (!winMsg.empty()) {
        res.matchOver = true;
        res.feedbackMessage = players[eliminatedIdx].name + " was eliminated and was " + roleName + "!\n\n" + winMsg;
    } else {
        res.feedbackMessage = players[eliminatedIdx].name + " was eliminated and was " + roleName + "!\n\n" +
                              "The game continues!\n" + getSpeakingOrderText();
    }
    return res;
}

bool GameEngine::isAwaitingMrWhiteGuess(int64_t userId) const {
    return matchActive && pendingMrWhiteId != 0 && pendingMrWhiteId == userId;
}

std::string GameEngine::submitMrWhiteGuess(int64_t userId, const std::string& guess) {
    if (!isAwaitingMrWhiteGuess(userId)) {
        return "";
    }

    std::string cleanGuess = toLowerCase(guess);
    while (!cleanGuess.empty() && std::isspace(static_cast<unsigned char>(cleanGuess.front()))) {
        cleanGuess.erase(cleanGuess.begin());
    }
    while (!cleanGuess.empty() && std::isspace(static_cast<unsigned char>(cleanGuess.back()))) {
        cleanGuess.pop_back();
    }

    pendingMrWhiteId = 0;
    if (cleanGuess == toLowerCase(citizenWord)) {
        matchActive = false;
        updateLeaderboard({userId});
        return "Mr. White guessed '" + citizenWord + "' correctly! MR. WHITE WINS THE GAME!";
    }

    std::string winMsg = checkWinCondition();
    if (!winMsg.empty()) {
        return "Wrong guess! The Citizens' word was NOT '" + guess + "'.\n\n" + winMsg;
    }
    return "Wrong guess! Mr. White is out and the game continues!\n\n" + getSpeakingOrderText();
}

std::string GameEngine::checkWinCondition() {
    int aliveCitizens = 0;
    int aliveImpostors = 0;
    std::vector<int64_t> citizenIds;
    std::vector<int64_t> impostorIds;

    for (const auto& p : players) {
        if (p.role == PlayerRole::CITIZEN) {
            citizenIds.push_back(p.userId);
            if (p.alive) {
                aliveCitizens++;
            }
        } else {
            impostorIds.push_back(p.userId);
            if (p.alive) {
                aliveImpostors++;
            }
        }
    }

    if (aliveImpostors == 0) {
        matchActive = false;
        updateLeaderboard(citizenIds);
        return "All Spies and Mr. White have been eliminated! CITIZENS WIN!\nCitizen Word: " + citizenWord + " | Spy Word: " + spyWord;
    }

    if (aliveImpostors >= aliveCitizens) {
        matchActive = false;
        updateLeaderboard(impostorIds);
        return "Impostors equal or outnumber Citizens! IMPOSTORS WIN!\nCitizen Word: " + citizenWord + " | Spy Word: " + spyWord;
    }

    return "";
}

void GameEngine::updateLeaderboard(const std::vector<int64_t>& winnerIds) {
    std::map<std::string, int> scores;
    std::ifstream inFile("data/leaderboard.txt");
    std::string line;
    while (std::getline(inFile, line)) {
        size_t sep = line.find('|');
        if (sep != std::string::npos) {
            std::string playerName = line.substr(0, sep);
            int pts = std::stoi(line.substr(sep + 1));
            scores[playerName] = pts;
        }
    }
    inFile.close();

    for (int64_t wId : winnerIds) {
        for (const auto& p : players) {
            if (p.userId == wId) {
                scores[p.name] += 10;
            }
        }
    }

    std::ofstream outFile("data/leaderboard.txt");
    for (const auto& entry : scores) {
        outFile << entry.first << "|" << entry.second << "\n";
    }
    outFile.close();
}

std::string GameEngine::getLeaderboardText() const {
    std::ifstream inFile("data/leaderboard.txt");
    if (!inFile.is_open()) {
        return "Leaderboard is empty. Play a match to earn points!";
    }

    std::vector<std::pair<std::string, int>> entries;
    std::string line;
    while (std::getline(inFile, line)) {
        size_t sep = line.find('|');
        if (sep != std::string::npos) {
            entries.push_back({line.substr(0, sep), std::stoi(line.substr(sep + 1))});
        }
    }
    inFile.close();

    if (entries.empty()) {
        return "Leaderboard is empty. Play a match to earn points!";
    }

    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });

    std::ostringstream oss;
    oss << "All-Time Leaderboard:\n";
    for (size_t i = 0; i < entries.size(); ++i) {
        oss << (i + 1) << ". " << entries[i].first << " - " << entries[i].second << " pts\n";
    }
    return oss.str();
}
