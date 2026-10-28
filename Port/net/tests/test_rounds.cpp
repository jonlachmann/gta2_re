// Port/net tests: the round flow (session/round_flow.cpp) on its own: phases, ready, the frag limit, the results'
// map vote, and the votes to start, change map and kick.
#include "../session/round_flow.hpp"
#include "check.hpp"

namespace
{
const int kTps = 30;

std::vector<net::RoundPlayer> Players(int in)
{
    std::vector<net::RoundPlayer> p(6);
    for (int i = 0; i < in; i++)
    {
        p[i].in = true;
        p[i].voter = true;
        p[i].name = "P" + std::to_string(i);
    }
    return p;
}

// Ticks until the phase is <phase> (at most <max>), carrying out new rounds; the tick it got there
uint32_t RunTo(net::RoundFlow& f, uint32_t tick, uint8_t phase, const std::vector<net::RoundPlayer>& players, int max, net::RoundActions* last = nullptr)
{
    for (int i = 0; i < max && f.State().phase != phase; i++)
    {
        net::RoundActions a = f.Tick(++tick, players);
        if (last)
        {
            *last = a;
        }
    }
    CHECK(f.State().phase == phase);
    return tick;
}
} // namespace

int main()
{
    const std::vector<std::string> maps = {"mp1-6p", "mp2-6p", "downtown-6p"};
    net::RoundRules rules;
    rules.fragLimit = 3;
    rules.timeLimitMin = 1;
    rules.ticksPerSecond = kTps;

    // Warmup: one player waits; the second joins, both ready: the countdown, then the round with a new round's reset
    {
        net::RoundFlow f;
        f.Start(rules, maps, 0);
        auto players = Players(1);
        uint32_t tick = 0;
        for (int i = 0; i < 300; i++)
        {
            f.Tick(++tick, players);
        }
        CHECK(f.State().phase == net::kPhaseWarmup && f.State().phaseLeft == 0); // too few: no clock
        players = Players(2);
        f.Tick(++tick, players);
        CHECK(f.State().phase == net::kPhaseWarmup && f.State().phaseLeft == 60);
        net::RoundActions a;
        f.Command(tick, 0, "/ready", players, a);
        CHECK(f.State().ready == 1 && a.notices.size() == 1 && a.notices[0].second == "P0 is ready (1 of 2)");
        f.Command(tick, 1, "/READY", players, a);
        f.Tick(++tick, players);
        CHECK(f.State().phase == net::kPhaseCountdown && f.State().ready == 0);
        net::RoundActions go;
        tick = RunTo(f, tick, net::kPhasePlaying, players, 6 * kTps, &go);
        CHECK(go.newRound);
        // The frag limit: P1 wins, the results, nobody votes: no rotation, the same map, the countdown again
        players[1].score = 3;
        net::RoundActions end = f.Tick(++tick, players);
        CHECK(f.State().phase == net::kPhaseResults && f.State().winner == 2 && f.State().number == 2);
        CHECK(!end.notices.empty() && end.notices[0].second == "P1 wins round 1");
        players[1].score = 0;
        net::RoundActions after;
        tick = RunTo(f, tick, net::kPhaseCountdown, players, 16 * kTps, &after);
        CHECK(after.changeMap < 0);
    }
    // The warmup ends 60 s after the minimum is in, ready or not; a player leaving in the countdown: warmup again
    {
        net::RoundFlow f;
        f.Start(rules, maps, 0);
        auto players = Players(2);
        uint32_t tick = RunTo(f, 0, net::kPhaseCountdown, players, 61 * kTps);
        CHECK(tick > 59 * kTps);
        players = Players(1);
        f.Tick(++tick, players);
        CHECK(f.State().phase == net::kPhaseWarmup);
    }
    // The results' map vote: 2 for downtown, 1 for mp2: downtown, and the flow waits for the new game
    {
        net::RoundFlow f;
        f.Start(rules, maps, 0);
        auto players = Players(3);
        uint32_t tick = RunTo(f, 0, net::kPhasePlaying, players, 70 * kTps);
        players[0].score = 3;
        f.Tick(++tick, players);
        CHECK(f.State().phase == net::kPhaseResults);
        net::RoundActions a;
        f.Command(tick, 0, "/map 3", players, a);
        f.Command(tick, 1, "/map downtown", players, a);
        f.Command(tick, 2, "/map MP2-6P", players, a);
        CHECK(f.State().mapChoice[0] == 3 && f.State().mapChoice[1] == 3 && f.State().mapChoice[2] == 2);
        net::RoundActions change;
        for (int i = 0; i < 16 * kTps && change.changeMap < 0; i++)
        {
            change = f.Tick(++tick, players);
        }
        CHECK(change.changeMap == 2);
        f.Tick(++tick, players); // waiting: no phase change
        CHECK(f.State().phase == net::kPhaseResults);
        f.NewGame(++tick, 2, players);
        CHECK(f.State().phase == net::kPhaseCountdown);
    }
    // Votes: start passes with 2 of 3 (the caller and one more), kick needs a majority (non-voters are no), one at a time
    {
        net::RoundFlow f;
        f.Start(rules, maps, 0);
        auto players = Players(3);
        uint32_t tick = 1;
        f.Tick(tick, players);
        net::RoundActions a;
        f.Command(tick, 0, "/start", players, a);
        CHECK(f.State().voteKind == net::kVoteStart);
        f.Command(tick, 1, "/kick p2", players, a);
        CHECK(a.notices.back().second == "A vote is open already");
        f.Command(tick, 1, "/yes", players, a);
        CHECK(f.State().voteKind == net::kVoteNone && f.State().phase == net::kPhaseCountdown);
        net::RoundActions k;
        f.Command(tick, 0, "/kick P2", players, k);
        CHECK(f.State().voteKind == net::kVoteKick && f.State().voteArg == 2 && k.kick < 0);
        for (int i = 0; i < 21 * kTps && f.State().voteKind != net::kVoteNone; i++)
        {
            k = f.Tick(++tick, players);
        }
        CHECK(f.State().voteKind == net::kVoteNone && k.kick < 0); // 1 of 3: failed at the time
        f.Command(tick, 0, "/kick P2", players, k);
        f.Command(tick, 1, "/yes", players, k);
        CHECK(k.kick == 2);
        // A change-map vote in the round ends it without a winner
        tick = RunTo(f, tick, net::kPhasePlaying, players, 6 * kTps);
        net::RoundActions m;
        f.Command(tick, 1, "/map 2", players, m);
        f.Command(tick, 2, "/no", players, m);
        CHECK(f.State().voteKind == net::kVoteMap);
        f.Command(tick, 0, "/yes", players, m);
        CHECK(m.changeMap == 1 && f.State().winner == 0);
    }
    // Off (an arena): the round from the start, the frag limit starts the next at once
    {
        net::RoundRules off = rules;
        off.enabled = false;
        net::RoundFlow f;
        f.Start(off, maps, 0);
        auto players = Players(2);
        players[0].alwaysReady = players[1].alwaysReady = true;
        CHECK(f.State().phase == net::kPhasePlaying);
        players[0].score = 3;
        net::RoundActions a = f.Tick(1, players);
        CHECK(a.newRound && f.State().phase == net::kPhasePlaying && f.State().number == 2);
    }
    // Votes off on the server
    {
        net::RoundRules noVotes = rules;
        noVotes.votes = 0;
        net::RoundFlow f;
        f.Start(noVotes, maps, 0);
        auto players = Players(2);
        net::RoundActions a;
        f.Command(1, 0, "/start", players, a);
        CHECK(f.State().voteKind == net::kVoteNone && a.notices.back().second == "That vote is off on this server");
    }
    // Completing commands: a command, a map, a player; several that fit: as far as they agree, and the options
    {
        const std::vector<std::string> names = {"Jon", "Jonas", "Bot"};
        std::vector<std::string> options;
        CHECK(net::CompleteCommand("/r", maps, names, options) == "/ready" && options.empty());
        CHECK(net::CompleteCommand("/M", maps, names, options) == "/map ");
        CHECK(net::CompleteCommand("/map dow", maps, names, options) == "/map downtown-6p");
        CHECK(net::CompleteCommand("/map m", maps, names, options) == "/map mp" && options.size() == 2);
        CHECK(net::CompleteCommand("/kick j", maps, names, options) == "/kick Jon" && options.size() == 2);
        CHECK(net::CompleteCommand("/kick b", maps, names, options) == "/kick Bot" && options.empty());
        CHECK(net::CompleteCommand("/x", maps, names, options) == "/x" && options.empty());
        CHECK(net::CompleteCommand("hello", maps, names, options) == "hello");
        CHECK(net::CompleteCommand("/ready now", maps, names, options) == "/ready now");
    }
    printf("round flow tests passed\n");
    return 0;
}
