#include <tgbot/tgbot.h>
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <mutex>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include "GameEngine.h"

struct ChatSession {
    GameEngine engine;
    int64_t lobbyMessageId = 0;
    int64_t timerMessageId = 0;
    int remainingSeconds = 0;
    bool timerRunning = false;
    bool inVotePhase = false;
};

std::map<int64_t, ChatSession> sessions;
std::mutex sessionMutex;

std::string formatTime(int totalSeconds) {
    if (totalSeconds < 0) {
        totalSeconds = 0;
    }
    int mins = totalSeconds / 60;
    int secs = totalSeconds % 60;
    std::string minStr = (mins < 10 ? "0" : "") + std::to_string(mins);
    std::string secStr = (secs < 10 ? "0" : "") + std::to_string(secs);
    return minStr + ":" + secStr;
}

int parseExtraSeconds(const std::string& rawArg) {
    std::string cleaned;
    for (char c : rawArg) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            cleaned += c;
        } else if (!std::isspace(static_cast<unsigned char>(c)) && c != 's' && c != 'S') {
            return -1;
        }
    }
    if (cleaned.empty()) {
        return -1;
    }
    try {
        int val = std::stoi(cleaned);
        if (val <= 0 || val > 600) {
            return -1;
        }
        return val;
    } catch (...) {
        return -1;
    }
}

void runCountdownTimer(TgBot::Bot& bot, int64_t chatId) {
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(5));

        int currentSecs = 0;
        int64_t msgId = 0;
        bool shouldTriggerVote = false;

        {
            std::lock_guard<std::mutex> lock(sessionMutex);
            auto it = sessions.find(chatId);
            if (it == sessions.end() || !it->second.timerRunning) {
                return;
            }

            it->second.remainingSeconds -= 5;
            currentSecs = it->second.remainingSeconds;
            msgId = it->second.timerMessageId;

            if (currentSecs <= 0) {
                it->second.remainingSeconds = 0;
                it->second.timerRunning = false;
                it->second.inVotePhase = true;
                shouldTriggerVote = true;
            }
        }

        if (shouldTriggerVote) {
            try {
                if (msgId != 0) {
                    bot.getApi().editMessageText(
                        "Time is up! 00:00 remaining.",
                        chatId,
                        msgId
                    );
                }
                bot.getApi().sendMessage(
                    chatId,
                    "Discussion time has ended! Use /vote <player_number> to eliminate a suspect."
                );
            } catch (const std::exception& e) {
                std::cerr << "Timer end API error: " << e.what() << "\n";
            }
            return;
        } else {
            try {
                if (msgId != 0) {
                    bot.getApi().editMessageText(
                        "Discussion Time Remaining: " + formatTime(currentSecs) + "\nUse /extratime <seconds> to add more time.",
                        chatId,
                        msgId
                    );
                }
            } catch (...) {
            }
        }
    }
}

TgBot::InlineKeyboardMarkup::Ptr buildLobbyKeyboard() {
    TgBot::InlineKeyboardMarkup::Ptr keyboard(new TgBot::InlineKeyboardMarkup);
    std::vector<TgBot::InlineKeyboardButton::Ptr> row;

    TgBot::InlineKeyboardButton::Ptr joinBtn(new TgBot::InlineKeyboardButton);
    joinBtn->text = "Join Game";
    joinBtn->callbackData = "join_game";

    TgBot::InlineKeyboardButton::Ptr startBtn(new TgBot::InlineKeyboardButton);
    startBtn->text = "Start Match";
    startBtn->callbackData = "start_match";

    row.push_back(joinBtn);
    row.push_back(startBtn);
    keyboard->inlineKeyboard.push_back(row);
    return keyboard;
}

