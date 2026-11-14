#include <unity.h>

#include "request.h"
#include "runner.h"

typedef typeof(((natpmp_request_t){}).state) state_t;

static natpmp_request_t mapping(state_t state) {
  return (natpmp_request_t){
      .handle = 1,
      .opcode = NATPMP_OPCODE_MAP_TCP,
      .state = state,
      .map = {.internal_port = 0x1234,
              .external_port = 0x1111,
              .lifetime = 3600},
  };
}

static natpmp_request_t released(state_t state) {
  auto request = mapping(state);
  request.map.external_port = 0;
  request.map.lifetime = 0;
  return request;
}

static natpmp_response_t const grant = {
    .type = NATPMP_RESPONSE_MAP,
    .map = {.opcode = NATPMP_OPCODE_MAP_TCP,
            .internal_port = 0x1234,
            .external_port = 0x5678,
            .lifetime = 7200},
};

static natpmp_response_t const deletion = {
    .type = NATPMP_RESPONSE_MAP,
    .map = {.opcode = NATPMP_OPCODE_MAP_TCP, .internal_port = 0x1234},
};

static natpmp_response_t const refusal = {
    .type = NATPMP_RESPONSE_ERROR,
    .error = {.result = 2, .opcode = NATPMP_OPCODE_MAP_TCP},
};

static void test_request_matches() {
  auto const request = mapping(NATPMP_REQUEST_SENT);
  TEST_ASSERT_TRUE(natpmp_request_matches(&request, &grant));
  TEST_ASSERT_TRUE(natpmp_request_matches(&request, &refusal));

  // for another protocol or port, or the external address
  auto other = grant;
  other.map.opcode = NATPMP_OPCODE_MAP_UDP;
  TEST_ASSERT_FALSE(natpmp_request_matches(&request, &other));
  other = grant;
  other.map.internal_port = 0x1235;
  TEST_ASSERT_FALSE(natpmp_request_matches(&request, &other));
  auto const error = (natpmp_response_t){
      .type = NATPMP_RESPONSE_ERROR,
      .error = {.result = 3, .opcode = NATPMP_OPCODE_EXTERNAL_ADDRESS}};
  TEST_ASSERT_FALSE(natpmp_request_matches(&request, &error));
  auto const address = (natpmp_response_t){.type = NATPMP_RESPONSE_ADDRESS};
  TEST_ASSERT_FALSE(natpmp_request_matches(&request, &address));
}

static void test_request_released_matches_deletion() {
  // a late grant isn't the answer to a deletion
  auto const request = released(NATPMP_REQUEST_SENT);
  TEST_ASSERT_FALSE(natpmp_request_matches(&request, &grant));
  TEST_ASSERT_TRUE(natpmp_request_matches(&request, &deletion));
  TEST_ASSERT_TRUE(natpmp_request_matches(&request, &refusal));
}

static void test_request_granted() {
  auto request = mapping(NATPMP_REQUEST_SENT);
  natpmp_request_apply_answer(&request, &grant, 1000);
  TEST_ASSERT_EQUAL(NATPMP_REQUEST_ANSWERED, request.state);
  TEST_ASSERT_EQUAL_UINT16(0x5678, request.map.external_port);
  TEST_ASSERT_EQUAL_UINT32(7200, request.map.lifetime);
  // halfway through the lifetime
  TEST_ASSERT_EQUAL_UINT64(1000 + 3600 * 1000, request.map.renew_at);
  TEST_ASSERT_EQUAL_UINT64(1000 + 7200 * 1000, request.map.expires_at);
}

static void test_request_refused() {
  auto request = mapping(NATPMP_REQUEST_SENT);
  natpmp_request_apply_answer(&request, &refusal, 0);
  TEST_ASSERT_EQUAL(NATPMP_REQUEST_FAILED, request.state);

  // and a mapping for no time is no mapping
  request = mapping(NATPMP_REQUEST_SENT);
  natpmp_request_apply_answer(&request, &deletion, 0);
  TEST_ASSERT_EQUAL(NATPMP_REQUEST_FAILED, request.state);
}

static void test_request_deleted() {
  // whether the gateway deleted it or refused to
  auto request = released(NATPMP_REQUEST_SENT);
  natpmp_request_apply_answer(&request, &deletion, 0);
  TEST_ASSERT_EQUAL(NATPMP_REQUEST_FREE, request.state);
  request = released(NATPMP_REQUEST_SENT);
  natpmp_request_apply_answer(&request, &refusal, 0);
  TEST_ASSERT_EQUAL(NATPMP_REQUEST_FREE, request.state);
}

static void test_request_address_answered() {
  natpmp_request_t request = {.opcode = NATPMP_OPCODE_EXTERNAL_ADDRESS,
                              .state = NATPMP_REQUEST_SENT};
  auto const address = (natpmp_response_t){
      .type = NATPMP_RESPONSE_ADDRESS,
      .address = {.external_ip = {{203, 0, 113, 7}}},
  };
  TEST_ASSERT_TRUE(natpmp_request_matches(&request, &address));
  natpmp_request_apply_answer(&request, &address, 0);
  TEST_ASSERT_EQUAL(NATPMP_REQUEST_ANSWERED, request.state);

  auto const event = natpmp_request_take_event(&request);
  TEST_ASSERT_EQUAL(NATPMP_EVENT_ADDRESS, event.type);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(((uint8_t const[]){203, 0, 113, 7}),
                                event.address.external_ip.octets, 4);
  TEST_ASSERT_EQUAL(NATPMP_REQUEST_FREE, request.state);
}

