#include <unity.h>

#include <byo/natpmp/natpmp.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "runner.h"

static natpmp_session_t *session;

static void begin() {
  session = natpmp_session_begin((natpmp_config_t){});
  TEST_ASSERT_NOT_NULL(session);
}

static natpmp_handle_t request() {
  auto const handle = natpmp_session_address_request(session);
  TEST_ASSERT_TRUE(handle.has_value);
  return handle.value;
}

static natpmp_handle_t map(natpmp_protocol_t protocol) {
  auto const handle =
      natpmp_session_map_request(session, 3600, protocol, 0x1234, 0x1111);
  TEST_ASSERT_TRUE(handle.has_value);
  return handle.value;
}

static void receive(size_t size, uint8_t const data[]) {
  natpmp_session_on_datagram(session, size, data);
}

static uint8_t const address_request[] = {0, 0};

static uint8_t const address_response[] = {
    0,   128, 0,   0,  // version, R + EXTERNAL_ADDRESS, SUCCESS
    0,   0,   0,   10, // epoch
    203, 0,   113, 7,
};

static uint8_t const address_error[] = {
    0, 128, 0, 3, // version, R + EXTERNAL_ADDRESS, NETWORK_FAILURE
    0, 0,   0, 10, 0, 0, 0, 0,
};

static uint8_t const map_tcp_request[] = {
    0,    2,    0,    0,    // version, MAP_TCP, reserved
    0x12, 0x34, 0x11, 0x11, // internal port, suggested external port
    0,    0,    0x0E, 0x10, // lifetime
};

static uint8_t const map_response[] = {
    0,    130,  0,    0,    // version, R + MAP_TCP, SUCCESS
    0,    0,    0,    10,   // epoch
    0x12, 0x34, 0x56, 0x78, // internal port, external port
    0,    0,    0x1C, 0x20, // lifetime
};

static uint8_t const map_error[] = {
    0, 130, 0, 2, // version, R + MAP_TCP, NOT_AUTHORIZED
    0, 0,   0, 10, 0x12, 0x34, 0, 0, 0, 0, 0, 0,
};

static void
expect_sent(natpmp_time_ms_t now, size_t size, uint8_t const data[]) {
  auto const event = natpmp_session_poll(session, now);
  TEST_ASSERT_EQUAL(NATPMP_EVENT_SEND, event.type);
  TEST_ASSERT_EQUAL_size_t(size, event.send.size);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(data, event.send.data, size);
}

static void expect_send(natpmp_time_ms_t now) {
  expect_sent(now, sizeof(address_request), address_request);
}

static void expect_timeout(natpmp_time_ms_t now, natpmp_time_ms_t deadline) {
  auto const event = natpmp_session_poll(session, now);
  TEST_ASSERT_EQUAL(NATPMP_EVENT_TIMEOUT, event.type);
  TEST_ASSERT_TRUE(event.timeout.deadline.has_value);
  TEST_ASSERT_EQUAL_UINT64(deadline, event.timeout.deadline.value);
}

static void expect_idle(natpmp_time_ms_t now) {
  auto const event = natpmp_session_poll(session, now);
  TEST_ASSERT_EQUAL(NATPMP_EVENT_TIMEOUT, event.type);
  TEST_ASSERT_FALSE(event.timeout.deadline.has_value);
}

static void expect_address(natpmp_time_ms_t now, natpmp_handle_t handle) {
  auto const event = natpmp_session_poll(session, now);
  TEST_ASSERT_EQUAL(NATPMP_EVENT_ADDRESS, event.type);
  TEST_ASSERT_EQUAL_UINT16(handle, event.address.handle);
  uint8_t const external_ip[] = {203, 0, 113, 7};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(external_ip, event.address.external_ip.octets,
                                sizeof(external_ip));
}

static void expect_mapped(natpmp_time_ms_t now, natpmp_handle_t handle) {
  auto const event = natpmp_session_poll(session, now);
  TEST_ASSERT_EQUAL(NATPMP_EVENT_MAPPED, event.type);
  TEST_ASSERT_EQUAL_UINT16(handle, event.mapped.handle);
  // what the gateway granted, not what was asked for
  TEST_ASSERT_EQUAL_UINT16(0x5678, event.mapped.external_port);
  TEST_ASSERT_EQUAL_UINT32(7200, event.mapped.lifetime);
}