int main() {
    const char* envToken = std::getenv("BOT_TOKEN");
if (envToken == nullptr || std::string(envToken).empty()) {
    std::cerr << "Error: BOT_TOKEN environment variable is not set.\n";
    return 1;
}
std::string token(envToken);


    bot.getEvents().onCommand("newgame", [&bot](TgBot::Message::Ptr message) {
        int64_t chatId = message->chat->id;
        std::string category = "general";

        std::istringstream iss(message->text);
        std::string cmd, arg;
        iss >> cmd >> arg;
        if (!arg.empty()) {
            category = arg;
        }

        std::string lobbyText;
        {
            std::lock_guard<std::mutex> lock(sessionMutex);
            ChatSession& session = sessions[chatId];
            session.timerRunning = false;
            session.inVotePhase = false;
            session.remainingSeconds = 0;
            session.engine.resetGame();

            if (!session.engine.loadCategory(category)) {
                bot.getApi().sendMessage(
                    chatId,
                    "Invalid category '" + category + "'. Available: general, food, tech, uzbek."
                );
                return;
            }

            session.engine.addPlayer(message->from->id, message->from->firstName);
            lobbyText = "New Undercover Lobby (" + category + ")\n" + session.engine.getLobbyStatus();
        }

        auto sentMsg = bot.getApi().sendMessage(
            chatId,
            lobbyText,
            false,
            0,
            buildLobbyKeyboard()
        );

        std::lock_guard<std::mutex> lock(sessionMutex);
        sessions[chatId].lobbyMessageId = sentMsg->messageId;
    });

    bot.getEvents().onCallbackQuery([&bot](TgBot::CallbackQuery::Ptr query) {
        int64_t chatId = query->message->chat->id;
        int64_t userId = query->from->id;
        std::string firstName = query->from->firstName;

        if (query->data == "join_game") {
            std::string updatedText;
            bool joined = false;
            {
                std::lock_guard<std::mutex> lock(sessionMutex);
                auto it = sessions.find(chatId);
                if (it == sessions.end()) {
                    bot.getApi().answerCallbackQuery(query->id, "No active lobby. Use /newgame first.");
                    return;
                }
                joined = it->second.engine.addPlayer(userId, firstName);
                updatedText = it->second.engine.getLobbyStatus();
            }

            if (joined) {
                bot.getApi().answerCallbackQuery(query->id, "You joined the lobby!");
                try {
                    bot.getApi().editMessageText(
                        updatedText,
                        chatId,
                        query->message->messageId,
                        "",
                        "",
                        false,
                        buildLobbyKeyboard()
                    );
                } catch (...) {
                }
            } else {
                bot.getApi().answerCallbackQuery(query->id, "Already joined or lobby full/started.");
            }
        } else if (query->data == "start_match") {
            std::vector<PlayerAssignment> assignments;
            std::string speakingOrderMsg;

            {
                std::lock_guard<std::mutex> lock(sessionMutex);
                auto it = sessions.find(chatId);
                if (it == sessions.end()) {
                    bot.getApi().answerCallbackQuery(query->id, "No active lobby.");
                    return;
                }
                if (!it->second.engine.startMatch()) {
                    bot.getApi().answerCallbackQuery(query->id, "Need at least 3 players to start!");
                    return;
                }

                assignments = it->second.engine.getPlayerAssignments();
                speakingOrderMsg = it->second.engine.getSpeakingOrderText();
                it->second.remainingSeconds = 180;
                it->second.timerRunning = true;
                it->second.inVotePhase = false;
            }

            bot.getApi().answerCallbackQuery(query->id, "Match started! Check your private messages.");

            for (const auto& p : assignments) {
                try {
                    bot.getApi().sendMessage(p.userId, p.secretPrompt);
                } catch (...) {
                    bot.getApi().sendMessage(
                        chatId,
                        "Could not DM " + p.name + ". Make sure you have started @IMPOSTERISM_BOT in private chat!"
                    );
                }
            }

            bot.getApi().sendMessage(chatId, "Match Started!\n\n" + speakingOrderMsg);

            auto timerMsg = bot.getApi().sendMessage(
                chatId,
                "Discussion Time Remaining: 03:00\nUse /extratime <seconds> to add more time."
            );

            {
                std::lock_guard<std::mutex> lock(sessionMutex);
                sessions[chatId].timerMessageId = timerMsg->messageId;
            }

            std::thread(runCountdownTimer, std::ref(bot), chatId).detach();
        }
    });

    bot.getEvents().onCommand("extratime", [&bot](TgBot::Message::Ptr message) {
        int64_t chatId = message->chat->id;
        std::string text = message->text;
        size_t spacePos = text.find(' ');

        if (spacePos == std::string::npos) {
            bot.getApi().sendMessage(chatId, "Usage: /extratime <seconds> (e.g., /extratime 28)");
            return;
        }

        int extraSecs = parseExtraSeconds(text.substr(spacePos + 1));
        if (extraSecs <= 0) {
            bot.getApi().sendMessage(chatId, "Invalid input. Enter a positive number of seconds (1 to 600).");
            return;
        }
        int newTotal = 0;
        int64_t msgId = 0;
        {
            std::lock_guard<std::mutex> lock(sessionMutex);
            auto it = sessions.find(chatId);
            if (it == sessions.end() || !it->second.timerRunning) {
                bot.getApi().sendMessage(chatId, "No active discussion timer right now.");
                return;
            }
            it->second.remainingSeconds += extraSecs;
            newTotal = it->second.remainingSeconds;
            msgId = it->second.timerMessageId;
        }

        bot.getApi().sendMessage(
            chatId,
            "Added +" + std::to_string(extraSecs) + "s! New remaining time: " + formatTime(newTotal)
        );

        if (msgId != 0) {
            try {
                bot.getApi().editMessageText(
                    "Discussion Time Remaining: " + formatTime(newTotal) + "\nUse /extratime <seconds> to add more time.",
                    chatId,
                    msgId
                );
            } catch (...) {
            }
        }
    });

    bot.getEvents().onCommand("vote", [&bot](TgBot::Message::Ptr message) {
        int64_t chatId = message->chat->id;
        int64_t voterId = message->from->id;

        std::istringstream iss(message->text);
        std::string cmd;
        int targetIndex = -1;
        if (!(iss >> cmd >> targetIndex)) {
            bot.getApi().sendMessage(chatId, "Usage: /vote <player_number>");
            return;
        }

        VoteResult result;
        {
            std::lock_guard<std::mutex> lock(sessionMutex);
            auto it = sessions.find(chatId);
            if (it == sessions.end() || !it->second.engine.isMatchActive()) {
                bot.getApi().sendMessage(chatId, "No active match to vote in.");
                return;
            }
            result = it->second.engine.castVote(voterId, targetIndex);
            if (result.roundEnded || result.matchOver) {
                it->second.timerRunning = false;
                it->second.inVotePhase = false;
            }
        }

        bot.getApi().sendMessage(chatId, result.feedbackMessage);

        if (result.roundEnded && !result.matchOver && !result.awaitingMrWhiteGuess) {
            std::lock_guard<std::mutex> lock(sessionMutex);
            auto it = sessions.find(chatId);
            if (it != sessions.end()) {
                it->second.remainingSeconds = 180;
                it->second.timerRunning = true;
                auto timerMsg = bot.getApi().sendMessage(
                    chatId,
                    "Next Round Discussion Time: 03:00\nUse /extratime <seconds> to add more time."
                );
                it->second.timerMessageId = timerMsg->messageId;
                std::thread(runCountdownTimer, std::ref(bot), chatId).detach();
            }
        }
    });

    bot.getEvents().onCommand("leaderboard", [&bot](TgBot::Message::Ptr message) {
        int64_t chatId = message->chat->id;
        std::string boardText;
        {
            std::lock_guard<std::mutex> lock(sessionMutex);
            boardText = sessions[chatId].engine.getLeaderboardText();
        }
        bot.getApi().sendMessage(chatId, boardText);
    });

    bot.getEvents().onNonCommandMessage([&bot](TgBot::Message::Ptr message) {
        int64_t chatId = message->chat->id;
        int64_t senderId = message->from->id;

        std::string mrWhiteOutcome;
        bool handled = false;
        {
            std::lock_guard<std::mutex> lock(sessionMutex);
            auto it = sessions.find(chatId);
            if (it != sessions.end() && it->second.engine.isAwaitingMrWhiteGuess(senderId)) {
                mrWhiteOutcome = it->second.engine.submitMrWhiteGuess(senderId, message->text);
                handled = true;
            }
        }

        if (handled) {
            bot.getApi().sendMessage(chatId, mrWhiteOutcome);
        }
    });

    try {
        std::cout << "Bot online: @" << bot.getApi().getMe()->username << "\n";
        TgBot::TgLongPoll longPoll(bot);
        while (true) {
            longPoll.start();
        }
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
    }

    return 0;
}
