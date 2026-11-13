#include <unity.h>

#include "queue.h"
#include "runner.h"

static natpmp_request_t mapping(natpmp_port_t port) {
  return (natpmp_request_t){
      .opcode = NATPMP_OPCODE_MAP_TCP,
      .map = {.internal_port = port, .lifetime = 3600},
  };
}

static natpmp_response_t grant(natpmp_port_t port) {
  return (natpmp_response_t){
      .type = NATPMP_RESPONSE_MAP,
      .map = {.opcode = NATPMP_OPCODE_MAP_TCP,
              .internal_port = port,
              .external_port = port,
              .lifetime = 3600},
  };
}

static void test_queue_in_order() {
  natpmp_queue_t queue = {};
  auto first = mapping(1);
  auto second = mapping(2);
  natpmp_queue_push(&queue, &first);
  natpmp_queue_push(&queue, &second);
  TEST_ASSERT_EQUAL(NATPMP_REQUEST_QUEUED, first.state);
  TEST_ASSERT_EQUAL(NATPMP_REQUEST_QUEUED, second.state);

  // only the head goes out
  TEST_ASSERT_EQUAL(NATPMP_RETRANSMIT_DUE, natpmp_queue_poll(&queue, 0).status);
  TEST_ASSERT_EQUAL(NATPMP_REQUEST_SENT, first.state);
  TEST_ASSERT_EQUAL(NATPMP_REQUEST_QUEUED, second.state);

  auto const response = grant(1);
  natpmp_queue_on_response(&queue, 10, &response);
  TEST_ASSERT_EQUAL(NATPMP_REQUEST_ANSWERED, first.state);
  TEST_ASSERT_EQUAL_PTR(&second, queue.head);
}

static void test_queue_writes_head() {
  natpmp_queue_t queue = {};
  auto request = mapping(0x1234);
  natpmp_queue_push(&queue, &request);

  auto const status = natpmp_queue_poll(&queue, 0);
  TEST_ASSERT_EQUAL(NATPMP_RETRANSMIT_DUE, status.status);
  uint8_t const expected[] = {
      0,    2,    0,    0,    // version, MAP_TCP, reserved
      0x12, 0x34, 0,    0,    // internal port, any external port
      0,    0,    0x0E, 0x10, // lifetime
  };
  TEST_ASSERT_EQUAL_size_t(sizeof(expected), status.size);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, queue.data, sizeof(expected));

  // and waits for its answer
  TEST_ASSERT_EQUAL(NATPMP_RETRANSMIT_WAIT,
                    natpmp_queue_poll(&queue, 249).status);
  TEST_ASSERT_EQUAL(NATPMP_RETRANSMIT_DUE,
                    natpmp_queue_poll(&queue, 250).status);
}

static void test_queue_new_head_starts_over() {
  natpmp_queue_t queue = {};
  auto first = mapping(1);
  auto second = mapping(2);
  natpmp_queue_push(&queue, &first);
  natpmp_queue_push(&queue, &second);
  natpmp_queue_poll(&queue, 0);
  natpmp_queue_poll(&queue, 250);

  // goes out right away, then waits 250 ms like the first did
  auto const response = grant(1);
  natpmp_queue_on_response(&queue, 300, &response);
  TEST_ASSERT_EQUAL(NATPMP_RETRANSMIT_DUE,
                    natpmp_queue_poll(&queue, 300).status);
  TEST_ASSERT_EQUAL_UINT64(550, queue.retransmit.next);
}

static void test_queue_ignores_other_responses() {
  natpmp_queue_t queue = {};
  auto request = mapping(1);
  natpmp_queue_push(&queue, &request);

  // nothing sent yet
  auto const response = grant(1);
  natpmp_queue_on_response(&queue, 0, &response);
  TEST_ASSERT_EQUAL(NATPMP_REQUEST_QUEUED, request.state);

  // or for another request
  natpmp_queue_poll(&queue, 0);
  auto const other = grant(2);
  natpmp_queue_on_response(&queue, 0, &other);
  TEST_ASSERT_EQUAL(NATPMP_REQUEST_SENT, request.state);
  TEST_ASSERT_EQUAL_PTR(&request, queue.head);
}

static void test_queue_pushed_again() {
  natpmp_queue_t queue = {};
  auto first = mapping(1);
  auto second = mapping(2);
  natpmp_queue_push(&queue, &first);
  natpmp_queue_push(&queue, &second);
  auto const first_grant = grant(1);
  auto const second_grant = grant(2);
  natpmp_queue_poll(&queue, 0);
  natpmp_queue_on_response(&queue, 0, &first_grant);
  natpmp_queue_poll(&queue, 0);
  natpmp_queue_on_response(&queue, 0, &second_grant);

  // e.g. for a renewal, without what was behind it the last time
  natpmp_queue_push(&queue, &first);
  natpmp_queue_poll(&queue, 0);
  natpmp_queue_on_response(&queue, 0, &first_grant);
  TEST_ASSERT_NULL(queue.head);
}

static void test_queue_fail_all() {
  natpmp_queue_t queue = {};
  auto first = mapping(1);
  auto second = mapping(2);
  auto released = mapping(3);
  released.map.lifetime = 0;
  natpmp_queue_push(&queue, &first);
  natpmp_queue_push(&queue, &second);
  natpmp_queue_push(&queue, &released);
  natpmp_queue_poll(&queue, 0);

  natpmp_queue_fail_all(&queue);
  TEST_ASSERT_NULL(queue.head);
  TEST_ASSERT_EQUAL(NATPMP_REQUEST_FAILED, first.state);
  TEST_ASSERT_EQUAL(NATPMP_REQUEST_FAILED, second.state);
  TEST_ASSERT_EQUAL(NATPMP_REQUEST_FREE, released.state);
}

void queue_tests() {
  RUN_TEST(test_queue_in_order);
  RUN_TEST(test_queue_writes_head);
  RUN_TEST(test_queue_new_head_starts_over);
  RUN_TEST(test_queue_ignores_other_responses);
  RUN_TEST(test_queue_pushed_again);
  RUN_TEST(test_queue_fail_all);
}
