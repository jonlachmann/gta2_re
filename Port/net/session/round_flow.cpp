// Port/net (docs/netcode.md, "Round flow"): the phases of a match's rounds and the players' votes (round_flow.hpp).
#include "round_flow.hpp"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

namespace
{
std::string Lower(std::string s)
{
    for (char& c : s)
    {
        c = (char)tolower((unsigned char)c);
    }
    return s;
}

int CountBits(unsigned v)
{
    int n = 0;
    for (; v; v &= v - 1)
    {
        n++;
    }
    return n;
}

const char* PhaseName(net::RoundPhase phase)
{
    switch (phase)
    {
        case net::kPhaseWarmup: return "warmup";
        case net::kPhaseCountdown: return "countdown";
        case net::kPhaseResults: return "results";
        default: return "round";
    }
}
} // namespace

int net::RoundFlow::Seconds(uint32_t ticks) const
{
    const int tps = mRules.ticksPerSecond > 0 ? mRules.ticksPerSecond : 30;
    return (int)((ticks + tps - 1) / tps);
}

void net::RoundFlow::Start(const RoundRules& rules, const std::vector<std::string>& maps, int currentMap)
{
    mRules = rules;
    mMaps = maps;
    mCurrentMap = currentMap;
    mState = RoundState();
    mState.minPlayers = (uint8_t)(rules.enabled ? rules.minPlayers : 0);
    mState.phase = rules.enabled ? kPhaseWarmup : kPhasePlaying;
    mPhaseEnd = 0;
    mRoundStart = 0;
}

void net::RoundFlow::Enter(uint32_t tick, RoundPhase phase, RoundActions& out)
{
    const int tps = mRules.ticksPerSecond > 0 ? mRules.ticksPerSecond : 30;
    mState.phase = phase;
    mState.ready = 0;
    if (phase == kPhaseCountdown)
    {
        mPhaseEnd = tick + (uint32_t)(mRules.countdownSec * tps);
        out.notices.push_back({-1, "Round " + std::to_string(mState.number) + " starts in " + std::to_string(mRules.countdownSec) + " s"});
    }
    else if (phase == kPhaseResults)
    {
        mPhaseEnd = tick + (uint32_t)(mRules.resultsSec * tps);
        for (uint8_t& c : mState.mapChoice)
        {
            c = 0;
        }
    }
    else if (phase == kPhasePlaying)
    {
        mRoundStart = tick;
        out.newRound = true;
        out.notices.push_back({-1, "Round " + std::to_string(mState.number) + ": go"});
    }
    else
    {
        mPhaseEnd = 0; // the warmup's clock starts once the minimum is in
        out.notices.push_back({-1, "Warmup: /ready (or F1) when you are"});
    }
    printf("net: round flow: %s (round %d)\n", PhaseName(phase), mState.number);
}

net::RoundActions net::RoundFlow::NewGame(uint32_t tick, int currentMap, const std::vector<RoundPlayer>& players)
{
    RoundActions out;
    mCurrentMap = currentMap;
    mWaitingForMap = false;
    if (!mRules.enabled)
    {
        mRoundStart = tick;
        return out;
    }
    int in = 0;
    for (const RoundPlayer& p : players)
    {
        in += p.in;
    }
    Enter(tick, in >= mRules.minPlayers ? kPhaseCountdown : kPhaseWarmup, out); // with no scores: the server's StartGameOnce
    return out;
}

void net::RoundFlow::Left(int slot)
{
    if (slot < 0 || slot >= kRoundSlots)
    {
        return;
    }
    const uint8_t bit = (uint8_t)(1u << slot);
    mState.ready &= (uint8_t)~bit;
    mState.voteYes &= (uint8_t)~bit;
    mState.voteNo &= (uint8_t)~bit;
    mState.mapChoice[slot] = 0;
    if (mState.voteKind == kVoteKick && mState.voteArg == slot)
    {
        mState.voteKind = kVoteNone; // whoever it was about is gone
    }
}

