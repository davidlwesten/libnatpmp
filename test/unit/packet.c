#include <unity.h>

#include "packet.h"
#include "runner.h"

static natpmp_optional_response_t read(size_t size, uint8_t const data[]) {
  return natpmp_packet_read_response(
      (const_view_t){.size = size, .data = data});
}

static void test_packet_address_request() {
  uint8_t buffer[16] = {0xAA, 0xAA, 0xAA};
  auto const size = natpmp_packet_write_address_request(
      (view_t){.size = sizeof(buffer), .data = buffer});

  // and nothing past it
  uint8_t const expected[] = {0, 0, 0xAA};
  TEST_ASSERT_EQUAL_size_t(2, size);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, buffer, sizeof(expected));
}

static void test_packet_map_request() {
  uint8_t buffer[16];
  auto const size = natpmp_packet_write_map_request(
      (view_t){.size = sizeof(buffer), .data = buffer}, NATPMP_OPCODE_MAP_TCP,
      0x1234, 0x5678, 7200);

  uint8_t const expected[] = {
      0,    2,                // version, MAP_TCP
      0,    0,                // reserved
      0x12, 0x34,             // internal port
      0x56, 0x78,             // suggested external port
      0,    0,    0x1C, 0x20, // lifetime
  };
  TEST_ASSERT_EQUAL_size_t(sizeof(expected), size);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, buffer, sizeof(expected));
}

static void test_packet_map_request_udp() {
  uint8_t buffer[16];
  natpmp_packet_write_map_request(
      (view_t){.size = sizeof(buffer), .data = buffer}, NATPMP_OPCODE_MAP_UDP,
      1, 1, 1);
  TEST_ASSERT_EQUAL_UINT8(1, buffer[1]);
}

static void test_packet_delete_request() {
  uint8_t buffer[16];
  auto const size = natpmp_packet_write_map_request(
      (view_t){.size = sizeof(buffer), .data = buffer}, NATPMP_OPCODE_MAP_UDP,
      0x1234, 0, 0);

  // lifetime and suggested port 0 (section 3.4)
  uint8_t const expected[] = {0, 1, 0, 0, 0x12, 0x34, 0, 0, 0, 0, 0, 0};
  TEST_ASSERT_EQUAL_size_t(sizeof(expected), size);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, buffer, sizeof(expected));
}

static void test_packet_address_response() {
  uint8_t const data[] = {
      0,   128,         // version, 128 + EXTERNAL_ADDRESS
      0,   0,           // success
      0,   0,   1,   0, // epoch
      203, 0,   113, 7, // external address
  };
  auto const response = read(sizeof(data), data);
  TEST_ASSERT_TRUE(response.has_value);
  TEST_ASSERT_EQUAL(NATPMP_RESPONSE_TO_ADDRESS, response.value.answers);
  TEST_ASSERT_EQUAL_UINT16(0, response.value.result);
  TEST_ASSERT_EQUAL_UINT32(256, response.value.epoch);
  TEST_ASSERT_TRUE(response.value.has_body);

  uint8_t const expected[] = {203, 0, 113, 7};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, response.value.body.address, 4);
}

static void test_packet_map_response() {
  uint8_t const data[] = {
      0,    130,              // version, 128 + MAP_TCP
      0,    0,                // success
      0,    0,    0,    9,    // epoch
      0x12, 0x34,             // internal port
      0x9A, 0xBC,             // external port
      0,    0,    0x0E, 0x10, // lifetime
  };
  auto const response = read(sizeof(data), data);
  TEST_ASSERT_TRUE(response.has_value);
  TEST_ASSERT_EQUAL(NATPMP_RESPONSE_TO_MAP, response.value.answers);
  TEST_ASSERT_EQUAL(NATPMP_OPCODE_MAP_TCP, response.value.opcode);
  TEST_ASSERT_EQUAL_UINT32(9, response.value.epoch);
  TEST_ASSERT_TRUE(response.value.has_body);
  TEST_ASSERT_EQUAL_UINT16(0x1234, response.value.body.map.internal_port);
  TEST_ASSERT_EQUAL_UINT16(0x9ABC, response.value.body.map.external_port);
  TEST_ASSERT_EQUAL_UINT32(3600, response.value.body.map.lifetime);
}

static void test_packet_map_response_udp() {
  uint8_t const data[16] = {0, 129};
  auto const response = read(sizeof(data), data);
  TEST_ASSERT_TRUE(response.has_value);
  TEST_ASSERT_EQUAL(NATPMP_OPCODE_MAP_UDP, response.value.opcode);
}

