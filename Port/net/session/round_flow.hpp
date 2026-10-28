// Port/net (docs/netcode.md, "Round flow"): a match's rounds, run by the server: a warmup until enough players are in
// and ready, a countdown, the round to its frag or time limit, the results with a vote for the next map; and the
// players' votes to start, change map or kick. Plain data in, actions out: it knows nothing of the game (the server
// turns the actions into engine calls), so it is tested on its own (Port/net/tests/test_rounds.cpp).
#pragma once
#include "../core/round_state.hpp"
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

namespace net
{
struct RoundRules
{
    bool enabled = true; // off (an arena of bots): the round from the start, the next one at once after it, as before
    int minPlayers = 2; // in the game for a round to start (--minplayers)
    int warmupSec = 60; // after the minimum is in, the warmup ends anyway (--warmup; 0: only when everyone is ready)
    int countdownSec = 5;
    int resultsSec = 15; // --results
    int voteSec = 20;
    unsigned votes = (1u << kVoteStart) | (1u << kVoteMap) | (1u << kVoteKick); // the vote kinds allowed (--votes)
    int gameType = 1; // 1 frags, 2 points (the score), 3 tag (time only)
    int fragLimit = 0; // frags, or points in a points game; 0 none
    int timeLimitMin = 0;
    int ticksPerSecond = 30;
    bool rotation = false; // the maps are a rotation (--maps): without votes the next one follows; else the same map again
};

// A player slot as the round sees it
struct RoundPlayer
{
    bool in = false; // in the game
    bool voter = false; // a person (an arena bot doesn't vote)
    bool alwaysReady = false; // an arena bot
    std::string name;
    int score = 0; // frags, or points in a points game
};

// What the server is to do after a tick or a command
struct RoundActions
{
    bool newRound = false; // everyone back at a restart zone with no score, weapons or power-ups (engine::NewRound)
    int changeMap = -1; // to this map (its number); the round flow waits for NewGame
    int kick = -1; // this slot out
    std::vector<std::pair<int, std::string>> notices; // (slot, line), slot -1: everyone
};

class RoundFlow
{
  public:
    // <maps>: what a vote chooses from, by name; <currentMap>: the one being played (-1: none of them)
    void Start(const RoundRules& rules, const std::vector<std::string>& maps, int currentMap);
    // A new game started (a map change): its countdown, or the warmup if too few are left
    RoundActions NewGame(uint32_t tick, int currentMap, const std::vector<RoundPlayer>& players);
    RoundActions Tick(uint32_t tick, const std::vector<RoundPlayer>& players);
    // A player's command ("/ready", "/start", "/map 2", "/kick NAME", "/yes", "/no"), at <tick>
    void Command(uint32_t tick, int slot, const std::string& text, const std::vector<RoundPlayer>& players, RoundActions& out);
    void Left(int slot); // its ready flag, votes and map choice gone
    const RoundState& State() const
    {
        return mState;
    }
    const std::vector<std::string>& Maps() const
    {
        return mMaps;
    }

  private:
    void Enter(uint32_t tick, RoundPhase phase, RoundActions& out);
    void EndRound(uint32_t tick, const std::vector<RoundPlayer>& players, RoundActions& out);
    void OpenVote(uint32_t tick, int slot, VoteKind kind, int arg, const std::vector<RoundPlayer>& players, RoundActions& out);
    void DecideVote(uint32_t tick, const std::vector<RoundPlayer>& players, RoundActions& out);
    void Carry(VoteKind kind, int arg, uint32_t tick, RoundActions& out);
    int NextMap() const; // after the results: the most chosen, else the rotation's next, else the same
    int FindMap(const std::string& arg) const; // a number (1-based) or a name; -1 none
    int Seconds(uint32_t ticks) const;

    RoundRules mRules;
    RoundState mState;
    std::vector<std::string> mMaps;
    int mCurrentMap = -1;
    uint32_t mPhaseEnd = 0; // the tick the countdown or results end (the warmup's: once the minimum is in)
    uint32_t mRoundStart = 0;
    uint32_t mVoteEnd = 0;
    bool mWaitingForMap = false; // a map change asked for: the countdown starts with the new game
};
} // namespace net
