#pragma once

#include <stddef.h>
#include <stdint.h>

#include "address.h"
#include "natpmp_gen.h"
#include "view.h"

// Reading and writing NAT-PMP packets on top of the generated codec,
// with the rules the schema can't express

/**
 * @brief Write an external address request
 *
 * @returns The size of the request
 */
size_t natpmp_packet_write_address_request(view_t dst);

/**
 * @brief Write a mapping request, or a delete if `lifetime` is 0
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
  enum {
    /// A request failed
    NATPMP_RESPONSE_ERROR,
    /// To an external address request, or the gateway announcing a new one
    NATPMP_RESPONSE_ADDRESS,
    /// To a mapping or delete request
    NATPMP_RESPONSE_MAP,
  } type;
  /// Seconds Since Start of Epoch, which every response carries
  uint32_t epoch;
  union {
    struct {
      /// Not SUCCESS, and can be a code RFC 6886 doesn't define
      uint16_t result;
      /// The request that failed
      enum natpmp_opcode opcode;
    } error;
    struct {
      natpmp_ipv4_address_t external_ip;
    } address;
    struct {
      /// NATPMP_OPCODE_MAP_UDP or NATPMP_OPCODE_MAP_TCP
      enum natpmp_opcode opcode;
      uint16_t internal_port;
      uint16_t external_port;
      uint32_t lifetime;
    } map;
  };
} natpmp_response_t;

typedef struct natpmp_optional_response {
  bool has_value;
  natpmp_response_t value;
} natpmp_optional_response_t;

/**
 * @brief Parse a datagram received from a NAT-PMP gateway
 *
 * Every response has the full body for its opcode, also errors, where it's
 * ignored.
 *
 * @returns The response, or nothing if `src` isn't a valid one and should be
 *          ignored
 */
natpmp_optional_response_t natpmp_packet_read_response(const_view_t src);
