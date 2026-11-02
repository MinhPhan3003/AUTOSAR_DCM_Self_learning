/**
 * @file    Test_Assert.c
 * @brief   Minimal check/report helpers shared by all module tests.
 */
#include <stdio.h>
#include "Test_Assert.h"

static uint32 Test_Checks   = 0u;
static uint32 Test_Failures = 0u;

void Test_Begin(const char* Name)
{
    printf("[TEST] %s\n", Name);
}

void Test_Check(boolean Condition, const char* Text)
{
    Test_Checks++;
    if (Condition == FALSE)
    {
        Test_Failures++;
        printf("    FAIL: %s\n", Text);
    }
}

uint32 Test_GetFailureCount(void)
{
    return Test_Failures;
}

void Test_PrintSummary(void)
{
    printf("\n%lu checks, %lu failures\n", (unsigned long)Test_Checks, (unsigned long)Test_Failures);
}
