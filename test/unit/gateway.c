#include <unity.h>

#include "gateway.h"
#include "runner.h"

static void test_gateway_epoch_first_response() {
  natpmp_gateway_t gateway = {};

  // any epoch is fine without one to compare it to
  TEST_ASSERT_FALSE(natpmp_gateway_update_epoch(&gateway, 600'000, 0));
  TEST_ASSERT_TRUE(gateway.epoch.has_value);
  TEST_ASSERT_EQUAL_UINT32(0, gateway.epoch.seconds);
  TEST_ASSERT_EQUAL_UINT64(600'000, gateway.epoch.received);
}

static void test_gateway_epoch_keeps_pace() {
  natpmp_gateway_t gateway = {};
  TEST_ASSERT_FALSE(natpmp_gateway_update_epoch(&gateway, 0, 10));
  TEST_ASSERT_FALSE(natpmp_gateway_update_epoch(&gateway, 0, 10));
  TEST_ASSERT_FALSE(natpmp_gateway_update_epoch(&gateway, 60'000, 70));
  // ahead is fine too
  TEST_ASSERT_FALSE(natpmp_gateway_update_epoch(&gateway, 61'000, 1000));
}

static void test_gateway_epoch_tolerance() {
  natpmp_gateway_t gateway = {};
  TEST_ASSERT_FALSE(natpmp_gateway_update_epoch(&gateway, 0, 10));

  // 7/8 of 16 s after 10 s, less 2 s
  natpmp_gateway_t copy = gateway;
  TEST_ASSERT_FALSE(natpmp_gateway_update_epoch(&copy, 16'000, 22));
  copy = gateway;
  TEST_ASSERT_TRUE(natpmp_gateway_update_epoch(&copy, 16'000, 21));

  // and a millisecond later, it's too far behind
  copy = gateway;
  TEST_ASSERT_TRUE(natpmp_gateway_update_epoch(&copy, 16'001, 22));
}

static void test_gateway_epoch_from_last_response() {
  natpmp_gateway_t gateway = {};
  TEST_ASSERT_FALSE(natpmp_gateway_update_epoch(&gateway, 0, 10));
  TEST_ASSERT_FALSE(natpmp_gateway_update_epoch(&gateway, 16'000, 22));

  // 22 s plus 7/8 of the 16 s since, less 2 s
  natpmp_gateway_t copy = gateway;
  TEST_ASSERT_FALSE(natpmp_gateway_update_epoch(&copy, 32'000, 34));
  TEST_ASSERT_TRUE(natpmp_gateway_update_epoch(&gateway, 32'000, 33));

  // and a reboot becomes the new baseline
  TEST_ASSERT_EQUAL_UINT32(33, gateway.epoch.seconds);
  TEST_ASSERT_EQUAL_UINT64(32'000, gateway.epoch.received);
  TEST_ASSERT_FALSE(natpmp_gateway_update_epoch(&gateway, 33'000, 34));
}

static void test_gateway_epoch_large() {
  natpmp_gateway_t gateway = {};
  TEST_ASSERT_FALSE(natpmp_gateway_update_epoch(&gateway, 0, UINT32_MAX - 100));
  TEST_ASSERT_FALSE(natpmp_gateway_update_epoch(&gateway, 100'000, UINT32_MAX));
  TEST_ASSERT_TRUE(natpmp_gateway_update_epoch(&gateway, 200'000, 0));
}

static void test_gateway_external_ip() {
  natpmp_gateway_t gateway = {};
  natpmp_ipv4_address_t const first = {{203, 0, 113, 7}};
  natpmp_ipv4_address_t const second = {{203, 0, 113, 8}};

  // the first one is news, a repeat isn't, and a change is again
  TEST_ASSERT_TRUE(natpmp_gateway_update_external_ip(&gateway, first));
  TEST_ASSERT_FALSE(natpmp_gateway_update_external_ip(&gateway, first));
  TEST_ASSERT_TRUE(natpmp_gateway_update_external_ip(&gateway, second));
  TEST_ASSERT_TRUE(natpmp_gateway_update_external_ip(&gateway, first));
}

void gateway_tests() {
  RUN_TEST(test_gateway_epoch_first_response);
  RUN_TEST(test_gateway_epoch_keeps_pace);
  RUN_TEST(test_gateway_epoch_tolerance);
  RUN_TEST(test_gateway_epoch_from_last_response);
  RUN_TEST(test_gateway_epoch_large);
  RUN_TEST(test_gateway_external_ip);
}
