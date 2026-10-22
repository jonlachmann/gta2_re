// Port (docs/port.md): the per-frame state log, see state_log.cpp.
#pragma once

void StateLog_Init(); // reads GTA2_STATELOG*; does nothing without it
void StateLog_Frame(unsigned flip); // at every Vid_FlipBuffers
