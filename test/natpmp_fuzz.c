/* Generated from natpmp.pkt -- do not edit.
 *
 * libFuzzer harness for the code in natpmp_gen.c. Each input is used twice:
 * as a packet to decode (decode_<message>), and as a recipe for building
 * a valid message to encode (encode_<message>).
 *
 *   clang -std=c23 -g -fsanitize=fuzzer,address,undefined \
 *       natpmp_fuzz.c natpmp_gen.c -o natpmp_fuzz
 */
#include <stdlib.h>
#include <string.h>

#include "natpmp_gen.h"

/* Recipe input: choices are read from [p, end); past the end, they are 0. */
struct src {
    const uint8_t *p;
    const uint8_t *end;
};

static uint8_t take_u8(struct src *in) {
    return in->p < in->end ? *in->p++ : 0;
}

static uint32_t take_u32(struct src *in) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++)
        v = v << 8 | take_u8(in);
    return v;
}

static void take_raw(struct src *in, uint8_t *dst, size_t n) {
    for (size_t i = 0; i < n; i++)
        dst[i] = take_u8(in);
}

static volatile uint8_t sink;

static void walk_bytes(const struct natpmp_bytes *v) {
    if (v->len != 0)
        sink = v->ptr[v->len - 1];
}

/* A view into the recipe itself: up to max bytes, as many as remain. */
static void take_bytes(struct src *in, struct natpmp_bytes *v, size_t max) {
    size_t n = (size_t)take_u8(in) << 8 | take_u8(in);
    size_t left = (size_t)(in->end - in->p);
    n = n % (max < SIZE_MAX ? max + 1 : SIZE_MAX);
    v->len = n < left ? n : left;
    v->ptr = in->p;
    in->p += v->len;
}

static void walk_address_request(const struct natpmp_address_request *v) {
    (void)v;
}

static void walk_map_request(const struct natpmp_map_request *v) {
    (void)v;
}

static void walk_response_header(const struct natpmp_response_header *v) {
    walk_bytes(&v->body);
}

static void walk_address_response(const struct natpmp_address_response *v) {
    (void)v;
}

static void walk_map_response(const struct natpmp_map_response *v) {
    (void)v;
}

static void build_address_request(struct src *in, struct natpmp_address_request *v) {
    v->opcode = (enum natpmp_opcode)(take_u32(in) & 0x7f);
}

static void build_map_request(struct src *in, struct natpmp_map_request *v) {
    v->opcode = (enum natpmp_opcode)(take_u32(in) & 0x7f);
    v->internal_port = (uint16_t)(take_u32(in) & 0xffff);
    v->external_port = (uint16_t)(take_u32(in) & 0xffff);
    v->lifetime = (uint32_t)(take_u32(in) & 0xffffffff);
}

static void build_response_header(struct src *in, struct natpmp_response_header *v) {
    v->r = (uint8_t)(take_u32(in) & 0x1);
    v->opcode = (enum natpmp_opcode)(take_u32(in) & 0x7f);
    v->result = (enum natpmp_result)(take_u32(in) & 0xffff);
    v->epoch = (uint32_t)(take_u32(in) & 0xffffffff);
    take_bytes(in, &v->body, SIZE_MAX);
}

static void build_address_response(struct src *in, struct natpmp_address_response *v) {
    take_raw(in, v->external_ip, 4);
}

static void build_map_response(struct src *in, struct natpmp_map_response *v) {
    v->internal_port = (uint16_t)(take_u32(in) & 0xffff);
    v->external_port = (uint16_t)(take_u32(in) & 0xffff);
    v->lifetime = (uint32_t)(take_u32(in) & 0xffffffff);
}

/* Decode then encode must give back the input exactly. */
static void decode_address_request(const uint8_t *buf, size_t size) {
    struct natpmp_address_request msg;
    if (natpmp_address_request_decode(&msg, buf, size) != NATPMP_OK)
        return;
    walk_address_request(&msg);
    if (natpmp_address_request_encoded_size(&msg) != size)
        abort();
    uint8_t *a = malloc(size != 0 ? size : 1);
    uint8_t *b = malloc(size != 0 ? size : 1);
    size_t n = 0;
    if (natpmp_address_request_encode(&msg, a, size, &n) != NATPMP_OK || n != size)
        abort();
    if (size != 0 &&
        natpmp_address_request_encode(&msg, b, size - 1, &n) != NATPMP_NOSPACE)
        abort();
    if (memcmp(a, buf, size) != 0)
        abort();
    free(a);
    free(b);
}

/* Build a valid AddressRequest from the input, then encode it: it must take
 * exactly encoded_size bytes, decode, and encode to identical bytes. */
