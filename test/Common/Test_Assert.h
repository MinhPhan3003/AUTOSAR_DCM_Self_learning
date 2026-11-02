/**
 * @file    Test_Assert.h
 * @brief   Minimal check/report helpers shared by all module tests.
 */
#ifndef TEST_ASSERT_H
#define TEST_ASSERT_H

#include "Std_Types.h"

void   Test_Begin(const char* Name);
void   Test_Check(boolean Condition, const char* Text);
uint32 Test_GetFailureCount(void);
void   Test_PrintSummary(void);

#endif /* TEST_ASSERT_H */
