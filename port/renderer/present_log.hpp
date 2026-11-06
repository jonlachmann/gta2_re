// GTA2_PRESENT_LOG=<file>: each presented frame with the time it reached the screen (Wayland's
// presentation-time protocol; elsewhere the time the swap returned), and a summary every 10 seconds.
#pragma once

struct SDL_Window;

namespace PresentLog
{
void Attach(SDL_Window* pWindow);
void BeforeSwap();
void AfterSwap();
// The next swap shows the last frame again.
void Repeat();
} // namespace PresentLog

// The game time a frame drawn by the game loop shows, in nanoseconds.
void port_PresentContent(unsigned long long gameNs);
