#include "client_probe.hpp"
#include "../engine/dev/dev.hpp"
#include "../session/env.hpp"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void net::ClientProbe::Start()
{
    mJerk.PrintJumps(Env().jerkLog >= 2);
}

void net::ClientProbe::Frame(uint32_t frame, const std::vector<uint32_t>& remote)
{
    engine::DebugSetFrame(frame);
    engine::DebugSoundChannels(frame);
    engine::DebugAnimLog(frame);
    engine::DebugSelfLog(frame);
    if (Env().animLog && frame % 3 == 0)
    {
        printf("net: frame %u, remote buttons:", frame);
        for (size_t i = 0; i < remote.size(); i++)
        {
            printf(" 0x%x", remote[i]);
        }
        printf("\n");
    }
}

void net::ClientProbe::AfterTick()
{
    engine::DebugCheckPlayerCars("after the frame's tick");
}

void net::ClientProbe::Input(uint32_t frame, uint32_t local)
{
    if (Env().inputLog && local != mLastInput)
    {
        printf("net: frame %u, input 0x%x\n", frame, local);
        mLastInput = local;
    }
}

// GTA2_NET_SAY=<frame>:<command>[;...]: test commands, at their frame
void net::ClientProbe::Say(uint32_t frame, bool connected, const std::function<void(const std::string&)>& send)
{
    for (const char* k = Env().say; k && *k; k = strchr(k, ';'), k = k ? k + 1 : NULL)
    {
        const unsigned at = (unsigned)atoi(k);
        const char* colon = strchr(k, ':');
        const char* end = strchr(k, ';');
        if (colon && at == frame && connected)
        {
            send(std::string(colon + 1, end ? end : colon + 1 + strlen(colon + 1)));
        }
    }
}

void net::ClientProbe::Marker(const EntityState& self)
{
    if (Env().marker)
    {
        engine::SetServerMarker(self.f[0], self.f[1], self.f[2]);
    }
}

void net::ClientProbe::AfterRestore(const Snapshot& snap)
{
    engine::DebugCheckPlayerCars("after the restore");
    engine::DebugOwnCarDiff(snap);
    if (Env().compare)
    {
        engine::ComparePrediction(snap);
    }
}

bool net::ClientProbe::AfterApply(const Snapshot& snap, uint32_t ourTick)
{
    engine::DebugCheckPlayerCars("after the apply");
    const uint32_t s = snap.tick;
    if (!Env().divergeTick || s < Env().divergeTick)
    {
        return false;
    }
    engine::CompareDivergence(snap, 0); // as applied: what the snapshot didn't carry over
    engine::DumpPeds(s, false);
    mAloneFrom = s;
    engine::StartRandLog(); // from here on each tick runs once (the resimulation, then live)
    printf("net: divergence check, on our own from server tick %u (our tick %u)\n", s, ourTick);
    return true;
}

void net::ClientProbe::AfterResim()
{
    engine::DebugCheckPlayerCars("after a resimulated tick");
}

void net::ClientProbe::CompareAlone(const Snapshot& snap, engine::SavedWorld* saved, engine::SavedWorld& now)
{
    const uint32_t s = snap.tick;
    if (saved && saved->valid && saved->tick == s && s > mAloneFrom)
    {
        engine::RestoreGame(*saved);
        engine::CompareDivergence(snap, s - mAloneFrom);
        engine::DumpPeds(s, false);
        engine::RestoreGame(now);
    }
    if (s >= mAloneFrom + Env().divergeTicks)
    {
        engine::PrintDivergence();
        exit(0);
    }
}

// GTA2_NET_CORRLOG: corrections of over half a block or 45 degrees (players: a quarter block)
void net::ClientProbe::Correction(uint32_t frame, const engine::Pose& n, int32_t dx, int32_t dy, int32_t dr, const Snapshot& latest)
{
    const int32_t kFar = 2 << 14;
    if (!Env().corrLog || !(abs(dx) > 8192 || abs(dy) > 8192 || abs(dr) > 180 || (engine::IsPlayerId(n.id) && (abs(dx) > 4096 || abs(dy) > 4096))))
    {
        return;
    }
    printf("net: frame %u, correction of %u: %.2f blocks, %d degrees%s%s\n", frame, n.id, sqrtf((float)dx * dx + (float)dy * dy) / 16384.0f,
           dr / 4, abs(dx) > kFar || abs(dy) > kFar ? " (jumped)" : "",
           engine::IsPlayerId(n.id) ? ("; " + engine::DebugPlayerCar((int)n.id - 1, latest)).c_str() : "");
}

// GTA2_NET_JERKLOG: what can be seen (within 12 blocks; a camera shows 6 to 8)
void net::ClientProbe::Jerk(uint32_t frame, int slot, bool synced)
{
    if (!Env().jerkLog || !synced)
    {
        return;
    }
    std::vector<engine::Pose> all, shown;
    engine::Poses(all);
    float lx = 0, ly = 0;
    engine::PlayerPosition(slot, &lx, &ly);
    for (const engine::Pose& q : all)
    {
        const float dx = engine::Blocks(q.x) - lx, dy = engine::Blocks(q.y) - ly;
        if (dx * dx + dy * dy < 12 * 12)
        {
            shown.push_back(q);
        }
    }
    mJerk.Feed(frame, shown, (uint32_t)slot + 1);
    if (frame % 900 == 0)
    {
        mJerk.Report("client", frame);
    }
}

void net::ClientProbe::FrameEnd()
{
    engine::GangLog(false);
}

void net::ClientProbe::PrintStats(uint32_t frame)
{
    if (Env().compare)
    {
        engine::PrintPredictionStats(frame);
    }
}