net::RoundActions net::RoundFlow::Tick(uint32_t tick, const std::vector<RoundPlayer>& players)
{
    RoundActions out;
    const int tps = mRules.ticksPerSecond > 0 ? mRules.ticksPerSecond : 30;
    int in = 0;
    bool allReady = true;
    for (size_t i = 0; i < players.size() && i < (size_t)kRoundSlots; i++)
    {
        if (players[i].in)
        {
            in++;
            allReady = allReady && (players[i].alwaysReady || (mState.ready >> i & 1));
        }
        else
        {
            Left((int)i); // gone since: its flags too
        }
    }
    if (mState.voteKind != kVoteNone)
    {
        DecideVote(tick, players, out);
    }
    if (mWaitingForMap)
    {
        return out; // the next game's NewGame goes on
    }
    switch (mState.phase)
    {
        case kPhaseWarmup:
            if (in < mRules.minPlayers || in == 0)
            {
                mPhaseEnd = 0;
                mState.phaseLeft = 0;
                break;
            }
            if (!mPhaseEnd && mRules.warmupSec > 0)
            {
                mPhaseEnd = tick + (uint32_t)(mRules.warmupSec * tps);
            }
            mState.phaseLeft = mPhaseEnd ? Seconds(mPhaseEnd > tick ? mPhaseEnd - tick : 0) : 0;
            if (allReady || (mPhaseEnd && tick >= mPhaseEnd))
            {
                Enter(tick, kPhaseCountdown, out);
            }
            break;
        case kPhaseCountdown:
            if (mRules.enabled && in < mRules.minPlayers)
            {
                out.notices.push_back({-1, "Too few players: warmup again"});
                Enter(tick, kPhaseWarmup, out);
                break;
            }
            mState.phaseLeft = Seconds(mPhaseEnd > tick ? mPhaseEnd - tick : 0);
            if (tick >= mPhaseEnd)
            {
                Enter(tick, kPhasePlaying, out);
            }
            break;
        case kPhasePlaying:
        {
            const int elapsed = (int)((tick - mRoundStart) / tps);
            mState.secondsLeft = mRules.timeLimitMin ? (mRules.timeLimitMin * 60 > elapsed ? mRules.timeLimitMin * 60 - elapsed : 0) : 0;
            int best = 0;
            for (const RoundPlayer& p : players)
            {
                best = p.in && p.score > best ? p.score : best;
            }
            const bool reached = mRules.fragLimit > 0 && mRules.gameType != 3 && best >= mRules.fragLimit;
            if (reached || (mRules.timeLimitMin && mState.secondsLeft == 0))
            {
                EndRound(tick, players, out);
            }
            break;
        }
        case kPhaseResults:
            mState.phaseLeft = Seconds(mPhaseEnd > tick ? mPhaseEnd - tick : 0);
            if (tick >= mPhaseEnd)
            {
                const int next = NextMap();
                if (next >= 0 && next != mCurrentMap)
                {
                    out.changeMap = next;
                    mWaitingForMap = true;
                    printf("net: round flow: the next map, %s\n", mMaps[next].c_str());
                }
                else
                {
                    Enter(tick, mRules.enabled && in >= mRules.minPlayers ? kPhaseCountdown : kPhaseWarmup, out);
                }
            }
            break;
    }
    return out;
}

// The leader wins; at the time limit a tie is a draw (the frag limit has one winner: the first to reach it)
void net::RoundFlow::EndRound(uint32_t tick, const std::vector<RoundPlayer>& players, RoundActions& out)
{
    int best = -1, bestScore = 0, second = -1;
    for (size_t i = 0; i < players.size(); i++)
    {
        const RoundPlayer& p = players[i];
        if (!p.in)
        {
            continue;
        }
        if (best < 0 || p.score > bestScore)
        {
            second = best < 0 ? second : bestScore; // the leader so far is the runner-up now (the first: nobody yet)
            best = (int)i;
            bestScore = p.score;
        }
        else if (p.score > second)
        {
            second = p.score;
        }
    }
    const bool reached = mRules.fragLimit > 0 && mRules.gameType != 3 && bestScore >= mRules.fragLimit;
    const bool draw = best < 0 || (!reached && bestScore == second);
    printf("net: round %d over at tick %u: %s\n", mState.number, tick,
           draw ? "a draw" : (players[best].name + " (player " + std::to_string(best) + ") wins with " + std::to_string(bestScore)).c_str());
    out.notices.push_back({-1, draw ? "Round " + std::to_string(mState.number) + " is a draw"
                                    : players[best].name + " wins round " + std::to_string(mState.number)});
    mState.winner = draw ? 0 : best + 1;
    mState.number++;
    if (!mRules.enabled)
    {
        // As before the round flow: the next round at once, or the rotation's next map
        const int next = mRules.rotation && mMaps.size() > 1 ? (mCurrentMap + 1) % (int)mMaps.size() : mCurrentMap;
        if (next != mCurrentMap)
        {
            out.changeMap = next;
            mWaitingForMap = true;
        }
        else
        {
            mRoundStart = tick;
            out.newRound = true;
        }
        return;
    }
    Enter(tick, kPhaseResults, out);
}

int net::RoundFlow::NextMap() const
{
    int votes[16] = {};
    int top = -1;
    bool tie = false;
    for (int i = 0; i < kRoundSlots; i++)
    {
        const int c = mState.mapChoice[i] - 1;
        if (c >= 0 && c < (int)mMaps.size() && c < 16)
        {
            votes[c]++;
        }
    }
    for (int m = 0; m < (int)mMaps.size() && m < 16; m++)
    {
        if (votes[m] && (top < 0 || votes[m] > votes[top]))
        {
            top = m;
            tie = false;
        }
        else if (votes[m] && votes[m] == votes[top])
        {
            tie = true;
        }
    }
    if (top >= 0 && !tie)
    {
        return top;
    }
    return mRules.rotation && !mMaps.empty() ? (mCurrentMap + 1) % (int)mMaps.size() : mCurrentMap;
}

