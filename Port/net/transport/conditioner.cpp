// Port/net transport (docs/netcode.md, N3): a link conditioner for tests. It wraps a transport and holds every
// send back by a delay with jitter, and drops some unreliable ones. On both ends, a round trip gets twice the delay.
#include "transport.hpp"
#include "../core/protocol.hpp"
#include <chrono>
#include <deque>

namespace
{
class Conditioner : public net::Transport
{
  public:
    Conditioner(net::Transport* inner, int delayMs, int jitterMs, int lossPercent)
        : mInner(inner), mDelayMs(delayMs), mJitterMs(jitterMs), mLoss(lossPercent)
    {
    }
    ~Conditioner() override
    {
        delete mInner;
    }

    bool Poll(net::TransportEvent& event) override
    {
        Release();
        return mInner->Poll(event);
    }

    bool Send(uint32_t peer, uint8_t channel, const void* data, size_t size, bool reliable) override
    {
        if (!reliable && size > net::kMaxPayload)
        {
            return false; // the same refusal as the real one
        }
        if (!reliable && (int)(Next() % 100) < mLoss)
        {
            return true; // lost on the way
        }
        int delay = mDelayMs;
        if (mJitterMs > 0)
        {
            delay += (int)(Next() % (2 * mJitterMs + 1)) - mJitterMs;
        }
        Held h;
        h.due = Now() + (delay > 0 ? delay : 0);
        if (!mHeld.empty() && h.due < mHeld.back().due)
        {
            h.due = mHeld.back().due; // One queue in order, so jitter never reorders (ENet's sequencing would drop them)
        }
        h.peer = peer;
        h.channel = channel;
        h.reliable = reliable;
        h.data.assign((const uint8_t*)data, (const uint8_t*)data + size);
        mHeld.push_back(h);
        return true;
    }

    void Flush() override
    {
        Release();
    }

    void Disconnect(uint32_t peer) override
    {
        mInner->Disconnect(peer);
    }

    uint32_t RoundTripMs(uint32_t peer) const override
    {
        return mInner->RoundTripMs(peer); // includes our delay: ENet measures it on the wire
    }

  private:
    struct Held
    {
        long long due;
        uint32_t peer;
        uint8_t channel;
        bool reliable;
        std::vector<uint8_t> data;
    };

    static long long Now()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    uint32_t Next()
    {
        mSeed = mSeed * 1103515245u + 12345u;
        return mSeed >> 8;
    }

    void Release()
    {
        const long long now = Now();
        bool any = false;
        while (!mHeld.empty() && mHeld.front().due <= now)
        {
            const Held& h = mHeld.front();
            mInner->Send(h.peer, h.channel, h.data.data(), h.data.size(), h.reliable);
            mHeld.pop_front();
            any = true;
        }
        if (any)
        {
            mInner->Flush();
        }
    }

    net::Transport* mInner;
    int mDelayMs, mJitterMs, mLoss;
    uint32_t mSeed = 12345;
    std::deque<Held> mHeld;
};
} // namespace

net::Transport* net::CreateConditioner(Transport* inner, int delayMs, int jitterMs, int lossPercent)
{
    return new Conditioner(inner, delayMs, jitterMs, lossPercent);
}
