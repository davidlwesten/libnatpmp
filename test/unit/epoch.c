#include <unity.h>

#include "epoch.h"
#include "runner.h"

static void test_epoch_first_response() {
  natpmp_epoch_t epoch = {};

  // any epoch is fine without one to compare it to
  TEST_ASSERT_FALSE(natpmp_epoch_update(&epoch, 600'000, 0));
  TEST_ASSERT_TRUE(epoch.has_value);
  TEST_ASSERT_EQUAL_UINT32(0, epoch.seconds);
  TEST_ASSERT_EQUAL_UINT64(600'000, epoch.received);
}

static void test_epoch_keeps_pace() {
  natpmp_epoch_t epoch = {};
  TEST_ASSERT_FALSE(natpmp_epoch_update(&epoch, 0, 10));
  TEST_ASSERT_FALSE(natpmp_epoch_update(&epoch, 0, 10));
  TEST_ASSERT_FALSE(natpmp_epoch_update(&epoch, 60'000, 70));
  // ahead is fine too
  TEST_ASSERT_FALSE(natpmp_epoch_update(&epoch, 61'000, 1000));
}

static void test_epoch_tolerance() {
  natpmp_epoch_t epoch = {};
  TEST_ASSERT_FALSE(natpmp_epoch_update(&epoch, 0, 10));

  // 7/8 of 16 s after 10 s, less 2 s
  natpmp_epoch_t copy = epoch;
  TEST_ASSERT_FALSE(natpmp_epoch_update(&copy, 16'000, 22));
  copy = epoch;
  TEST_ASSERT_TRUE(natpmp_epoch_update(&copy, 16'000, 21));

  // and a millisecond later, it's too far behind
  copy = epoch;
  TEST_ASSERT_TRUE(natpmp_epoch_update(&copy, 16'001, 22));
}

static void test_epoch_from_last_response() {
  natpmp_epoch_t epoch = {};
  TEST_ASSERT_FALSE(natpmp_epoch_update(&epoch, 0, 10));
  TEST_ASSERT_FALSE(natpmp_epoch_update(&epoch, 16'000, 22));

  // 22 s plus 7/8 of the 16 s since, less 2 s
  natpmp_epoch_t copy = epoch;
  TEST_ASSERT_FALSE(natpmp_epoch_update(&copy, 32'000, 34));
  TEST_ASSERT_TRUE(natpmp_epoch_update(&epoch, 32'000, 33));

  // and a reboot becomes the new baseline
  TEST_ASSERT_EQUAL_UINT32(33, epoch.seconds);
  TEST_ASSERT_EQUAL_UINT64(32'000, epoch.received);
  TEST_ASSERT_FALSE(natpmp_epoch_update(&epoch, 33'000, 34));
}

static void test_epoch_large() {
  natpmp_epoch_t epoch = {};
  TEST_ASSERT_FALSE(natpmp_epoch_update(&epoch, 0, UINT32_MAX - 100));
  TEST_ASSERT_FALSE(natpmp_epoch_update(&epoch, 100'000, UINT32_MAX));
  TEST_ASSERT_TRUE(natpmp_epoch_update(&epoch, 200'000, 0));
}

void epoch_tests() {
  RUN_TEST(test_epoch_first_response);
  RUN_TEST(test_epoch_keeps_pace);
  RUN_TEST(test_epoch_tolerance);
  RUN_TEST(test_epoch_from_last_response);
  RUN_TEST(test_epoch_large);
}