static void test_request_address_failed() {
  natpmp_request_t request = {.opcode = NATPMP_OPCODE_EXTERNAL_ADDRESS,
                              .state = NATPMP_REQUEST_FAILED};
  auto const event = natpmp_request_take_event(&request);
  TEST_ASSERT_EQUAL(NATPMP_EVENT_ADDRESS_FAILED, event.type);
  TEST_ASSERT_EQUAL(NATPMP_REQUEST_FREE, request.state);
}

static void test_request_mapped_stays_active() {
  auto request = mapping(NATPMP_REQUEST_SENT);
  natpmp_request_apply_answer(&request, &grant, 0);

  auto const event = natpmp_request_take_event(&request);
  TEST_ASSERT_EQUAL(NATPMP_EVENT_MAPPED, event.type);
  TEST_ASSERT_EQUAL_UINT16(1, event.mapped.handle);
  TEST_ASSERT_EQUAL_UINT16(0x5678, event.mapped.external_port);
  TEST_ASSERT_EQUAL_UINT32(7200, event.mapped.lifetime);
  TEST_ASSERT_EQUAL(NATPMP_REQUEST_ACTIVE, request.state);
}

static void test_request_map_failed_frees() {
  auto request = mapping(NATPMP_REQUEST_FAILED);
  auto const event = natpmp_request_take_event(&request);
  TEST_ASSERT_EQUAL(NATPMP_EVENT_MAP_FAILED, event.type);
  TEST_ASSERT_EQUAL_UINT16(1, event.map_failed.handle);
  TEST_ASSERT_EQUAL(NATPMP_REQUEST_FREE, request.state);
}

static void test_request_fail() {
  auto request = mapping(NATPMP_REQUEST_SENT);
  natpmp_request_fail(&request);
  TEST_ASSERT_EQUAL(NATPMP_REQUEST_FAILED, request.state);

  // a released one just expires on the gateway
  request = released(NATPMP_REQUEST_QUEUED);
  natpmp_request_fail(&request);
  TEST_ASSERT_EQUAL(NATPMP_REQUEST_FREE, request.state);
}

static void test_request_fail_renewal() {
  // tries again halfway to the expiry, and again after that
  auto request = mapping(NATPMP_REQUEST_SENT);
  request.map.renew_at = 4000;
  request.map.expires_at = 8000;
  natpmp_request_fail(&request);
  TEST_ASSERT_EQUAL(NATPMP_REQUEST_ACTIVE, request.state);
  TEST_ASSERT_EQUAL_UINT64(6000, request.map.renew_at);
  natpmp_request_fail(&request);
  TEST_ASSERT_EQUAL_UINT64(7000, request.map.renew_at);

  // but a released one is still freed
  request.map.lifetime = 0;
  natpmp_request_fail(&request);
  TEST_ASSERT_EQUAL(NATPMP_REQUEST_FREE, request.state);
}

static void test_request_release_asks_for_deletion() {
  auto request = mapping(NATPMP_REQUEST_ACTIVE);
  TEST_ASSERT_TRUE(natpmp_request_release(&request));
  TEST_ASSERT_TRUE(natpmp_request_released(&request));
  TEST_ASSERT_EQUAL_UINT16(0, request.map.external_port);
  TEST_ASSERT_EQUAL_UINT32(0, request.map.lifetime);

  // and an answer not reported yet goes unreported
  request = mapping(NATPMP_REQUEST_ANSWERED);
  TEST_ASSERT_TRUE(natpmp_request_release(&request));
}

static void test_request_release_in_queue() {
  // keeps its place
  auto request = mapping(NATPMP_REQUEST_QUEUED);
  TEST_ASSERT_FALSE(natpmp_request_release(&request));
  TEST_ASSERT_EQUAL(NATPMP_REQUEST_QUEUED, request.state);
  TEST_ASSERT_TRUE(natpmp_request_released(&request));

  // still the head, and goes out again as a deletion
  request = mapping(NATPMP_REQUEST_SENT);
  TEST_ASSERT_FALSE(natpmp_request_release(&request));
  TEST_ASSERT_EQUAL(NATPMP_REQUEST_QUEUED, request.state);
}

static void test_request_release_failed() {
  // there's nothing to delete
  auto request = mapping(NATPMP_REQUEST_FAILED);
  TEST_ASSERT_FALSE(natpmp_request_release(&request));
  TEST_ASSERT_EQUAL(NATPMP_REQUEST_FREE, request.state);
}

static void test_request_release_twice() {
  auto request = released(NATPMP_REQUEST_SENT);
  TEST_ASSERT_FALSE(natpmp_request_release(&request));
  TEST_ASSERT_EQUAL(NATPMP_REQUEST_SENT, request.state);
}

void request_tests() {
  RUN_TEST(test_request_matches);
  RUN_TEST(test_request_released_matches_deletion);
  RUN_TEST(test_request_granted);
  RUN_TEST(test_request_refused);
  RUN_TEST(test_request_deleted);
  RUN_TEST(test_request_address_answered);
  RUN_TEST(test_request_address_failed);
  RUN_TEST(test_request_mapped_stays_active);
  RUN_TEST(test_request_map_failed_frees);
  RUN_TEST(test_request_fail);
  RUN_TEST(test_request_fail_renewal);
  RUN_TEST(test_request_release_asks_for_deletion);
  RUN_TEST(test_request_release_in_queue);
  RUN_TEST(test_request_release_failed);
  RUN_TEST(test_request_release_twice);
}
