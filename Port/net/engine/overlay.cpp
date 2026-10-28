// Port/net engine (docs/netcode.md, "Playing"): what a client shows over the frame: the connection's status (the ping,
// an outage), the round's panel (warmup, countdown, results, votes) and announcements.
#include "internal.hpp"
#include "dev/dev.hpp"
#include "Source/Camera.hpp"
#include "Source/Draw.hpp"
#include "Source/Frontend.hpp"
#include "Source/gtx_0x106C.hpp"
#include <chrono>
#include <string>
#include <vector>

namespace
{
wchar_t gStatusCorner[64], gStatusCentre[64];
} // namespace

namespace
{
void Widen(const std::string& text, wchar_t* out, size_t size)
{
    size_t n = 0;
    for (; n < text.size() && n + 1 < size; n++)
    {
        out[n] = (wchar_t)(unsigned char)text[n];
    }
    out[n] = 0;
}
} // namespace

namespace
{
wchar_t gAnnouncement[128];
long long gAnnouncementUntilMs;
std::vector<std::vector<wchar_t>> gPanel; // SetNetPanel's lines, widened
long long NowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

// Over the frame for 4 s, as the netcode's other lines: the game's own message line is in the HUD, which the rollback
// restores to before the message (it showed for a frame)
void net::engine::ShowHudMessage(const char* text)
{
    Widen(text, gAnnouncement, sizeof(gAnnouncement) / sizeof(gAnnouncement[0]));
    gAnnouncementUntilMs = NowMs() + 4000;
}

void net::engine::SetNetPanel(const std::vector<std::string>& lines)
{
    gPanel.resize(lines.size());
    for (size_t i = 0; i < lines.size(); i++)
    {
        gPanel[i].assign(lines[i].size() + 1, 0);
        Widen(lines[i], gPanel[i].data(), gPanel[i].size());
    }
}

void net::engine::SetNetStatus(const std::string& corner, const std::string& centre)
{
    Widen(corner, gStatusCorner, sizeof(gStatusCorner) / sizeof(gStatusCorner[0]));
    Widen(centre, gStatusCentre, sizeof(gStatusCentre) / sizeof(gStatusCentre[0]));
}

namespace
{
// <text> centred at <y>, wrapped at spaces to fit <w> less a margin; the y under it. No std::wstring, whose
// traits assume a 4-byte wchar_t (the port has -fshort-wchar)
s32 DrawCentredWrapped(const wchar_t* text, s32 w, s32 y, s32 lineHeight)
{
    std::vector<wchar_t> line(1, 0); // 0-terminated
    size_t lineEnd = 0; // the last fitting word's end
    for (const wchar_t* c = text;; c++)
    {
        if (*c == ' ' || !*c)
        {
            if (Frontend::GetMaxTextWidth_5D8990(line.data(), gStyleTextFont_703BAA) > w - 32 && lineEnd)
            {
                // over: draw up to the last word that fitted, start again after it
                std::vector<wchar_t> rest(line.begin() + lineEnd + 1, line.end());
                line.resize(lineEnd);
                line.push_back(0);
                DrawTextScaled_5D77A0(line.data(), (w - Frontend::GetMaxTextWidth_5D8990(line.data(), gStyleTextFont_703BAA)) / 2, y, gStyleTextFont_703BAA);
                y += lineHeight;
                line = rest;
            }
            lineEnd = line.size() - 1;
            if (!*c)
            {
                break;
            }
        }
        line.insert(line.end() - 1, *c);
    }
    if (line[0])
    {
        DrawTextScaled_5D77A0(line.data(), (w - Frontend::GetMaxTextWidth_5D8990(line.data(), gStyleTextFont_703BAA)) / 2, y, gStyleTextFont_703BAA);
        y += lineHeight;
    }
    return y;
}
} // namespace

void net::engine::DrawOverlay()
{
    Camera* cam = gViewCamera_676978;
    if (!cam)
    {
        return;
    }
    // In the HUD's 640-wide units (DrawTextScaled_5D77A0 scales them as it does the HUD's)
    const float scale = cam->field_A8_ui_scale.ToFloat() > 0 ? cam->field_A8_ui_scale.ToFloat() : 1.0f;
    const s32 w = (s32)(cam->field_68_screen_px_width / scale), h = (s32)(cam->field_6C_screen_px_height / scale);
    if (gStatusCorner[0])
    {
        DrawTextScaled_5D77A0(gStatusCorner, w - 8 - Frontend::GetMaxTextWidth_5D8990(gStatusCorner, gStyleTextFont_703BAA), h - 24, gStyleTextFont_703BAA);
    }
    // The panel near the top (the HUD's score and wanted level are above it), the announcement under it
    const s32 kLine = 16;
    s32 panelY = 56;
    for (const std::vector<wchar_t>& line : gPanel)
    {
        panelY = line[0] ? DrawCentredWrapped(line.data(), w, panelY, kLine) : panelY + kLine;
    }
    if (gAnnouncement[0] && NowMs() < gAnnouncementUntilMs)
    {
        const s32 ay = gPanel.empty() ? h / 4 : (panelY + 8 > h / 4 ? panelY + 8 : h / 4);
        DrawCentredWrapped(gAnnouncement, w, ay, kLine);
    }
    if (gStatusCentre[0])
    {
        DrawTextScaled_5D77A0(gStatusCentre, (w - Frontend::GetMaxTextWidth_5D8990(gStatusCentre, gStyleTextFont_703BAA)) / 2, h / 3, gStyleTextFont_703BAA);
    }
    DebugDrawMarker(cam); // GTA2_NET_MARKER (dev/checks.cpp)
}
