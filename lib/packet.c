#include "packet.h"

#include <assert.h>
#include <string.h>

size_t natpmp_packet_write_address_request(view_t dst) {
  assert(dst.data);

  struct natpmp_address_request const request = {
      .opcode = NATPMP_OPCODE_EXTERNAL_ADDRESS,
  };

  size_t written = 0;
  [[maybe_unused]] auto const status =
      natpmp_address_request_encode(&request, dst.data, dst.size, &written);
  assert(status == NATPMP_OK);

  return written;
}

size_t natpmp_packet_write_map_request(view_t dst,
                                       enum natpmp_opcode opcode,
                                       uint16_t internal_port,
                                       uint16_t external_port,
                                       uint32_t lifetime) {
  assert(dst.data);
  assert(opcode == NATPMP_OPCODE_MAP_UDP || opcode == NATPMP_OPCODE_MAP_TCP);

  struct natpmp_map_request const request = {
      .opcode = opcode,
      .internal_port = internal_port,
      .external_port = external_port,
      .lifetime = lifetime,
  };

  size_t written = 0;
  [[maybe_unused]] auto const status =
      natpmp_map_request_encode(&request, dst.data, dst.size, &written);
  assert(status == NATPMP_OK);

  return written;
}

// Unsupported Version is just the header, with OP 0 rather than 128 (section
// 3.5)
static bool natpmp_packet_is_unsupported_version(
    struct natpmp_response_header const *header) {
  return header->r == 0 && header->opcode == NATPMP_OPCODE_EXTERNAL_ADDRESS &&
         header->result == NATPMP_RESULT_UNSUPPORTED_VERSION &&
         header->body.len == 0;
}

// The body of a response to one of our requests. Errors may leave it out.
static bool natpmp_packet_read_body(struct natpmp_response_header const *header,
                                    natpmp_response_t *out) {
  auto const body = header->body;
  if (body.len == 0) {
    out->has_body = false;
    return header->result != NATPMP_RESULT_SUCCESS;
  }

  out->has_body = true;
  switch (out->answers) {
  case NATPMP_RESPONSE_TO_ADDRESS: {
    struct natpmp_address_response address;
    if (natpmp_address_response_decode(&address, body.ptr, body.len) !=
        NATPMP_OK) {
      return false;
    }

    memcpy(out->body.address, address.external_ip, sizeof(out->body.address));
    return true;
  }
  case NATPMP_RESPONSE_TO_MAP: {
    struct natpmp_map_response map;
    if (natpmp_map_response_decode(&map, body.ptr, body.len) != NATPMP_OK) {
      return false;
    }

    out->body.map.internal_port = map.internal_port;
    out->body.map.external_port = map.external_port;
    out->body.map.lifetime = map.lifetime;

    return true;
  }
  case NATPMP_RESPONSE_TO_ANY:
    break;
  }
  unreachable();
}

natpmp_optional_response_t natpmp_packet_read_response(const_view_t src) {
  assert(src.data);

  natpmp_optional_response_t result = {.has_value = false};

  // the generated decoder checks the size limits and the version
  struct natpmp_response_header header;
  if (natpmp_response_header_decode(&header, src.data, src.size) != NATPMP_OK) {
    return result;
  }

  auto const out = &result.value;
  out->epoch = header.epoch;
  out->result = header.result;

  if (natpmp_packet_is_unsupported_version(&header)) {
    out->answers = NATPMP_RESPONSE_TO_ANY;
    out->has_body = false;
    result.has_value = true;
    return result;
  }

  // anything else without the R bit is a request, not meant for us
  if (header.r != 1) {
    return result;
  }

  switch (header.opcode) {
  case NATPMP_OPCODE_EXTERNAL_ADDRESS:
    out->answers = NATPMP_RESPONSE_TO_ADDRESS;
    break;
  case NATPMP_OPCODE_MAP_UDP:
  case NATPMP_OPCODE_MAP_TCP:
    out->answers = NATPMP_RESPONSE_TO_MAP;
    out->opcode = header.opcode;
    break;
  default:
    // we never send any other opcode
    return result;
  }

  result.has_value = natpmp_packet_read_body(&header, out);
  return result;
}
