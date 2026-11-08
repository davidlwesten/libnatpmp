#pragma once

#include <unity.h>

void test_runner_run(UnityTestFunction function, const char *name, int line);
bool test_runner_init(int argc, char *argv[]);
bool test_runner_selected_test_found();

#undef RUN_TEST
#define RUN_TEST(function) test_runner_run(function, #function, __LINE__)
