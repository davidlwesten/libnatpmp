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
  TEST_ASSERT_EQUAL(NATPMP_RESPONSE_ADDRESS, response.value.type);
  TEST_ASSERT_EQUAL_UINT32(256, response.value.epoch);

  natpmp_ipv4_address_t const expected = {{203, 0, 113, 7}};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.octets,
                                response.value.address.external_ip.octets, 4);
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
  TEST_ASSERT_EQUAL(NATPMP_RESPONSE_MAP, response.value.type);
  TEST_ASSERT_EQUAL_UINT32(9, response.value.epoch);

  auto const map = response.value.map;
  TEST_ASSERT_EQUAL(NATPMP_OPCODE_MAP_TCP, map.opcode);
  TEST_ASSERT_EQUAL_UINT16(0x1234, map.internal_port);
  TEST_ASSERT_EQUAL_UINT16(0x9ABC, map.external_port);
  TEST_ASSERT_EQUAL_UINT32(3600, map.lifetime);
}

static void test_packet_map_response_udp() {
  uint8_t const data[16] = {0, 129};
  auto const response = read(sizeof(data), data);
  TEST_ASSERT_TRUE(response.has_value);
  TEST_ASSERT_EQUAL(NATPMP_OPCODE_MAP_UDP, response.value.map.opcode);
}

static void test_packet_address_error() {
  // with the address zeroed
  uint8_t const data[] = {0, 128, 0, 3, 0, 0, 0, 5, 0, 0, 0, 0};
  auto const response = read(sizeof(data), data);
  TEST_ASSERT_TRUE(response.has_value);
  TEST_ASSERT_EQUAL(NATPMP_RESPONSE_ERROR, response.value.type);
  TEST_ASSERT_EQUAL_UINT32(5, response.value.epoch);
  TEST_ASSERT_EQUAL_UINT16(3, response.value.error.result);
  TEST_ASSERT_EQUAL(NATPMP_OPCODE_EXTERNAL_ADDRESS,
                    response.value.error.opcode);
}

static void test_packet_map_error() {
  uint8_t const data[] = {0,    130,  0, 2, 0, 0, 0, 5,
                          0x12, 0x34, 0, 0, 0, 0, 0, 0};
  auto const response = read(sizeof(data), data);
  TEST_ASSERT_TRUE(response.has_value);
  TEST_ASSERT_EQUAL(NATPMP_RESPONSE_ERROR, response.value.type);
  TEST_ASSERT_EQUAL_UINT16(2, response.value.error.result);
  TEST_ASSERT_EQUAL(NATPMP_OPCODE_MAP_TCP, response.value.error.opcode);
}

static void test_packet_undefined_result() {
  // an error like any other (section 3.5)
  uint8_t const data[] = {0, 128, 0x12, 0x34, 0, 0, 0, 5, 0, 0, 0, 0};
  auto const response = read(sizeof(data), data);
  TEST_ASSERT_TRUE(response.has_value);
  TEST_ASSERT_EQUAL(NATPMP_RESPONSE_ERROR, response.value.type);
  TEST_ASSERT_EQUAL_UINT16(0x1234, response.value.error.result);
}

static void test_packet_rejects_bare_unsupported_version() {
  // RFC 6886 shows Unsupported Version with OP 0 (section 3.5), which erratum
  // 3618 corrects to 128. It only answers a request whose version isn't 0
  // anyway, which we never send.
  uint8_t const data[] = {0, 0, 0, 1, 0, 0, 0, 5};
  TEST_ASSERT_FALSE(read(sizeof(data), data).has_value);
}

static void test_packet_rejects_missing_body() {
  // errors have their body too, only it doesn't mean anything
  uint8_t const success[] = {0, 128, 0, 0, 0, 0, 0, 5};
  uint8_t const error[] = {0, 129, 0, 2, 0, 0, 0, 5};
  TEST_ASSERT_FALSE(read(sizeof(success), success).has_value);
  TEST_ASSERT_FALSE(read(sizeof(error), error).has_value);
}

static void test_packet_rejects_incomplete_body() {
  uint8_t const data[] = {0, 130, 0, 2, 0, 0, 0, 5, 0x12, 0x34};
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
  TEST_ASSERT_FALSE(read(8, data).has_value);
  TEST_ASSERT_FALSE(read(9, data).has_value);
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
  RUN_TEST(test_packet_address_error);
  RUN_TEST(test_packet_map_error);
  RUN_TEST(test_packet_undefined_result);
  RUN_TEST(test_packet_rejects_bare_unsupported_version);
  RUN_TEST(test_packet_rejects_missing_body);
  RUN_TEST(test_packet_rejects_incomplete_body);
  RUN_TEST(test_packet_rejects_requests);
  RUN_TEST(test_packet_rejects_other_version);
  RUN_TEST(test_packet_rejects_unknown_opcode);
  RUN_TEST(test_packet_rejects_wrong_sizes);
  RUN_TEST(test_packet_rejects_mismatched_body);
}