static void expect_failed(natpmp_time_ms_t now, natpmp_handle_t handle) {
  auto const event = natpmp_session_poll(session, now);
  TEST_ASSERT_EQUAL(NATPMP_EVENT_FAILED, event.type);
  TEST_ASSERT_EQUAL_UINT16(handle, event.failed.handle);
}

// sends the request at the head of the queue nine times, without an answer
static natpmp_time_ms_t unanswered(natpmp_time_ms_t now) {
  for (int i = 0; i < 9; ++i) {
    expect_send(now);
    now += 250 << i;
  }
  return now;
}

static void test_session_nothing_requested() {
  begin();
  expect_idle(0);
  natpmp_session_end(session);
}

static void test_session_address() {
  begin();
  auto const handle = request();
  expect_send(1000);
  expect_timeout(1000, 1250);
  receive(sizeof(address_response), address_response);

  expect_address(1010, handle);
  expect_idle(1010);
  natpmp_session_end(session);
}

static void test_session_one_at_a_time() {
  begin();
  auto const first = request();
  auto const second = request();
  TEST_ASSERT_NOT_EQUAL(first, second);

  // the second waits for the first
  expect_send(0);
  expect_timeout(0, 250);
  receive(sizeof(address_response), address_response);
  expect_address(10, first);

  // and gets its own request, from scratch
  expect_send(20);
  expect_timeout(20, 270);
  receive(sizeof(address_response), address_response);
  expect_address(30, second);
  expect_idle(30);
  natpmp_session_end(session);
}

static void test_session_retransmits() {
  begin();
  request();

  // right away, then after 250 ms, doubling every time
  natpmp_time_ms_t now = 0;
  natpmp_time_ms_t wait = 250;
  for (int i = 0; i < 9; ++i) {
    expect_send(now);
    expect_timeout(now, now + wait);
    expect_timeout(now + wait - 1, now + wait);
    now += wait;
    wait *= 2;
  }
  natpmp_session_end(session);
}

static void test_session_fails_unanswered() {
  begin();
  auto const handle = request();
  auto const now = unanswered(0);

  // 64 s after the ninth
  expect_timeout(now - 1, now);
  expect_failed(now, handle);
  expect_idle(now);
  natpmp_session_end(session);
}

static void test_session_unanswered_fails_queued() {
  begin();
  auto const first = request();
  auto const second = request();
  auto const now = unanswered(0);

  // the gateway is gone, no need to try again for the second
  expect_failed(now, first);
  expect_failed(now, second);
  expect_idle(now);
  natpmp_session_end(session);
}

static void test_session_refused() {
  begin();
  auto const first = request();
  auto const second = request();
  expect_send(0);
  receive(sizeof(address_error), address_error);

  // only the first, the second still gets its turn
  expect_failed(0, first);
  expect_send(0);
  receive(sizeof(address_response), address_response);
  expect_address(0, second);
  natpmp_session_end(session);
}

static void test_session_ignores_other_responses() {
  begin();

  // nothing asked for
  receive(sizeof(address_response), address_response);
  expect_idle(0);

  // or nothing sent yet
  request();
  receive(sizeof(address_response), address_response);
  expect_send(0);

  // not for the address request
  receive(sizeof(map_response), map_response);
  receive(sizeof(map_error), map_error);
  // and garbage
  receive(3, address_response);
  expect_timeout(0, 250);
  natpmp_session_end(session);
}

static void test_session_ignores_late_responses() {
  begin();
  auto const first = request();
  auto const second = request();
  expect_send(0);
  receive(sizeof(address_response), address_response);

  // another answer to the first, before the second went out
  receive(sizeof(address_error), address_error);
  expect_address(0, first);
  expect_send(0);
  expect_timeout(0, 250);
  receive(sizeof(address_response), address_response);
  expect_address(0, second);
  natpmp_session_end(session);
}

static void test_session_queue_full() {
  session = natpmp_session_begin((natpmp_config_t){.queue_size = 2});
  TEST_ASSERT_NOT_NULL(session);
  auto const first = request();
  request();

  auto const full = natpmp_session_address_request(session);
  TEST_ASSERT_FALSE(full.has_value);
  TEST_ASSERT_EQUAL_INT(ENOBUFS, full.error);

  // room again once the first is reported
  expect_send(0);
  receive(sizeof(address_response), address_response);
  TEST_ASSERT_FALSE(natpmp_session_address_request(session).has_value);
  expect_address(0, first);
  request();
  natpmp_session_end(session);
}

