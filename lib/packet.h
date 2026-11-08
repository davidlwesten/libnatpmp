#pragma once

#include <stddef.h>
#include <stdint.h>

#include "natpmp_gen.h"
#include "view.h"

// Reading and writing NAT-PMP packets (RFC 6886) on top of the generated codec,
// with the rules the schema can't express

/**
 * @brief Write an external address request (section 3.2)
 *
 * @returns The size of the request
 */
size_t natpmp_packet_write_address_request(view_t dst);

/**
 * @brief Write a mapping request (section 3.3), or a delete if `lifetime` is 0
 *        (section 3.4)
 *
 * @param[in] opcode NATPMP_OPCODE_MAP_UDP or NATPMP_OPCODE_MAP_TCP
 *
 * @returns The size of the request
 */
size_t natpmp_packet_write_map_request(view_t dst,
                                       enum natpmp_opcode opcode,
                                       uint16_t internal_port,
                                       uint16_t external_port,
                                       uint32_t lifetime);

/**
 * @brief A response as far as a client cares about it
 */
typedef struct natpmp_response {
  uint32_t epoch;
  /// Can be a code RFC 6886 doesn't define
  uint16_t result;
  enum {
    /// Unsupported Version, which doesn't say which request it answers
    NATPMP_RESPONSE_TO_ANY,
    NATPMP_RESPONSE_TO_ADDRESS,
    NATPMP_RESPONSE_TO_MAP,
  } answers;
  /// NATPMP_OPCODE_MAP_UDP or NATPMP_OPCODE_MAP_TCP, set for
  /// NATPMP_RESPONSE_TO_MAP
  enum natpmp_opcode opcode;
  /// Always true on success. Errors may come without one.
  bool has_body;
  union {
    /// The external IPv4 address, for NATPMP_RESPONSE_TO_ADDRESS
    uint8_t address[4];
    /// For NATPMP_RESPONSE_TO_MAP
    struct {
      uint16_t internal_port;
      uint16_t external_port;
      uint32_t lifetime;
    } map;
  } body;
} natpmp_response_t;

typedef struct natpmp_optional_response {
  bool has_value;
  natpmp_response_t value;
} natpmp_optional_response_t;

/**
 * @brief Parse a datagram received from a NAT-PMP gateway
 *
 * Only checks the format, not the result code.
 *
 * @returns The response, or nothing if `src` isn't a valid one and should be
 *          ignored
 */
natpmp_optional_response_t natpmp_packet_read_response(const_view_t src);