static void test_packet_unsupported_version() {
  // just the header, and OP 0 instead of 128 (section 3.5)
  uint8_t const data[] = {0, 0, 0, 1, 0, 0, 0, 5};
  auto const response = read(sizeof(data), data);
  TEST_ASSERT_TRUE(response.has_value);
  TEST_ASSERT_EQUAL(NATPMP_RESPONSE_TO_ANY, response.value.answers);
  TEST_ASSERT_EQUAL_UINT16(1, response.value.result);
  TEST_ASSERT_EQUAL_UINT32(5, response.value.epoch);
  TEST_ASSERT_FALSE(response.value.has_body);
}

static void test_packet_error_without_body() {
  uint8_t const data[] = {0, 130, 0, 2, 0, 0, 0, 5};
  auto const response = read(sizeof(data), data);
  TEST_ASSERT_TRUE(response.has_value);
  TEST_ASSERT_EQUAL(NATPMP_RESPONSE_TO_MAP, response.value.answers);
  TEST_ASSERT_EQUAL(NATPMP_OPCODE_MAP_TCP, response.value.opcode);
  TEST_ASSERT_EQUAL_UINT16(2, response.value.result);
  TEST_ASSERT_FALSE(response.value.has_body);
}

static void test_packet_error_with_body() {
  uint8_t const data[] = {0, 128, 0, 3, 0, 0, 0, 5, 0, 0, 0, 0};
  auto const response = read(sizeof(data), data);
  TEST_ASSERT_TRUE(response.has_value);
  TEST_ASSERT_EQUAL_UINT16(3, response.value.result);
  TEST_ASSERT_TRUE(response.value.has_body);
}

static void test_packet_undefined_result() {
  // passed on, it's up to the caller what to make of it
  uint8_t const data[] = {0, 128, 0x12, 0x34, 0, 0, 0, 5};
  auto const response = read(sizeof(data), data);
  TEST_ASSERT_TRUE(response.has_value);
  TEST_ASSERT_EQUAL_UINT16(0x1234, response.value.result);
}

static void test_packet_rejects_success_without_body() {
  uint8_t const data[] = {0, 128, 0, 0, 0, 0, 0, 5};
  TEST_ASSERT_FALSE(read(sizeof(data), data).has_value);
}

static void test_packet_rejects_requests() {
  // the R bit is clear
  uint8_t const data[] = {0, 1, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0};
  TEST_ASSERT_FALSE(read(sizeof(data), data).has_value);
}

static void test_packet_rejects_other_version() {
  // e.g. PCP
  uint8_t const data[] = {2, 128, 0, 0, 0, 0, 0, 5, 1, 2, 3, 4};
  TEST_ASSERT_FALSE(read(sizeof(data), data).has_value);
}

static void test_packet_rejects_unknown_opcode() {
  uint8_t const data[] = {0, 131, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0};
  TEST_ASSERT_FALSE(read(sizeof(data), data).has_value);
}

static void test_packet_rejects_wrong_sizes() {
  uint8_t const data[17] = {0, 129};
  // a header is 8 bytes, and the bodies have fixed sizes
  TEST_ASSERT_FALSE(read(0, data).has_value);
  TEST_ASSERT_FALSE(read(7, data).has_value);
  TEST_ASSERT_FALSE(read(12, data).has_value);
  TEST_ASSERT_FALSE(read(15, data).has_value);
  TEST_ASSERT_FALSE(read(17, data).has_value);
  TEST_ASSERT_TRUE(read(16, data).has_value);
}

static void test_packet_rejects_mismatched_body() {
  // an address body on a map response
  uint8_t const data[] = {0, 129, 0, 0, 0, 0, 0, 5, 1, 2, 3, 4};
  TEST_ASSERT_FALSE(read(sizeof(data), data).has_value);
}

void packet_tests() {
  RUN_TEST(test_packet_address_request);
  RUN_TEST(test_packet_map_request);
  RUN_TEST(test_packet_map_request_udp);
  RUN_TEST(test_packet_delete_request);
  RUN_TEST(test_packet_address_response);
  RUN_TEST(test_packet_map_response);
  RUN_TEST(test_packet_map_response_udp);
  RUN_TEST(test_packet_unsupported_version);
  RUN_TEST(test_packet_error_without_body);
  RUN_TEST(test_packet_error_with_body);
  RUN_TEST(test_packet_undefined_result);
  RUN_TEST(test_packet_rejects_success_without_body);
  RUN_TEST(test_packet_rejects_requests);
  RUN_TEST(test_packet_rejects_other_version);
  RUN_TEST(test_packet_rejects_unknown_opcode);
  RUN_TEST(test_packet_rejects_wrong_sizes);
  RUN_TEST(test_packet_rejects_mismatched_body);
}
