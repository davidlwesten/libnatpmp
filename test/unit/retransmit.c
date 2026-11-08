#include <unity.h>

#include "retransmit.h"
#include "runner.h"

static void test_retransmit_first_due_right_away() {
  natpmp_retransmit_t const retransmit = {};
  TEST_ASSERT_EQUAL(NATPMP_RETRANSMIT_DUE,
                    natpmp_retransmit_poll(&retransmit, 0));
  TEST_ASSERT_EQUAL(NATPMP_RETRANSMIT_DUE,
                    natpmp_retransmit_poll(&retransmit, 123456));
}

static void test_retransmit_doubles_from_250ms() {
  natpmp_retransmit_t retransmit = {};
  natpmp_time_ms_t now = 1000;
  natpmp_time_ms_t wait = 250;

  // nine attempts, the last one 32 s after the eighth
  for (int i = 0; i < 9; ++i) {
    TEST_ASSERT_EQUAL(NATPMP_RETRANSMIT_DUE,
                      natpmp_retransmit_poll(&retransmit, now));
    natpmp_retransmit_advance(&retransmit, now);
    TEST_ASSERT_EQUAL(NATPMP_RETRANSMIT_WAIT,
                      natpmp_retransmit_poll(&retransmit, now + wait - 1));
    now += wait;
    wait *= 2;
  }
}

static void test_retransmit_gives_up_64s_after_ninth() {
  natpmp_retransmit_t retransmit = {};
  natpmp_time_ms_t now = 0;
  for (int i = 0; i < 9; ++i) {
    now = retransmit.next;
    natpmp_retransmit_advance(&retransmit, now);
  }

  TEST_ASSERT_EQUAL_UINT64(now + 64000, retransmit.next);
  TEST_ASSERT_EQUAL(NATPMP_RETRANSMIT_WAIT,
                    natpmp_retransmit_poll(&retransmit, now + 63999));
  TEST_ASSERT_EQUAL(NATPMP_RETRANSMIT_GAVE_UP,
                    natpmp_retransmit_poll(&retransmit, now + 64000));
}

static void test_retransmit_waits_from_actual_send() {
  natpmp_retransmit_t retransmit = {};
  natpmp_retransmit_advance(&retransmit, 0);

  // sent late, e.g. after the client was busy, so the next wait starts then
  natpmp_retransmit_advance(&retransmit, 2000);
  TEST_ASSERT_EQUAL_UINT64(2500, retransmit.next);
}

void retransmit_tests() {
  RUN_TEST(test_retransmit_first_due_right_away);
  RUN_TEST(test_retransmit_doubles_from_250ms);
  RUN_TEST(test_retransmit_gives_up_64s_after_ninth);
  RUN_TEST(test_retransmit_waits_from_actual_send);
}
