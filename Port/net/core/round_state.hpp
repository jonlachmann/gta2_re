// Port/net core (docs/netcode.md, "Playing"): the state of a match's round as plain numbers. The server's round
// flow (session/round_flow.cpp) keeps it, the world entity carries it to the clients (engine/world_entity.cpp), and a
// client shows it. No game types: the session and the engine both use it.
#pragma once
#include <stdint.h>

namespace net
{
const int kRoundSlots = 8; // player slots the state has room for (bit masks, map choices)

enum RoundPhase : uint8_t
{
    kPhasePlaying = 0, // the round: the frag and time limits count (also the whole match when the flow is off)
    kPhaseWarmup = 1, // the map played without scores, until enough players are in and ready
    kPhaseCountdown = 2, // the seconds before the round starts
    kPhaseResults = 3, // the last round's winner and scores, and the vote for the next map
};

enum VoteKind : uint8_t
{
    kVoteNone = 0,
    kVoteStart = 1, // end the warmup
    kVoteMap = 2, // change to a map now (voteArg: its number, 0-based)
    kVoteKick = 3, // a player out (voteArg: its slot)
};

struct RoundState
{
    int number = 1; // the round being played (or next)
    int winner = 0; // the last round's: player index + 1, 0 none (a draw, or no round over yet)
    int secondsLeft = 0; // of the round, 0: no time limit
    uint8_t phase = kPhasePlaying;
    int phaseLeft = 0; // seconds left of the warmup (0: waiting for players or for everyone ready), countdown or results
    uint8_t minPlayers = 0; // for a round to start
    uint8_t ready = 0; // bit per slot: ready in the warmup
    uint8_t voteKind = kVoteNone;
    uint8_t voteArg = 0;
    uint8_t voteYes = 0, voteNo = 0; // bit per slot
    uint8_t voteLeft = 0; // seconds
    uint8_t mapChoice[kRoundSlots] = {}; // in the results: each slot's choice for the next map, 0 none, else its number + 1
};
} // namespace net
