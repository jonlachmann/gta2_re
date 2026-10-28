// Port/sdl_miles.cpp check: the mixer's volume, pan, loops, loop block and playback rate (ctest, Port/CMakeLists.txt).
#include "Source/3rdParty/mss.h"
#include <math.h>
#include <stdio.h>

void Port_MilesMix(float* out, int frames);

static int gFailures;
#define CHECK(x) ((x) ? (void)0 : (void)(printf("FAILED line %d: %s\n", __LINE__, #x), gFailures++))
#define NEAR(a, b) (fabsf((a) - (b)) < 0.01f)

int main(int, char**)
{
    AIL_startup();
    HSAMPLE s = AIL_allocate_sample_handle(0);
    AIL_init_sample(s);
    unsigned char data[4] = {192, 192, 64, 64}; // unsigned 8-bit: +0.5, +0.5, -0.5, -0.5
    AIL_set_sample_type(s, DIG_F_MONO_8, 0);
    AIL_set_sample_address(s, data, 4);
    AIL_set_sample_playback_rate(s, 44100);

    float out[2 * 16];
    AIL_start_sample(s); // centre, full volume, played once
    Port_MilesMix(out, 6);
    CHECK(NEAR(out[0], 0.5f) && NEAR(out[1], 0.5f) && NEAR(out[4], -0.5f) && NEAR(out[5], -0.5f));
    CHECK(out[8] == 0.0f && out[10] == 0.0f); // finished after 4 frames
    CHECK(AIL_sample_status(s) == SMP_DONE);

    AIL_set_sample_pan(s, 0); // hard left
    AIL_set_sample_volume(s, 64); // about half
    AIL_start_sample(s);
    Port_MilesMix(out, 1);
    CHECK(NEAR(out[0], 0.5f * 64 / 127) && out[1] == 0.0f);

    AIL_set_sample_pan(s, 64);
    AIL_set_sample_volume(s, 127);
    AIL_set_sample_loop_count(s, 2); // twice: 8 frames
    AIL_start_sample(s);
    Port_MilesMix(out, 10);
    CHECK(NEAR(out[2 * 7], -0.5f) && out[2 * 8] == 0.0f);

    AIL_set_sample_loop_count(s, 0); // forever, looping frames 2..3 after the first pass
    AIL_set_sample_loop_block(s, 2, 4);
    AIL_start_sample(s);
    Port_MilesMix(out, 16);
    CHECK(NEAR(out[2 * 4], -0.5f) && NEAR(out[2 * 15], -0.5f) && AIL_sample_status(s) == SMP_PLAYING);

    AIL_end_sample(s);
    AIL_set_sample_loop_count(s, 1);
    AIL_set_sample_loop_block(s, 0, 0);
    AIL_set_sample_playback_rate(s, 22050); // half speed: each frame twice
    AIL_start_sample(s);
    Port_MilesMix(out, 8);
    CHECK(NEAR(out[2 * 3], 0.5f) && NEAR(out[2 * 4], -0.5f) && NEAR(out[2 * 7], -0.5f));

    AIL_release_sample_handle(s);
    printf(gFailures ? "sdl_miles_test: %d failures\n" : "sdl_miles_test: OK\n", gFailures);
    return gFailures != 0;
}