static void test_session_default_queue_size() {
  begin();
  for (int i = 0; i < 8; ++i) {
    request();
  }
  TEST_ASSERT_FALSE(natpmp_session_address_request(session).has_value);
  natpmp_session_end(session);
}

static void test_session_queue_wraps() {
  session = natpmp_session_begin((natpmp_config_t){.queue_size = 2});
  TEST_ASSERT_NOT_NULL(session);

  // around the ring a few times
  for (int i = 0; i < 5; ++i) {
    auto const handle = request();
    expect_send(0);
    receive(sizeof(address_response), address_response);
    expect_address(0, handle);
  }
  expect_idle(0);
  natpmp_session_end(session);
}

static void test_session_keeps_order() {
  session = natpmp_session_begin((natpmp_config_t){.queue_size = 3});
  TEST_ASSERT_NOT_NULL(session);
  auto const first = request();
  auto const second = map(NATPMP_PROTOCOL_TCP);
  auto const third = request();
  expect_send(0);
  receive(sizeof(address_response), address_response);
  expect_address(0, first);

  // takes the first one's place, but still goes out last
  auto const fourth = map(NATPMP_PROTOCOL_TCP);
  expect_sent(0, sizeof(map_tcp_request), map_tcp_request);
  receive(sizeof(map_response), map_response);
  expect_mapped(0, second);
  expect_send(0);
  receive(sizeof(address_response), address_response);
  expect_address(0, third);
  expect_sent(0, sizeof(map_tcp_request), map_tcp_request);
  receive(sizeof(map_response), map_response);
  expect_mapped(0, fourth);
  expect_idle(0);
  natpmp_session_end(session);
}

static void test_session_unreachable() {
  begin();
  auto const first = request();
  auto const second = request();
  expect_send(0);

  // no need to wait for the retransmissions, or to try the second
  natpmp_session_on_unreachable(session);
  expect_failed(10, first);
  expect_failed(10, second);
  expect_idle(10);
  natpmp_session_end(session);
}

static void test_session_unreachable_before_sending() {
  begin();

  // nothing asked for
  natpmp_session_on_unreachable(session);
  expect_idle(0);

  // or nothing sent yet
  auto const handle = request();
  natpmp_session_on_unreachable(session);
  expect_failed(0, handle);
  expect_idle(0);

  // and later requests go out as usual
  request();
  expect_send(10);
  expect_timeout(10, 260);
  natpmp_session_end(session);
}

static void test_session_map() {
  begin();
  auto const handle = map(NATPMP_PROTOCOL_TCP);
  expect_sent(0, sizeof(map_tcp_request), map_tcp_request);
  expect_timeout(0, 250);
  receive(sizeof(map_response), map_response);

  expect_mapped(10, handle);
  expect_idle(10);
  natpmp_session_end(session);
}

static void test_session_map_udp() {
  begin();
  auto const handle =
      natpmp_session_map_request(session, 60, NATPMP_PROTOCOL_UDP, 53, 0);
  TEST_ASSERT_TRUE(handle.has_value);

  uint8_t const map_udp_request[] = {
      0, 1,  0, 0,  // version, MAP_UDP, reserved
      0, 53, 0, 0,  // internal port, any external port
      0, 0,  0, 60, // lifetime
  };
  expect_sent(0, sizeof(map_udp_request), map_udp_request);

  uint8_t const map_udp_response[] = {
      0, 129, 0,    0,    // version, R + MAP_UDP, SUCCESS
      0, 0,   0,    10,   // epoch
      0, 53,  0xC3, 0x50, // internal port, external port
      0, 0,   0,    30,   // lifetime
  };
  receive(sizeof(map_udp_response), map_udp_response);

  auto const event = natpmp_session_poll(session, 0);
  TEST_ASSERT_EQUAL(NATPMP_EVENT_MAPPED, event.type);
  TEST_ASSERT_EQUAL_UINT16(handle.value, event.mapped.handle);
  TEST_ASSERT_EQUAL_UINT16(50000, event.mapped.external_port);
  TEST_ASSERT_EQUAL_UINT32(30, event.mapped.lifetime);
  natpmp_session_end(session);
}

static void test_session_map_refused() {
  begin();
  auto const first = map(NATPMP_PROTOCOL_TCP);
  auto const second = map(NATPMP_PROTOCOL_TCP);
  expect_sent(0, sizeof(map_tcp_request), map_tcp_request);
  receive(sizeof(map_error), map_error);

  // only the first, the second still gets its turn
  expect_failed(0, first);
  expect_sent(0, sizeof(map_tcp_request), map_tcp_request);
  receive(sizeof(map_response), map_response);
  expect_mapped(0, second);
  natpmp_session_end(session);
}

