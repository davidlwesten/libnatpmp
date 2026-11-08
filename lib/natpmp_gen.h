/* Generated from natpmp.pkt -- do not edit. */
#ifndef NATPMP_GEN_H
#define NATPMP_GEN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    NATPMP_OK = 0,
    /* decode: malformed or truncated input;
     * encode: unencodable input (unknown tag, value too wide for its field,
     *         NUL in cstr, message over its declared maximum size) */
    NATPMP_INVALID,
    /* encode: output buffer too small */
    NATPMP_NOSPACE,
} natpmp_status;

/* enum Opcode : u7 */
enum natpmp_opcode : uint8_t {
    NATPMP_OPCODE_EXTERNAL_ADDRESS = 0,
    NATPMP_OPCODE_MAP_UDP          = 1,
    NATPMP_OPCODE_MAP_TCP          = 2,
};

/* enum Result : u16 */
enum natpmp_result : uint16_t {
    NATPMP_RESULT_SUCCESS             = 0,
    NATPMP_RESULT_UNSUPPORTED_VERSION = 1,
    NATPMP_RESULT_NOT_AUTHORIZED      = 2,
    NATPMP_RESULT_NETWORK_FAILURE     = 3,
    NATPMP_RESULT_OUT_OF_RESOURCES    = 4,
    NATPMP_RESULT_UNSUPPORTED_OPCODE  = 5,
};

/* u8[..] and u8[len]: points into the buffer. */
struct natpmp_bytes {
    const uint8_t *ptr;
    size_t len;
};

/* message AddressRequest { version: u8 = 0; r: u1 = 0; opcode: Opcode; } */
struct natpmp_address_request {
    enum natpmp_opcode opcode;
};

/* Size limits of an encoded AddressRequest, in bytes. */
constexpr size_t NATPMP_ADDRESS_REQUEST_MIN_SIZE = 2;
constexpr size_t NATPMP_ADDRESS_REQUEST_MAX_SIZE = 2;

/* message MapRequest {
 *     version: u8 = 0;
 *     r: u1 = 0;
 *     opcode: Opcode;
 *     _: u16;
 *     internal_port: u16;
 *     external_port: u16;
 *     lifetime: u32;
 * } */
struct natpmp_map_request {
    enum natpmp_opcode opcode;
    uint16_t internal_port;
    uint16_t external_port;
    uint32_t lifetime;
};

/* Size limits of an encoded MapRequest, in bytes. */
constexpr size_t NATPMP_MAP_REQUEST_MIN_SIZE = 12;
constexpr size_t NATPMP_MAP_REQUEST_MAX_SIZE = 12;

/* message ResponseHeader max 16 {
 *     version: u8 = 0;
 *     r: u1;
 *     opcode: Opcode;
 *     result: Result;
 *     epoch: u32;
 *     body: u8[..];
 * } */
struct natpmp_response_header {
    uint8_t r;
    enum natpmp_opcode opcode;
    enum natpmp_result result;
    uint32_t epoch;
    struct natpmp_bytes body;
};

/* Size limits of an encoded ResponseHeader, in bytes. Decode and encode
 * reject a ResponseHeader larger than NATPMP_RESPONSE_HEADER_MAX_SIZE. */
constexpr size_t NATPMP_RESPONSE_HEADER_MIN_SIZE = 8;
constexpr size_t NATPMP_RESPONSE_HEADER_MAX_SIZE = 16;

/* message AddressResponse { external_ip: u8[4]; } */
struct natpmp_address_response {
    uint8_t external_ip[4];
};

/* Size limits of an encoded AddressResponse, in bytes. */
constexpr size_t NATPMP_ADDRESS_RESPONSE_MIN_SIZE = 4;
constexpr size_t NATPMP_ADDRESS_RESPONSE_MAX_SIZE = 4;

/* message MapResponse {
 *     internal_port: u16;
 *     external_port: u16;
 *     lifetime: u32;
 * } */
struct natpmp_map_response {
    uint16_t internal_port;
    uint16_t external_port;
    uint32_t lifetime;
};

/* Size limits of an encoded MapResponse, in bytes. */
constexpr size_t NATPMP_MAP_RESPONSE_MIN_SIZE = 8;
constexpr size_t NATPMP_MAP_RESPONSE_MAX_SIZE = 8;

/* Decode one AddressRequest. Views in *out borrow buf: keep buf alive and
 * unchanged while using them. */
natpmp_status natpmp_address_request_decode(struct natpmp_address_request *out,
                                            const uint8_t *buf, size_t len);

/* Encode into buf[0..cap). On NATPMP_OK, *written is the encoded length. */
natpmp_status natpmp_address_request_encode(
    const struct natpmp_address_request *in, uint8_t *buf, size_t cap,
    size_t *written);

/* Exact number of bytes natpmp_address_request_encode will write, if it succeeds. */
size_t natpmp_address_request_encoded_size(
    const struct natpmp_address_request *in);

/* Decode one MapRequest. Views in *out borrow buf: keep buf alive and
 * unchanged while using them. */
natpmp_status natpmp_map_request_decode(struct natpmp_map_request *out,
                                        const uint8_t *buf, size_t len);

/* Encode into buf[0..cap). On NATPMP_OK, *written is the encoded length. */
natpmp_status natpmp_map_request_encode(const struct natpmp_map_request *in,
                                        uint8_t *buf, size_t cap,
                                        size_t *written);

/* Exact number of bytes natpmp_map_request_encode will write, if it succeeds. */
size_t natpmp_map_request_encoded_size(const struct natpmp_map_request *in);

/* Decode one ResponseHeader. Views in *out borrow buf: keep buf alive and
 * unchanged while using them. */
natpmp_status natpmp_response_header_decode(struct natpmp_response_header *out,
                                            const uint8_t *buf, size_t len);

/* Encode into buf[0..cap). On NATPMP_OK, *written is the encoded length. */
natpmp_status natpmp_response_header_encode(
    const struct natpmp_response_header *in, uint8_t *buf, size_t cap,
    size_t *written);

/* Exact number of bytes natpmp_response_header_encode will write, if it succeeds. */
size_t natpmp_response_header_encoded_size(
    const struct natpmp_response_header *in);

/* Decode one AddressResponse. Views in *out borrow buf: keep buf alive and
 * unchanged while using them. */
natpmp_status natpmp_address_response_decode(
    struct natpmp_address_response *out, const uint8_t *buf, size_t len);

/* Encode into buf[0..cap). On NATPMP_OK, *written is the encoded length. */
natpmp_status natpmp_address_response_encode(
    const struct natpmp_address_response *in, uint8_t *buf, size_t cap,
    size_t *written);

/* Exact number of bytes natpmp_address_response_encode will write, if it succeeds. */
size_t natpmp_address_response_encoded_size(
    const struct natpmp_address_response *in);

/* Decode one MapResponse. Views in *out borrow buf: keep buf alive and
 * unchanged while using them. */
natpmp_status natpmp_map_response_decode(struct natpmp_map_response *out,
                                         const uint8_t *buf, size_t len);

/* Encode into buf[0..cap). On NATPMP_OK, *written is the encoded length. */
natpmp_status natpmp_map_response_encode(const struct natpmp_map_response *in,
                                         uint8_t *buf, size_t cap,
                                         size_t *written);

/* Exact number of bytes natpmp_map_response_encode will write, if it succeeds. */
size_t natpmp_map_response_encoded_size(const struct natpmp_map_response *in);

#endif