static void encode_address_request(const uint8_t *data, size_t len) {
    struct src in = { data, data + len };
    struct natpmp_address_request msg = {};
    build_address_request(&in, &msg);
    size_t size = natpmp_address_request_encoded_size(&msg);
    size_t n = 0;
    if (size > NATPMP_ADDRESS_REQUEST_MAX_SIZE) {
        if (natpmp_address_request_encode(&msg, NULL, 0, &n) != NATPMP_INVALID)
            abort();
        return;
    }
    uint8_t *a = malloc(size != 0 ? size : 1);
    uint8_t *b = malloc(size != 0 ? size : 1);
    if (natpmp_address_request_encode(&msg, a, size, &n) != NATPMP_OK || n != size)
        abort();
    struct natpmp_address_request back;
    if (natpmp_address_request_decode(&back, a, size) != NATPMP_OK)
        abort();
    if (natpmp_address_request_encode(&back, b, size, &n) != NATPMP_OK || n != size ||
        memcmp(a, b, size) != 0)
        abort();
    free(a);
    free(b);
}

/* MapRequest has reserved fields or padding, which decode ignores and
 * encode writes as zeros, so the first encode may differ from the
 * input there. It must still have the same length, decode again, and
 * encode to identical bytes. */
static void decode_map_request(const uint8_t *buf, size_t size) {
    struct natpmp_map_request msg;
    if (natpmp_map_request_decode(&msg, buf, size) != NATPMP_OK)
        return;
    walk_map_request(&msg);
    if (natpmp_map_request_encoded_size(&msg) != size)
        abort();
    uint8_t *a = malloc(size != 0 ? size : 1);
    uint8_t *b = malloc(size != 0 ? size : 1);
    size_t n = 0;
    if (natpmp_map_request_encode(&msg, a, size, &n) != NATPMP_OK || n != size)
        abort();
    if (size != 0 &&
        natpmp_map_request_encode(&msg, b, size - 1, &n) != NATPMP_NOSPACE)
        abort();
    struct natpmp_map_request again;
    if (natpmp_map_request_decode(&again, a, size) != NATPMP_OK)
        abort();
    if (natpmp_map_request_encode(&again, b, size, &n) != NATPMP_OK || n != size ||
        memcmp(a, b, size) != 0)
        abort();
    free(a);
    free(b);
}

/* Build a valid MapRequest from the input, then encode it: it must take
 * exactly encoded_size bytes, decode, and encode to identical bytes. */
static void encode_map_request(const uint8_t *data, size_t len) {
    struct src in = { data, data + len };
    struct natpmp_map_request msg = {};
    build_map_request(&in, &msg);
    size_t size = natpmp_map_request_encoded_size(&msg);
    size_t n = 0;
    if (size > NATPMP_MAP_REQUEST_MAX_SIZE) {
        if (natpmp_map_request_encode(&msg, NULL, 0, &n) != NATPMP_INVALID)
            abort();
        return;
    }
    uint8_t *a = malloc(size != 0 ? size : 1);
    uint8_t *b = malloc(size != 0 ? size : 1);
    if (natpmp_map_request_encode(&msg, a, size, &n) != NATPMP_OK || n != size)
        abort();
    struct natpmp_map_request back;
    if (natpmp_map_request_decode(&back, a, size) != NATPMP_OK)
        abort();
    if (natpmp_map_request_encode(&back, b, size, &n) != NATPMP_OK || n != size ||
        memcmp(a, b, size) != 0)
        abort();
    free(a);
    free(b);
}

/* Decode then encode must give back the input exactly. */
static void decode_response_header(const uint8_t *buf, size_t size) {
    struct natpmp_response_header msg;
    if (natpmp_response_header_decode(&msg, buf, size) != NATPMP_OK)
        return;
    walk_response_header(&msg);
    if (natpmp_response_header_encoded_size(&msg) != size)
        abort();
    uint8_t *a = malloc(size != 0 ? size : 1);
    uint8_t *b = malloc(size != 0 ? size : 1);
    size_t n = 0;
    if (natpmp_response_header_encode(&msg, a, size, &n) != NATPMP_OK || n != size)
        abort();
    if (size != 0 &&
        natpmp_response_header_encode(&msg, b, size - 1, &n) != NATPMP_NOSPACE)
        abort();
    if (memcmp(a, buf, size) != 0)
        abort();
    free(a);
    free(b);
}

/* Build a valid ResponseHeader from the input, then encode it: it must take
 * exactly encoded_size bytes, decode, and encode to identical bytes. */
static void encode_response_header(const uint8_t *data, size_t len) {
    struct src in = { data, data + len };
    struct natpmp_response_header msg = {};
    build_response_header(&in, &msg);
    size_t size = natpmp_response_header_encoded_size(&msg);
    size_t n = 0;
    if (size > NATPMP_RESPONSE_HEADER_MAX_SIZE) {
        if (natpmp_response_header_encode(&msg, NULL, 0, &n) != NATPMP_INVALID)
            abort();
        return;
    }
    uint8_t *a = malloc(size != 0 ? size : 1);
    uint8_t *b = malloc(size != 0 ? size : 1);
    if (natpmp_response_header_encode(&msg, a, size, &n) != NATPMP_OK || n != size)
        abort();
    struct natpmp_response_header back;
    if (natpmp_response_header_decode(&back, a, size) != NATPMP_OK)
        abort();
    if (natpmp_response_header_encode(&back, b, size, &n) != NATPMP_OK || n != size ||
        memcmp(a, b, size) != 0)
        abort();
    free(a);
    free(b);
}