static void test_session_map_ignores_other_responses() {
  begin();
  map(NATPMP_PROTOCOL_TCP);
  expect_sent(0, sizeof(map_tcp_request), map_tcp_request);

  // for the external address
  receive(sizeof(address_response), address_response);
  receive(sizeof(address_error), address_error);

  // for UDP
  uint8_t udp_response[sizeof(map_response)];
  memcpy(udp_response, map_response, sizeof(map_response));
  udp_response[1] = 129;
  receive(sizeof(udp_response), udp_response);
  uint8_t udp_error[sizeof(map_error)];
  memcpy(udp_error, map_error, sizeof(map_error));
  udp_error[1] = 129;
  receive(sizeof(udp_error), udp_error);

  // for another internal port
  uint8_t other_port[sizeof(map_response)];
  memcpy(other_port, map_response, sizeof(map_response));
  other_port[9] = 0x35;
  receive(sizeof(other_port), other_port);

  expect_timeout(0, 250);
  natpmp_session_end(session);
}

static void test_session_mixed() {
  begin();
  auto const first = request();
  auto const second = map(NATPMP_PROTOCOL_TCP);
  auto const third = request();

  // in the order they were made
  expect_send(0);
  receive(sizeof(address_response), address_response);
  expect_address(0, first);
  expect_sent(0, sizeof(map_tcp_request), map_tcp_request);
  receive(sizeof(map_response), map_response);
  expect_mapped(0, second);
  expect_send(0);
  receive(sizeof(address_response), address_response);
  expect_address(0, third);
  expect_idle(0);
  natpmp_session_end(session);
}

static void test_session_map_unanswered_fails_queued() {
  begin();
  auto const first = map(NATPMP_PROTOCOL_TCP);
  auto const second = request();
  natpmp_time_ms_t now = 0;
  for (int i = 0; i < 9; ++i) {
    expect_sent(now, sizeof(map_tcp_request), map_tcp_request);
    now += 250 << i;
  }

  expect_failed(now, first);
  expect_failed(now, second);
  expect_idle(now);
  natpmp_session_end(session);
}

static void *counting_alloc(void *ctx, size_t size) {
  *(int *)ctx += 1;
  return malloc(size);
}

static void counting_free(void *ctx, void *ptr) {
  *(int *)ctx -= 1;
  free(ptr);
}

static void test_session_allocator() {
  int live = 0;
  session = natpmp_session_begin((natpmp_config_t){
      .allocator = {.alloc = counting_alloc,
                    .free = counting_free,
                    .ctx = &live},
  });
  TEST_ASSERT_NOT_NULL(session);

  // the queue comes with the session
  request();
  TEST_ASSERT_EQUAL_INT(1, live);
  natpmp_session_end(session);
  TEST_ASSERT_EQUAL_INT(0, live);
}

static void *failing_alloc(void * /*ctx*/, size_t /*size*/) { return nullptr; }

static void test_session_out_of_memory() {
  TEST_ASSERT_NULL(natpmp_session_begin((natpmp_config_t){
      .allocator = {.alloc = failing_alloc, .free = counting_free},
  }));
}

void session_tests() {
  RUN_TEST(test_session_nothing_requested);
  RUN_TEST(test_session_address);
  RUN_TEST(test_session_one_at_a_time);
  RUN_TEST(test_session_retransmits);
  RUN_TEST(test_session_fails_unanswered);
  RUN_TEST(test_session_unanswered_fails_queued);
  RUN_TEST(test_session_refused);
  RUN_TEST(test_session_ignores_other_responses);
  RUN_TEST(test_session_ignores_late_responses);
  RUN_TEST(test_session_queue_full);
  RUN_TEST(test_session_default_queue_size);
  RUN_TEST(test_session_queue_wraps);
  RUN_TEST(test_session_keeps_order);
  RUN_TEST(test_session_unreachable);
  RUN_TEST(test_session_unreachable_before_sending);
  RUN_TEST(test_session_map);
  RUN_TEST(test_session_map_udp);
  RUN_TEST(test_session_map_refused);
  RUN_TEST(test_session_map_ignores_other_responses);
  RUN_TEST(test_session_mixed);
  RUN_TEST(test_session_map_unanswered_fails_queued);
  RUN_TEST(test_session_allocator);
  RUN_TEST(test_session_out_of_memory);
}
