#include <unity.h>

#include <byo/natpmp/natpmp.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "runner.h"

static natpmp_session_t *session;

// what the rng gives the session next
static uint32_t random_value;
static bool random_fails;

static bool fill(void * /*ctx*/, size_t size, uint8_t data[]) {
  if (random_fails) {
    // and leaves garbage behind
    memset(data, 0xFF, size);
    return false;
  }
  TEST_ASSERT_EQUAL_size_t(sizeof(random_value), size);
  memcpy(data, &random_value, size);
  return true;
}

static natpmp_session_t *begin_with(natpmp_config_t config) {
  random_value = 0;
  random_fails = false;
  config.rng.fill = fill;
  session = natpmp_session_begin(config);
  return session;
}

static void begin() { TEST_ASSERT_NOT_NULL(begin_with((natpmp_config_t){})); }

static void request() { natpmp_session_address_request(session); }

static natpmp_handle_t map(natpmp_protocol_t protocol) {
  auto const handle =
      natpmp_session_map_request(session, 3600, protocol, 0x1234, 0x1111);
  TEST_ASSERT_TRUE(handle.has_value);
  return handle.value;
}

// with the epoch replaced, if `data` is long enough to carry one
static void receive_epoch(natpmp_time_ms_t now,
                          uint32_t epoch,
                          size_t size,
                          uint8_t const data[]) {
  uint8_t copy[16] = {};
  TEST_ASSERT_LESS_OR_EQUAL_size_t(sizeof(copy), size);
  memcpy(copy, data, size);
  if (size >= 8) {
    copy[4] = (uint8_t)(epoch >> 24);
    copy[5] = (uint8_t)(epoch >> 16);
    copy[6] = (uint8_t)(epoch >> 8);
    copy[7] = (uint8_t)epoch;
  }
  natpmp_session_on_datagram(session, now, size, copy);
}

