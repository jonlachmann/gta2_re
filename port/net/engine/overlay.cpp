// port/net engine (docs/netcode.md, "Playing"): what a client shows over the frame: the connection's status (the ping,
// an outage), the round's panel (warmup, countdown, results, votes) and announcements.
#include "internal.hpp"
#include "dev/dev.hpp"
#include "Source/Camera.hpp"
#include "Source/Draw.hpp"
#include "Source/Frontend.hpp"
#include "Source/gtx_0x106C.hpp"
#include <algorithm>
#include <ctype.h>
#include <chrono>
#include <string>
#include <vector>

namespace
{
wchar_t gStatusCorner[64], gStatusCentre[64];
wchar_t gBanner[64];
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

void net::engine::SetNetBanner(const std::string& text)
{
    // The message font has capitals only
    std::string upper(text);
    for (char& c : upper)
    {
        c = (char)toupper((unsigned char)c);
    }
    Widen(upper, gBanner, sizeof(gBanner) / sizeof(gBanner[0]));
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
            if (Frontend::GetMaxTextWidth(line.data(), gStyleTextFont) > w - 32 && lineEnd)
            {
                // over: draw up to the last word that fitted, start again after it
                std::vector<wchar_t> rest(line.begin() + lineEnd + 1, line.end());
                line.resize(lineEnd);
                line.push_back(0);
                DrawStringScaled(line.data(), (w - Frontend::GetMaxTextWidth(line.data(), gStyleTextFont)) / 2, y, gStyleTextFont);
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
        DrawStringScaled(line.data(), (w - Frontend::GetMaxTextWidth(line.data(), gStyleTextFont)) / 2, y, gStyleTextFont);
        y += lineHeight;
    }
    return y;
}
bool IsTableRow(const std::vector<wchar_t>& line)
{
    return std::find(line.begin(), line.end(), (wchar_t)'\t') != line.end();
}

// A row's cells, split at its tabs (each 0-terminated)
std::vector<std::vector<wchar_t>> Cells(const std::vector<wchar_t>& line)
{
    std::vector<std::vector<wchar_t>> cells(1);
    for (wchar_t c : line)
    {
        if (c == (wchar_t)'\t' || !c)
        {
            cells.back().push_back(0);
            if (!c)
            {
                break;
            }
            cells.emplace_back();
        }
        else
        {
            cells.back().push_back(c);
        }
    }
    return cells;
}

bool Numeric(const std::vector<wchar_t>& cell)
{
    return cell[0] == (wchar_t)'-' ? !cell[1] : cell[0] >= (wchar_t)'0' && cell[0] <= (wchar_t)'9';
}

// gPanel's rows [first, end) as a table centred in <w>: the columns as wide as their widest cell, a column of numbers
// ("-": none) under its heading right-aligned, the others left-aligned (the font is proportional: spaces can't line them
// up); the y under it
s32 DrawTable(size_t first, size_t end, s32 w, s32 y, s32 lineHeight)
{
    const s32 kGap = 24;
    std::vector<std::vector<std::vector<wchar_t>>> rows;
    std::vector<s32> widths;
    for (size_t i = first; i < end; i++)
    {
        rows.push_back(Cells(gPanel[i]));
        for (size_t c = 0; c < rows.back().size(); c++)
        {
            const s32 cw = Frontend::GetMaxTextWidth(rows.back()[c].data(), gStyleTextFont);
            if (c >= widths.size())
            {
                widths.push_back(0);
            }
            widths[c] = cw > widths[c] ? cw : widths[c];
        }
    }
    std::vector<bool> right(widths.size(), true); // every cell under the first row numeric (or empty)
    for (size_t r = 1; r < rows.size(); r++)
    {
        for (size_t c = 0; c < rows[r].size(); c++)
        {
            right[c] = right[c] && (!rows[r][c][0] || Numeric(rows[r][c]));
        }
    }
    s32 total = 0;
    for (size_t c = 0; c < widths.size(); c++)
    {
        total += widths[c] + (c && widths[c] ? kGap : 0);
    }
    for (auto& row : rows)
    {
        s32 x = (w - total) / 2;
        for (size_t c = 0; c < row.size(); c++)
        {
            const s32 cw = Frontend::GetMaxTextWidth(row[c].data(), gStyleTextFont);
            DrawStringScaled(row[c].data(), right[c] ? x + widths[c] - cw : x, y, gStyleTextFont);
            x += widths[c] + (widths[c] ? kGap : 0);
        }
        y += lineHeight;
    }
    return y;
}
} // namespace

void net::engine::DrawOverlay()
{
    Camera* cam = gViewCamera;
    if (!cam)
    {
        return;
    }
    // In the HUD's 640-wide units (DrawStringScaled scales them as it does the HUD's)
    const float scale = cam->mUiScale.ToFloat() > 0 ? cam->mUiScale.ToFloat() : 1.0f;
    const s32 w = (s32)(cam->mScreenPxWidth / scale), h = (s32)(cam->mScreenPxHeight / scale);
    if (gStatusCorner[0])
    {
        DrawStringScaled(gStatusCorner, w - 8 - Frontend::GetMaxTextWidth(gStatusCorner, gStyleTextFont), h - 24, gStyleTextFont);
    }
    // The panel near the top (the HUD's score and wanted level are above it), the announcement under it
    const s32 kLine = 16;
    s32 panelY = 56;
    for (size_t i = 0; i < gPanel.size();)
    {
        if (IsTableRow(gPanel[i]))
        {
            size_t end = i;
            while (end < gPanel.size() && IsTableRow(gPanel[end]))
            {
                end++;
            }
            panelY = DrawTable(i, end, w, panelY, kLine);
            i = end;
            continue;
        }
        panelY = gPanel[i][0] ? DrawCentredWrapped(gPanel[i].data(), w, panelY, kLine) : panelY + kLine;
        i++;
    }
    if (gAnnouncement[0] && NowMs() < gAnnouncementUntilMs)
    {
        const s32 ay = gPanel.empty() ? h / 4 : (panelY + 8 > h / 4 ? panelY + 8 : h / 4);
        DrawCentredWrapped(gAnnouncement, w, ay, kLine);
    }
    if (gBanner[0])
    {
        const s32 bw = Frontend::GetMaxTextWidth(gBanner, gStyleMessageFont);
        DrawStringScaled(gBanner, (w - bw) / 2, h / 2 - 24, gStyleMessageFont);
    }
    if (gStatusCentre[0])
    {
        DrawStringScaled(gStatusCentre, (w - Frontend::GetMaxTextWidth(gStatusCentre, gStyleTextFont)) / 2, h / 3, gStyleTextFont);
    }
    DebugDrawMarker(cam); // GTA2_NET_MARKER (dev/checks.cpp)
}