/* Decode then encode must give back the input exactly. */
static void decode_address_response(const uint8_t *buf, size_t size) {
    struct natpmp_address_response msg;
    if (natpmp_address_response_decode(&msg, buf, size) != NATPMP_OK)
        return;
    walk_address_response(&msg);
    if (natpmp_address_response_encoded_size(&msg) != size)
        abort();
    uint8_t *a = malloc(size != 0 ? size : 1);
    uint8_t *b = malloc(size != 0 ? size : 1);
    size_t n = 0;
    if (natpmp_address_response_encode(&msg, a, size, &n) != NATPMP_OK || n != size)
        abort();
    if (size != 0 &&
        natpmp_address_response_encode(&msg, b, size - 1, &n) != NATPMP_NOSPACE)
        abort();
    if (memcmp(a, buf, size) != 0)
        abort();
    free(a);
    free(b);
}

/* Build a valid AddressResponse from the input, then encode it: it must take
 * exactly encoded_size bytes, decode, and encode to identical bytes. */
static void encode_address_response(const uint8_t *data, size_t len) {
    struct src in = { data, data + len };
    struct natpmp_address_response msg = {};
    build_address_response(&in, &msg);
    size_t size = natpmp_address_response_encoded_size(&msg);
    size_t n = 0;
    if (size > NATPMP_ADDRESS_RESPONSE_MAX_SIZE) {
        if (natpmp_address_response_encode(&msg, NULL, 0, &n) != NATPMP_INVALID)
            abort();
        return;
    }
    uint8_t *a = malloc(size != 0 ? size : 1);
    uint8_t *b = malloc(size != 0 ? size : 1);
    if (natpmp_address_response_encode(&msg, a, size, &n) != NATPMP_OK || n != size)
        abort();
    struct natpmp_address_response back;
    if (natpmp_address_response_decode(&back, a, size) != NATPMP_OK)
        abort();
    if (natpmp_address_response_encode(&back, b, size, &n) != NATPMP_OK || n != size ||
        memcmp(a, b, size) != 0)
        abort();
    free(a);
    free(b);
}

/* Decode then encode must give back the input exactly. */
static void decode_map_response(const uint8_t *buf, size_t size) {
    struct natpmp_map_response msg;
    if (natpmp_map_response_decode(&msg, buf, size) != NATPMP_OK)
        return;
    walk_map_response(&msg);
    if (natpmp_map_response_encoded_size(&msg) != size)
        abort();
    uint8_t *a = malloc(size != 0 ? size : 1);
    uint8_t *b = malloc(size != 0 ? size : 1);
    size_t n = 0;
    if (natpmp_map_response_encode(&msg, a, size, &n) != NATPMP_OK || n != size)
        abort();
    if (size != 0 &&
        natpmp_map_response_encode(&msg, b, size - 1, &n) != NATPMP_NOSPACE)
        abort();
    if (memcmp(a, buf, size) != 0)
        abort();
    free(a);
    free(b);
}

/* Build a valid MapResponse from the input, then encode it: it must take
 * exactly encoded_size bytes, decode, and encode to identical bytes. */
static void encode_map_response(const uint8_t *data, size_t len) {
    struct src in = { data, data + len };
    struct natpmp_map_response msg = {};
    build_map_response(&in, &msg);
    size_t size = natpmp_map_response_encoded_size(&msg);
    size_t n = 0;
    if (size > NATPMP_MAP_RESPONSE_MAX_SIZE) {
        if (natpmp_map_response_encode(&msg, NULL, 0, &n) != NATPMP_INVALID)
            abort();
        return;
    }
    uint8_t *a = malloc(size != 0 ? size : 1);
    uint8_t *b = malloc(size != 0 ? size : 1);
    if (natpmp_map_response_encode(&msg, a, size, &n) != NATPMP_OK || n != size)
        abort();
    struct natpmp_map_response back;
    if (natpmp_map_response_decode(&back, a, size) != NATPMP_OK)
        abort();
    if (natpmp_map_response_encode(&back, b, size, &n) != NATPMP_OK || n != size ||
        memcmp(a, b, size) != 0)
        abort();
    free(a);
    free(b);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    /* An exact-size heap copy, so ASan catches any overread. */
    uint8_t *buf = malloc(size != 0 ? size : 1);
    if (size != 0)
        memcpy(buf, data, size);
    decode_address_request(buf, size);
    encode_address_request(buf, size);
    decode_map_request(buf, size);
    encode_map_request(buf, size);
    decode_response_header(buf, size);
    encode_response_header(buf, size);
    decode_address_response(buf, size);
    encode_address_response(buf, size);
    decode_map_response(buf, size);
    encode_map_response(buf, size);
    free(buf);
    return 0;
}