// from a gateway that started 10 s before the session and kept running
static void receive(natpmp_time_ms_t now, size_t size, uint8_t const data[]) {
  receive_epoch(now, 10 + (uint32_t)(now / 1000), size, data);
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

// with the port and lifetime that map_response grants
static uint8_t const map_tcp_renewal[] = {
    0,    2,    0,    0,    // version, MAP_TCP, reserved
    0x12, 0x34, 0x56, 0x78, // internal port, granted external port
    0,    0,    0x1C, 0x20, // granted lifetime
};

// half the lifetime map_response grants
static constexpr natpmp_time_ms_t renewal = 3600 * 1000;

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

static uint8_t const map_tcp_deletion[] = {
    0,    2,    0, 0, // version, MAP_TCP, reserved
    0x12, 0x34, 0, 0, // internal port, no external port
    0,    0,    0, 0, // no lifetime
};

static uint8_t const deletion_response[] = {
    0,    130,  0, 0,  // version, R + MAP_TCP, SUCCESS
    0,    0,    0, 10, // epoch
    0x12, 0x34, 0, 0,  // internal port, no external port
    0,    0,    0, 0,  // no lifetime
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

static void expect_address(natpmp_time_ms_t now) {
  auto const event = natpmp_session_poll(session, now);
  TEST_ASSERT_EQUAL(NATPMP_EVENT_ADDRESS, event.type);
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

static void expect_address_failed(natpmp_time_ms_t now) {
  auto const event = natpmp_session_poll(session, now);
  TEST_ASSERT_EQUAL(NATPMP_EVENT_ADDRESS_FAILED, event.type);
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
  request();
  expect_send(1000);
  expect_timeout(1000, 1250);
  receive(0, sizeof(address_response), address_response);

  expect_address(1010);
  expect_idle(1010);
  natpmp_session_end(session);
}

static void test_session_address_shared() {
  begin();
  request();
  expect_send(0);

  // on the wire, and then answered but not reported, it answers later calls too
  request();
  receive(0, sizeof(address_response), address_response);
  request();
  expect_address(0);
  expect_idle(0);

  // but once reported, there's a new request
  request();
  expect_send(10);
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
  request();
  auto const now = unanswered(0);

  // 64 s after the ninth
  expect_timeout(now - 1, now);
  expect_address_failed(now);
  expect_idle(now);
  natpmp_session_end(session);
}

static void test_session_unanswered_fails_queued() {
  begin();
  request();
  auto const mapping = map(NATPMP_PROTOCOL_TCP);
  auto const now = unanswered(0);

  // the gateway is gone, no need to try the mapping
  expect_address_failed(now);
  expect_failed(now, mapping);
  expect_idle(now);
  natpmp_session_end(session);
}

static void test_session_refused() {
  begin();
  request();
  auto const mapping = map(NATPMP_PROTOCOL_TCP);
  expect_send(0);
  receive(0, sizeof(address_error), address_error);

  // only the address request, the mapping still gets its turn
  expect_address_failed(0);
  expect_sent(0, sizeof(map_tcp_request), map_tcp_request);
  receive(0, sizeof(map_response), map_response);
  expect_mapped(0, mapping);
  natpmp_session_end(session);
}

static void test_session_ignores_other_responses() {
  begin();

  // nothing asked for
  receive(0, sizeof(address_response), address_response);
  expect_idle(0);

  // or nothing sent yet
  request();
  receive(0, sizeof(address_response), address_response);
  expect_send(0);

  // not for the address request
  receive(0, sizeof(map_response), map_response);
  receive(0, sizeof(map_error), map_error);
  // and garbage
  receive(0, 3, address_response);
  expect_timeout(0, 250);
  natpmp_session_end(session);
}

static void test_session_ignores_late_responses() {
  begin();
  request();
  auto const mapping = map(NATPMP_PROTOCOL_TCP);
  expect_send(0);
  receive(0, sizeof(address_response), address_response);

  // another answer to the address request, before the mapping went out
  receive(0, sizeof(address_error), address_error);
  expect_address(0);
  expect_sent(0, sizeof(map_tcp_request), map_tcp_request);
  expect_timeout(0, 250);
  receive(0, sizeof(map_response), map_response);
  expect_mapped(0, mapping);
  natpmp_session_end(session);
}

static void test_session_full() {
  begin_with((natpmp_config_t){.capacity = 2});
  TEST_ASSERT_NOT_NULL(session);
  auto const first = map(NATPMP_PROTOCOL_TCP);
  map(NATPMP_PROTOCOL_TCP);

  auto const full =
      natpmp_session_map_request(session, 3600, NATPMP_PROTOCOL_TCP, 1, 0);
  TEST_ASSERT_FALSE(full.has_value);
  TEST_ASSERT_EQUAL_INT(ENOBUFS, full.error);

  // the address request doesn't need a slot
  request();

  // room again once the first one's failure is reported
  expect_sent(0, sizeof(map_tcp_request), map_tcp_request);
  receive(0, sizeof(map_error), map_error);
  TEST_ASSERT_FALSE(
      natpmp_session_map_request(session, 3600, NATPMP_PROTOCOL_TCP, 1, 0)
          .has_value);
  expect_failed(0, first);
  map(NATPMP_PROTOCOL_TCP);
  natpmp_session_end(session);
}

static void test_session_default_capacity() {
  begin();
  for (int i = 0; i < 8; ++i) {
    map(NATPMP_PROTOCOL_TCP);
  }
  TEST_ASSERT_FALSE(
      natpmp_session_map_request(session, 3600, NATPMP_PROTOCOL_TCP, 1, 0)
          .has_value);
  natpmp_session_end(session);
}

static void test_session_reuses_slots() {
  begin_with((natpmp_config_t){.capacity = 2});
  TEST_ASSERT_NOT_NULL(session);

  // around the slots a few times
  for (int i = 0; i < 5; ++i) {
    auto const handle = map(NATPMP_PROTOCOL_TCP);
    expect_sent(0, sizeof(map_tcp_request), map_tcp_request);
    receive(0, sizeof(map_error), map_error);
    expect_failed(0, handle);
  }
  expect_idle(0);
  natpmp_session_end(session);
}

static void test_session_keeps_order() {
  begin_with((natpmp_config_t){.capacity = 2});
  TEST_ASSERT_NOT_NULL(session);
  auto const first = map(NATPMP_PROTOCOL_TCP);
  auto const second = map(NATPMP_PROTOCOL_TCP);
  expect_sent(0, sizeof(map_tcp_request), map_tcp_request);
  receive(0, sizeof(map_error), map_error);
  expect_failed(0, first);

  // takes the first one's slot, but still goes out last
  auto const third = map(NATPMP_PROTOCOL_TCP);
  expect_sent(0, sizeof(map_tcp_request), map_tcp_request);
  receive(0, sizeof(map_response), map_response);
  expect_mapped(0, second);
  expect_sent(0, sizeof(map_tcp_request), map_tcp_request);
  receive(0, sizeof(map_response), map_response);
  expect_mapped(0, third);
  expect_timeout(0, renewal);
  natpmp_session_end(session);
}

static void test_session_unreachable() {
  begin();
  request();
  auto const mapping = map(NATPMP_PROTOCOL_TCP);
  expect_send(0);

  // no need to wait for the retransmissions, or to try the mapping
  natpmp_session_on_host_unreachable(session);
  expect_address_failed(10);
  expect_failed(10, mapping);
  expect_idle(10);
  natpmp_session_end(session);
}

static void test_session_unreachable_before_sending() {
  begin();

  // nothing asked for
  natpmp_session_on_host_unreachable(session);
  expect_idle(0);

  // or nothing sent yet
  request();
  natpmp_session_on_host_unreachable(session);
  expect_address_failed(0);
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
  receive(5, sizeof(map_response), map_response);

  // halfway from the answer
  expect_mapped(10, handle);
  expect_timeout(10, 5 + renewal);
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
  receive(0, sizeof(map_udp_response), map_udp_response);

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
  receive(0, sizeof(map_error), map_error);

  // only the first, the second still gets its turn
  expect_failed(0, first);
  expect_sent(0, sizeof(map_tcp_request), map_tcp_request);
  receive(0, sizeof(map_response), map_response);
  expect_mapped(0, second);
  natpmp_session_end(session);
}

static void test_session_map_ignores_other_responses() {
  begin();
  map(NATPMP_PROTOCOL_TCP);
  expect_sent(0, sizeof(map_tcp_request), map_tcp_request);

  // for the external address
  receive(0, sizeof(address_response), address_response);
  receive(0, sizeof(address_error), address_error);

  // for UDP
  uint8_t udp_response[sizeof(map_response)];
  memcpy(udp_response, map_response, sizeof(map_response));
  udp_response[1] = 129;
  receive(0, sizeof(udp_response), udp_response);
  uint8_t udp_error[sizeof(map_error)];
  memcpy(udp_error, map_error, sizeof(map_error));
  udp_error[1] = 129;
  receive(0, sizeof(udp_error), udp_error);

  // for another internal port
  uint8_t other_port[sizeof(map_response)];
  memcpy(other_port, map_response, sizeof(map_response));
  other_port[9] = 0x35;
  receive(0, sizeof(other_port), other_port);

  expect_timeout(0, 250);
  natpmp_session_end(session);
}

static void test_session_mixed() {
  begin();
  auto const first = map(NATPMP_PROTOCOL_TCP);
  request();
  auto const third = map(NATPMP_PROTOCOL_TCP);

  // in the order they were made
  expect_sent(0, sizeof(map_tcp_request), map_tcp_request);
  receive(0, sizeof(map_response), map_response);
  expect_mapped(0, first);
  expect_send(0);
  receive(0, sizeof(address_response), address_response);
  expect_address(0);
  expect_sent(0, sizeof(map_tcp_request), map_tcp_request);
  receive(0, sizeof(map_response), map_response);
  expect_mapped(0, third);
  expect_timeout(0, renewal);
  natpmp_session_end(session);
}

static void test_session_map_unanswered_fails_queued() {
  begin();
  auto const first = map(NATPMP_PROTOCOL_TCP);
  auto const second = map(NATPMP_PROTOCOL_TCP);
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

static void test_session_map_zero_lifetime() {
  begin();
  auto const handle = map(NATPMP_PROTOCOL_TCP);
  expect_sent(0, sizeof(map_tcp_request), map_tcp_request);

  uint8_t zero_lifetime[sizeof(map_response)];
  memcpy(zero_lifetime, map_response, sizeof(map_response));
  memset(&zero_lifetime[12], 0, 4);
  receive(0, sizeof(zero_lifetime), zero_lifetime);
  expect_failed(0, handle);
  expect_idle(0);
  natpmp_session_end(session);
}

// a TCP mapping, granted at `now`
static natpmp_handle_t mapped(natpmp_time_ms_t now) {
  auto const handle = map(NATPMP_PROTOCOL_TCP);
  expect_sent(now, sizeof(map_tcp_request), map_tcp_request);
  receive(now, sizeof(map_response), map_response);
  expect_mapped(now, handle);
  return handle;
}

static void test_session_renews() {
  begin();
  auto const handle = mapped(0);
  expect_timeout(0, renewal);
  expect_timeout(renewal - 1, renewal);

  // asking for what the gateway granted
  expect_sent(renewal, sizeof(map_tcp_renewal), map_tcp_renewal);
  expect_timeout(renewal, renewal + 250);
  receive(renewal + 5, sizeof(map_response), map_response);
  expect_mapped(renewal + 10, handle);
  expect_timeout(renewal + 10, renewal + 5 + renewal);
  natpmp_session_end(session);
}

static void test_session_renewal_waits_its_turn() {
  begin();
  mapped(0);
  request();
  expect_send(renewal - 1);

  // due, but behind the address request
  expect_timeout(renewal, renewal - 1 + 250);
  receive(0, sizeof(address_response), address_response);
  expect_address(renewal + 10);
  expect_sent(renewal + 10, sizeof(map_tcp_renewal), map_tcp_renewal);
  natpmp_session_end(session);
}

static void test_session_renewal_behind_reused_slot() {
  begin();
  auto const mapping = map(NATPMP_PROTOCOL_TCP);
  auto const first = map(NATPMP_PROTOCOL_TCP);
  expect_sent(0, sizeof(map_tcp_request), map_tcp_request);
  receive(0, sizeof(map_response), map_response);
  expect_mapped(0, mapping);
  expect_sent(0, sizeof(map_tcp_request), map_tcp_request);
  receive(0, sizeof(map_error), map_error);
  expect_failed(0, first);

  // in the slot that was behind the mapping, and still waiting at the renewal
  auto const second = map(NATPMP_PROTOCOL_TCP);
  expect_sent(renewal - 1, sizeof(map_tcp_request), map_tcp_request);
  expect_timeout(renewal, renewal - 1 + 250);
  receive(renewal, sizeof(map_response), map_response);
  expect_mapped(renewal, second);
  expect_sent(renewal, sizeof(map_tcp_renewal), map_tcp_renewal);
  receive(renewal, sizeof(map_response), map_response);
  expect_mapped(renewal, mapping);
  expect_timeout(renewal, renewal + renewal);
  natpmp_session_end(session);
}

static void test_session_renewal_refused() {
  begin_with((natpmp_config_t){.capacity = 1});
  TEST_ASSERT_NOT_NULL(session);
  auto const handle = mapped(0);

  // the mapping keeps its slot
  TEST_ASSERT_FALSE(
      natpmp_session_map_request(session, 3600, NATPMP_PROTOCOL_TCP, 1, 0)
          .has_value);

  expect_sent(renewal, sizeof(map_tcp_renewal), map_tcp_renewal);
  receive(0, sizeof(map_error), map_error);
  expect_failed(renewal, handle);
  expect_idle(renewal);

  // until it fails
  map(NATPMP_PROTOCOL_TCP);
  natpmp_session_end(session);
}

static void test_session_renewal_unanswered() {
  begin();
  auto const first = mapped(0);
  mapped(300 * 1000);
  expect_timeout(300 * 1000, renewal);

  natpmp_time_ms_t now = renewal;
  for (int i = 0; i < 9; ++i) {
    expect_sent(now, sizeof(map_tcp_renewal), map_tcp_renewal);
    now += 250 << i;
  }
  expect_failed(now, first);

  // the other one isn't due yet, so it's left alone
  expect_timeout(now, 300 * 1000 + renewal);
  expect_sent(300 * 1000 + renewal, sizeof(map_tcp_renewal), map_tcp_renewal);
  natpmp_session_end(session);
}

static void test_session_unreachable_after_answer() {
  begin();
  auto const first = map(NATPMP_PROTOCOL_TCP);
  auto const second = map(NATPMP_PROTOCOL_TCP);
  expect_sent(0, sizeof(map_tcp_request), map_tcp_request);
  receive(0, sizeof(map_response), map_response);

  // the answer stands, only what's still waiting fails
  natpmp_session_on_host_unreachable(session);
  expect_mapped(0, first);
  expect_failed(0, second);
  expect_timeout(0, renewal);
  natpmp_session_end(session);
}

static void test_session_reports_before_renewing() {
  begin();
  auto const handle = map(NATPMP_PROTOCOL_TCP);
  expect_sent(0, sizeof(map_tcp_request), map_tcp_request);
  receive(0, sizeof(map_response), map_response);

  // polled only once the renewal is due
  expect_mapped(renewal, handle);
  expect_sent(renewal, sizeof(map_tcp_renewal), map_tcp_renewal);
  natpmp_session_end(session);
}

static void test_session_release() {
  begin();
  auto const handle = mapped(0);
  natpmp_session_release(session, handle);

  expect_sent(10, sizeof(map_tcp_deletion), map_tcp_deletion);
  expect_timeout(10, 260);
  receive(20, sizeof(deletion_response), deletion_response);

  // no event, and no more renewals
  expect_idle(20);
  natpmp_session_end(session);
}

static void test_session_release_frees_slot() {
  begin_with((natpmp_config_t){.capacity = 1});
  TEST_ASSERT_NOT_NULL(session);
  auto const handle = mapped(0);
  natpmp_session_release(session, handle);

  // only once the deletion is done
  TEST_ASSERT_FALSE(
      natpmp_session_map_request(session, 3600, NATPMP_PROTOCOL_TCP, 1, 0)
          .has_value);
  expect_sent(0, sizeof(map_tcp_deletion), map_tcp_deletion);
  receive(0, sizeof(deletion_response), deletion_response);
  map(NATPMP_PROTOCOL_TCP);
  natpmp_session_end(session);
}

static void test_session_release_queued() {
  begin();
  auto const mapping = map(NATPMP_PROTOCOL_TCP);
  request();
  natpmp_session_release(session, mapping);

  // keeps its place, ahead of the address request
  expect_sent(0, sizeof(map_tcp_deletion), map_tcp_deletion);
  receive(0, sizeof(deletion_response), deletion_response);
  expect_send(0);
  receive(0, sizeof(address_response), address_response);
  expect_address(0);
  expect_idle(0);
  natpmp_session_end(session);
}

static void test_session_release_sent() {
  begin();
  auto const handle = map(NATPMP_PROTOCOL_TCP);
  expect_sent(0, sizeof(map_tcp_request), map_tcp_request);
  natpmp_session_release(session, handle);

  // right away, from scratch
  expect_sent(10, sizeof(map_tcp_deletion), map_tcp_deletion);
  expect_timeout(10, 260);

  // and the grant for the map request comes too late
  receive(20, sizeof(map_response), map_response);
  expect_timeout(20, 260);
  receive(30, sizeof(deletion_response), deletion_response);
  expect_idle(30);
  natpmp_session_end(session);
}

static void test_session_release_answered() {
  begin();
  auto const handle = map(NATPMP_PROTOCOL_TCP);
  expect_sent(0, sizeof(map_tcp_request), map_tcp_request);
  receive(0, sizeof(map_response), map_response);

  // before the grant is polled, which is then never reported
  natpmp_session_release(session, handle);
  expect_sent(0, sizeof(map_tcp_deletion), map_tcp_deletion);
  receive(0, sizeof(deletion_response), deletion_response);
  expect_idle(0);
  natpmp_session_end(session);
}

static void test_session_release_waits_its_turn() {
  begin();
  auto const mapping = mapped(0);
  request();
  natpmp_session_release(session, mapping);

  // behind the address request
  expect_send(0);
  receive(0, sizeof(address_response), address_response);
  expect_address(0);
  expect_sent(0, sizeof(map_tcp_deletion), map_tcp_deletion);
  natpmp_session_end(session);
}

static void test_session_release_failed() {
  begin();
  auto const handle = map(NATPMP_PROTOCOL_TCP);
  expect_sent(0, sizeof(map_tcp_request), map_tcp_request);
  receive(0, sizeof(map_error), map_error);

  // nothing to delete, and the failure isn't reported
  natpmp_session_release(session, handle);
  expect_idle(0);
  natpmp_session_end(session);
}

static void test_session_release_refused() {
  begin();
  auto const handle = mapped(0);
  natpmp_session_release(session, handle);
  expect_sent(0, sizeof(map_tcp_deletion), map_tcp_deletion);

  // left to expire on the gateway
  receive(0, sizeof(map_error), map_error);
  expect_idle(0);
  natpmp_session_end(session);
}

static void test_session_release_unanswered() {
  begin();
  auto const handle = mapped(0);
  natpmp_session_release(session, handle);
  natpmp_time_ms_t now = 0;
  for (int i = 0; i < 9; ++i) {
    expect_sent(now, sizeof(map_tcp_deletion), map_tcp_deletion);
    now += 250 << i;
  }

  // left to expire on the gateway
  expect_idle(now);
  natpmp_session_end(session);
}

static void test_session_release_unreachable() {
  begin();
  auto const handle = mapped(0);
  natpmp_session_release(session, handle);
  expect_sent(0, sizeof(map_tcp_deletion), map_tcp_deletion);

  // left to expire on the gateway
  natpmp_session_on_host_unreachable(session);
  expect_idle(0);
  natpmp_session_end(session);
}

static void test_session_release_ignored() {
  begin();

  // no such handle
  natpmp_session_release(session, 999);
  expect_idle(0);

  // or already released, which doesn't start the deletion over
  auto const mapping = mapped(0);
  natpmp_session_release(session, mapping);
  expect_sent(0, sizeof(map_tcp_deletion), map_tcp_deletion);
  natpmp_session_release(session, mapping);
  expect_timeout(10, 250);
  natpmp_session_end(session);
}

static void test_session_epoch_reboot() {
  begin();
  auto const handle = mapped(0);

  // a minute later, the gateway has only been up for a second, and the rng
  // picks the longest delay
  random_value = 5000;
  receive_epoch(60'000, 1, sizeof(address_response), address_response);
  expect_timeout(60'000, 65'000);
  expect_sent(65'000, sizeof(map_tcp_renewal), map_tcp_renewal);
  receive_epoch(65'000, 2, sizeof(map_response), map_response);
  expect_mapped(65'000, handle);
  expect_timeout(65'000, 65'000 + renewal);
  natpmp_session_end(session);
}

static void test_session_epoch_answered() {
  begin();
  auto const handle = map(NATPMP_PROTOCOL_TCP);
  expect_sent(0, sizeof(map_tcp_request), map_tcp_request);
  receive(0, sizeof(map_response), map_response);

  // granted, but not reported yet
  random_value = 1000;
  receive_epoch(60'000, 1, sizeof(address_response), address_response);
  expect_mapped(60'000, handle);
  expect_timeout(60'000, 61'000);
  natpmp_session_end(session);
}

static void test_session_epoch_answers_after_reboot() {
  begin();
  auto const first = mapped(0);
  auto const second =
      natpmp_session_map_request(session, 3600, NATPMP_PROTOCOL_UDP, 0x1234, 0);
  TEST_ASSERT_TRUE(second.has_value);
  uint8_t const map_udp_request[] = {
      0,    1,    0,    0,    // version, MAP_UDP, reserved
      0x12, 0x34, 0,    0,    // internal port, any external port
      0,    0,    0x0E, 0x10, // lifetime
  };
  expect_sent(60'000, sizeof(map_udp_request), map_udp_request);

  // the answer that shows the reboot comes from the rebooted gateway
  random_value = 1000;
  uint8_t udp_response[sizeof(map_response)];
  memcpy(udp_response, map_response, sizeof(map_response));
  udp_response[1] = 129;
  receive_epoch(60'000, 1, sizeof(udp_response), udp_response);
  expect_mapped(60'000, second.value);
  expect_timeout(60'000, 61'000);

  expect_sent(61'000, sizeof(map_tcp_renewal), map_tcp_renewal);
  receive_epoch(61'000, 2, sizeof(map_response), map_response);
  expect_mapped(61'000, first);
  expect_timeout(61'000, 60'000 + renewal);
  natpmp_session_end(session);
}

static void test_session_epoch_without_random() {
  begin();
  mapped(0);

  // renews right away
  random_fails = true;
  receive_epoch(60'000, 1, sizeof(address_response), address_response);
  expect_sent(60'000, sizeof(map_tcp_renewal), map_tcp_renewal);
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
  begin_with((natpmp_config_t){
      .allocator = {.alloc = counting_alloc,
                    .free = counting_free,
                    .ctx = &live},
  });
  TEST_ASSERT_NOT_NULL(session);

  // the slots come with the session
  map(NATPMP_PROTOCOL_TCP);
  TEST_ASSERT_EQUAL_INT(1, live);
  natpmp_session_end(session);
  TEST_ASSERT_EQUAL_INT(0, live);
}

static void *failing_alloc(void * /*ctx*/, size_t /*size*/) { return nullptr; }

static void test_session_out_of_memory() {
  TEST_ASSERT_NULL(begin_with((natpmp_config_t){
      .allocator = {.alloc = failing_alloc, .free = counting_free},
  }));
}

void session_tests() {
  RUN_TEST(test_session_nothing_requested);
  RUN_TEST(test_session_address);
  RUN_TEST(test_session_address_shared);
  RUN_TEST(test_session_retransmits);
  RUN_TEST(test_session_fails_unanswered);
  RUN_TEST(test_session_unanswered_fails_queued);
  RUN_TEST(test_session_refused);
  RUN_TEST(test_session_ignores_other_responses);
  RUN_TEST(test_session_ignores_late_responses);
  RUN_TEST(test_session_full);
  RUN_TEST(test_session_default_capacity);
  RUN_TEST(test_session_reuses_slots);
  RUN_TEST(test_session_keeps_order);
  RUN_TEST(test_session_unreachable);
  RUN_TEST(test_session_unreachable_before_sending);
  RUN_TEST(test_session_map);
  RUN_TEST(test_session_map_udp);
  RUN_TEST(test_session_map_refused);
  RUN_TEST(test_session_map_ignores_other_responses);
  RUN_TEST(test_session_mixed);
  RUN_TEST(test_session_map_unanswered_fails_queued);
  RUN_TEST(test_session_map_zero_lifetime);
  RUN_TEST(test_session_renews);
  RUN_TEST(test_session_renewal_waits_its_turn);
  RUN_TEST(test_session_renewal_behind_reused_slot);
  RUN_TEST(test_session_renewal_refused);
  RUN_TEST(test_session_renewal_unanswered);
  RUN_TEST(test_session_unreachable_after_answer);
  RUN_TEST(test_session_reports_before_renewing);
  RUN_TEST(test_session_release);
  RUN_TEST(test_session_release_frees_slot);
  RUN_TEST(test_session_release_queued);
  RUN_TEST(test_session_release_sent);
  RUN_TEST(test_session_release_answered);
  RUN_TEST(test_session_release_waits_its_turn);
  RUN_TEST(test_session_release_failed);
  RUN_TEST(test_session_release_refused);
  RUN_TEST(test_session_release_unanswered);
  RUN_TEST(test_session_release_unreachable);
  RUN_TEST(test_session_release_ignored);
  RUN_TEST(test_session_epoch_reboot);
  RUN_TEST(test_session_epoch_answered);
  RUN_TEST(test_session_epoch_answers_after_reboot);
  RUN_TEST(test_session_epoch_without_random);
  RUN_TEST(test_session_allocator);
  RUN_TEST(test_session_out_of_memory);
}
