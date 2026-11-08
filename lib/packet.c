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

natpmp_optional_response_t natpmp_packet_read_response(const_view_t src) {
  assert(src.data);

  natpmp_optional_response_t result = {.has_value = false};

  // the generated decoders check the version and the sizes
  struct natpmp_response_header header;
  if (natpmp_response_header_decode(&header, src.data, src.size) != NATPMP_OK) {
    return result;
  }

  auto const out = &result.value;
  out->epoch = header.epoch;

  // without the R bit it's a request, not meant for us
  if (header.r != 1) {
    return result;
  }

  // the body for the opcode, which errors have too
  auto const body = header.body;
  struct natpmp_address_response address = {};
  struct natpmp_map_response map = {};
  switch (header.opcode) {
  case NATPMP_OPCODE_EXTERNAL_ADDRESS:
    if (natpmp_address_response_decode(&address, body.ptr, body.len) !=
        NATPMP_OK) {
      return result;
    }
    break;
  case NATPMP_OPCODE_MAP_UDP:
  case NATPMP_OPCODE_MAP_TCP:
    if (natpmp_map_response_decode(&map, body.ptr, body.len) != NATPMP_OK) {
      return result;
    }
    break;
  default:
    // we never send any other opcode
    return result;
  }

  if (header.result != NATPMP_RESULT_SUCCESS) {
    // what the body says is undefined then
    out->type = NATPMP_RESPONSE_ERROR;
    out->error.result = header.result;
    out->error.opcode = header.opcode;
  } else if (header.opcode == NATPMP_OPCODE_EXTERNAL_ADDRESS) {
    out->type = NATPMP_RESPONSE_ADDRESS;
    memcpy(out->address.external_ip.octets, address.external_ip,
           sizeof(out->address.external_ip.octets));
  } else {
    out->type = NATPMP_RESPONSE_MAP;
    out->map.opcode = header.opcode;
    out->map.internal_port = map.internal_port;
    out->map.external_port = map.external_port;
    out->map.lifetime = map.lifetime;
  }
  result.has_value = true;
  return result;
}
