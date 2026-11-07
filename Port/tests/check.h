// Port/tests: CHECK(x) prints the line of a failed check and counts it; main ends with return CheckReport("name").
#pragma once
#include <stdio.h>

static int gFailures;
#define CHECK(x) ((x) ? (void)0 : (void)(printf("FAILED line %d: %s\n", __LINE__, #x), gFailures++))

static int CheckReport(const char* name)
{
    if (gFailures)
    {
        printf("%s: %d failures\n", name, gFailures);
    }
    else
    {
        printf("%s: OK\n", name);
    }
    return gFailures != 0;
}
