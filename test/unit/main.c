#include "runner.h"

void setUp() {}
void tearDown() {}

extern void gateway_tests();
extern void packet_tests();
extern void queue_tests();
extern void request_tests();
extern void retransmit_tests();
extern void session_tests();

int main(int argc, char *argv[]) {
  if (!test_runner_init(argc, argv)) {
    return 2;
  }

  UNITY_BEGIN();

  gateway_tests();
  packet_tests();
  queue_tests();
  request_tests();
  retransmit_tests();
  session_tests();

  int const result = UNITY_END();
  return test_runner_selected_test_found() ? result : 2;
}
