// Port (docs/port.md, "Validation"): the checks against 10.5.exe and the test aids, linked into the game and off
// unless their environment variables are set. The platform layer calls them here and knows nothing else of them.
#pragma once

void Validation_Start(); // before the game starts: GTA2_SPEED (Windows), GTA2_PORT_ATTRACT_MS
void Validation_VideoInit(); // the video is set up (Vid_Init_SYS): GTA2_STATELOG
void Validation_Frame(unsigned flip); // every Vid_FlipBuffers, <flip> counting from 1