int net::RoundFlow::FindMap(const std::string& arg) const
{
    const int n = atoi(arg.c_str());
    if (n >= 1 && n <= (int)mMaps.size())
    {
        return n - 1;
    }
    const std::string a = Lower(arg);
    for (size_t i = 0; i < mMaps.size(); i++)
    {
        const std::string m = Lower(mMaps[i]);
        if (!a.empty() && (m == a || m == a + "-6p"))
        {
            return (int)i;
        }
    }
    return -1;
}

void net::RoundFlow::Command(uint32_t tick, int slot, const std::string& text, const std::vector<RoundPlayer>& players, RoundActions& out)
{
    if (slot < 0 || slot >= kRoundSlots || slot >= (int)players.size() || !players[slot].in)
    {
        return;
    }
    std::string line = Lower(text), arg;
    const size_t space = line.find(' ');
    if (space != std::string::npos)
    {
        arg = line.substr(line.find_first_not_of(' ', space) == std::string::npos ? line.size() : line.find_first_not_of(' ', space));
        line = line.substr(0, space);
    }
    const std::string& name = players[slot].name;
    const uint8_t bit = (uint8_t)(1u << slot);
    if (line == "/ready")
    {
        if (mState.phase != kPhaseWarmup || !mRules.enabled)
        {
            out.notices.push_back({slot, "Ready counts in the warmup"});
            return;
        }
        mState.ready ^= bit;
        int in = 0, ready = 0;
        for (size_t i = 0; i < players.size() && i < (size_t)kRoundSlots; i++)
        {
            in += players[i].in;
            ready += players[i].in && (players[i].alwaysReady || (mState.ready >> i & 1));
        }
        out.notices.push_back({-1, name + ((mState.ready & bit) ? " is ready (" : " is not ready (") + std::to_string(ready) + " of " +
                                       std::to_string(in) + ")"});
        return;
    }
    if (line == "/yes" || line == "/no")
    {
        if (mState.voteKind == kVoteNone)
        {
            out.notices.push_back({slot, "No vote is open"});
            return;
        }
        mState.voteYes = (uint8_t)(line == "/yes" ? mState.voteYes | bit : mState.voteYes & ~bit);
        mState.voteNo = (uint8_t)(line == "/no" ? mState.voteNo | bit : mState.voteNo & ~bit);
        DecideVote(tick, players, out);
        return;
    }
    if (line == "/start")
    {
        if (mState.phase != kPhaseWarmup || !mRules.enabled)
        {
            out.notices.push_back({slot, "Start votes are for the warmup"});
            return;
        }
        OpenVote(tick, slot, kVoteStart, 0, players, out);
        return;
    }
    if (line == "/map")
    {
        const int m = FindMap(arg);
        if (m < 0)
        {
            std::string list;
            for (size_t i = 0; i < mMaps.size(); i++)
            {
                list += (i ? ", " : "") + std::to_string(i + 1) + " " + mMaps[i];
            }
            out.notices.push_back({slot, "Maps: " + list});
            return;
        }
        if (mState.phase == kPhaseResults && mRules.enabled)
        {
            mState.mapChoice[slot] = (uint8_t)(m + 1); // the results' choice, no vote needed
            out.notices.push_back({-1, name + " votes for " + mMaps[m]});
            return;
        }
        OpenVote(tick, slot, kVoteMap, m, players, out);
        return;
    }
    if (line == "/kick")
    {
        int target = -1;
        for (size_t i = 0; i < players.size(); i++)
        {
            target = players[i].in && players[i].voter && Lower(players[i].name) == arg ? (int)i : target;
        }
        if (target < 0 || target == slot)
        {
            out.notices.push_back({slot, "No such player to kick"});
            return;
        }
        OpenVote(tick, slot, kVoteKick, target, players, out);
        return;
    }
    out.notices.push_back({slot, "Commands: /ready, /start, /map N, /kick NAME, /yes, /no"});
}

