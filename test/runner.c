#include "runner.h"

#include <string.h>

static const char *selected_test;
static size_t matched_tests;

bool test_runner_init(int argc, char *argv[]) {
  if (argc > 2) {
    return false;
  }
  selected_test = argc == 2 ? argv[1] : nullptr;
  matched_tests = 0;
  return true;
}

void test_runner_run(UnityTestFunction function, const char *name, int line) {
  if (selected_test && strcmp(selected_test, name) != 0) {
    return;
  }

  ++matched_tests;
  UnityDefaultTestRun(function, name, line);
}

bool test_runner_selected_test_found() {
  return !selected_test || matched_tests == 1;
}
