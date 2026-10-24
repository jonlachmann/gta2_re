// Port/net tests: a failed CHECK prints where and exits 1.
#pragma once
#include <stdio.h>
#include <stdlib.h>

#define CHECK(c) \
    do \
    { \
        if (!(c)) \
        { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            exit(1); \
        } \
    } while (0)