void net::RoundFlow::OpenVote(uint32_t tick, int slot, VoteKind kind, int arg, const std::vector<RoundPlayer>& players, RoundActions& out)
{
    if (!(mRules.votes >> kind & 1))
    {
        out.notices.push_back({slot, "That vote is off on this server"});
        return;
    }
    if (mState.voteKind != kVoteNone)
    {
        out.notices.push_back({slot, "A vote is open already"});
        return;
    }
    const int tps = mRules.ticksPerSecond > 0 ? mRules.ticksPerSecond : 30;
    mState.voteKind = kind;
    mState.voteArg = (uint8_t)arg;
    mState.voteYes = (uint8_t)(1u << slot); // its starter's
    mState.voteNo = 0;
    mVoteEnd = tick + (uint32_t)(mRules.voteSec * tps);
    mState.voteLeft = (uint8_t)mRules.voteSec;
    const std::string what = kind == kVoteStart ? "start the round" : kind == kVoteMap ? "change to " + mMaps[arg] : "kick " + players[arg].name;
    out.notices.push_back({-1, players[slot].name + " calls a vote: " + what + " (Y / N)"});
    printf("net: round flow: vote %d (%d) by player %d\n", (int)kind, arg, slot);
    DecideVote(tick, players, out);
}

// A majority of the people in the game: those who don't vote count as no
void net::RoundFlow::DecideVote(uint32_t tick, const std::vector<RoundPlayer>& players, RoundActions& out)
{
    uint8_t voters = 0;
    for (size_t i = 0; i < players.size() && i < (size_t)kRoundSlots; i++)
    {
        voters |= (uint8_t)(players[i].in && players[i].voter ? 1u << i : 0);
    }
    const int n = CountBits(voters), yes = CountBits(mState.voteYes & voters), no = CountBits(mState.voteNo & voters);
    mState.voteLeft = (uint8_t)(mVoteEnd > tick ? Seconds(mVoteEnd - tick) : 0);
    const bool passed = n > 0 && yes * 2 > n;
    if (!passed && !(no * 2 >= n || tick >= mVoteEnd))
    {
        return;
    }
    const VoteKind kind = (VoteKind)mState.voteKind;
    const int arg = mState.voteArg;
    mState.voteKind = kVoteNone;
    mState.voteYes = mState.voteNo = 0;
    mState.voteLeft = 0;
    out.notices.push_back({-1, std::string(passed ? "Vote passed" : "Vote failed") + " (" + std::to_string(yes) + " of " + std::to_string(n) + ")"});
    printf("net: round flow: vote %d (%d) %s, %d of %d\n", (int)kind, arg, passed ? "passed" : "failed", yes, n);
    if (passed)
    {
        Carry(kind, arg, tick, out);
    }
}

void net::RoundFlow::Carry(VoteKind kind, int arg, uint32_t tick, RoundActions& out)
{
    if (kind == kVoteKick)
    {
        out.kick = arg;
        return;
    }
    if (kind == kVoteStart && mState.phase == kPhaseWarmup)
    {
        Enter(tick, kPhaseCountdown, out);
        return;
    }
    if (kind == kVoteMap)
    {
        if (mState.phase == kPhasePlaying)
        {
            mState.winner = 0; // the round ends without one
            mState.number++;
        }
        if (arg != mCurrentMap)
        {
            out.changeMap = arg;
            mWaitingForMap = true;
            printf("net: round flow: the next map, %s\n", mMaps[arg].c_str());
        }
        else
        {
            Enter(tick, kPhaseCountdown, out); // the same map: the round again
        }
    }
}

std::string net::CompleteCommand(const std::string& line, const std::vector<std::string>& maps, const std::vector<std::string>& names,
                                 std::vector<std::string>& options)
{
    static const std::vector<std::string> kCommands = {"/ready", "/start", "/map", "/kick", "/yes", "/no"};
    options.clear();
    if (line.empty() || line[0] != '/')
    {
        return line;
    }
    const size_t space = line.find(' ');
    const std::string command = Lower(line.substr(0, space));
    const std::vector<std::string>* words = &kCommands;
    std::string head, word = line; // what stays, what is completed
    if (space != std::string::npos)
    {
        words = command == "/map" ? &maps : command == "/kick" ? &names : NULL;
        const size_t start = line.find_first_not_of(' ', space);
        head = line.substr(0, space) + " ";
        word = start == std::string::npos ? "" : line.substr(start);
    }
    if (!words)
    {
        return line;
    }
    const std::string prefix = Lower(word);
    for (const std::string& w : *words)
    {
        if (Lower(w).compare(0, prefix.size(), prefix) == 0)
        {
            options.push_back(w);
        }
    }
    if (options.empty())
    {
        return line;
    }
    // As far as they all agree
    std::string common = options[0];
    for (const std::string& o : options)
    {
        size_t n = 0;
        while (n < common.size() && n < o.size() && tolower((unsigned char)common[n]) == tolower((unsigned char)o[n]))
        {
            n++;
        }
        common.resize(n);
    }
    if (options.size() > 1)
    {
        return common.size() > word.size() ? head + common : line;
    }
    options.clear();
    const bool takesArg = space == std::string::npos && (common == "/map" || common == "/kick");
    return head + common + (takesArg ? " " : "");
}
