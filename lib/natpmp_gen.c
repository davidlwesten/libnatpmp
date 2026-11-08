/* Generated from natpmp.pkt -- do not edit. */
#include "natpmp_gen.h"

#include <string.h>

/* ---- wire primitives ---------------------------------------------------- */

/* Read cursor over [p, end). Every read checks bounds first. */
struct natpmp_rd {
    const uint8_t *p;
    const uint8_t *end;
};

/* Write cursor over [p, end). */
struct natpmp_wr {
    uint8_t *p;
    uint8_t *end;
};

static size_t natpmp_rd_left(const struct natpmp_rd *r) {
    return (size_t)(r->end - r->p);
}

static bool natpmp_rd_u8(struct natpmp_rd *r, uint8_t *v) {
    if (natpmp_rd_left(r) < 1)
        return false;
    *v = r->p[0];
    r->p += 1;
    return true;
}

static bool natpmp_rd_u16(struct natpmp_rd *r, uint16_t *v) {
    if (natpmp_rd_left(r) < 2)
        return false;
    *v = (uint16_t)((uint16_t)r->p[0] << 8 | r->p[1]);
    r->p += 2;
    return true;
}

static bool natpmp_rd_u32(struct natpmp_rd *r, uint32_t *v) {
    if (natpmp_rd_left(r) < 4)
        return false;
    *v = (uint32_t)r->p[0] << 24 | (uint32_t)r->p[1] << 16
       | (uint32_t)r->p[2] << 8 | r->p[3];
    r->p += 4;
    return true;
}

static bool natpmp_rd_skip(struct natpmp_rd *r, size_t n) {
    if (natpmp_rd_left(r) < n)
        return false;
    r->p += n;
    return true;
}

static bool natpmp_rd_raw(struct natpmp_rd *r, uint8_t *dst, size_t n) {
    if (natpmp_rd_left(r) < n)
        return false;
    memcpy(dst, r->p, n);
    r->p += n;
    return true;
}

static bool natpmp_rd_rest(struct natpmp_rd *r, struct natpmp_bytes *v) {
    v->ptr = r->p;
    v->len = natpmp_rd_left(r);
    r->p = r->end;
    return true;
}

static size_t natpmp_wr_left(const struct natpmp_wr *w) {
    return (size_t)(w->end - w->p);
}

static natpmp_status natpmp_wr_u8(struct natpmp_wr *w, uint8_t v) {
    if (natpmp_wr_left(w) < 1)
        return NATPMP_NOSPACE;
    w->p[0] = v;
    w->p += 1;
    return NATPMP_OK;
}

static natpmp_status natpmp_wr_u16(struct natpmp_wr *w, uint16_t v) {
    if (natpmp_wr_left(w) < 2)
        return NATPMP_NOSPACE;
    w->p[0] = (uint8_t)(v >> 8);
    w->p[1] = (uint8_t)v;
    w->p += 2;
    return NATPMP_OK;
}

static natpmp_status natpmp_wr_u32(struct natpmp_wr *w, uint32_t v) {
    if (natpmp_wr_left(w) < 4)
        return NATPMP_NOSPACE;
    w->p[0] = (uint8_t)(v >> 24);
    w->p[1] = (uint8_t)(v >> 16);
    w->p[2] = (uint8_t)(v >> 8);
    w->p[3] = (uint8_t)v;
    w->p += 4;
    return NATPMP_OK;
}

static natpmp_status natpmp_wr_raw(struct natpmp_wr *w, const void *src, size_t n) {
    if (natpmp_wr_left(w) < n)
        return NATPMP_NOSPACE;
    if (n != 0)
        memcpy(w->p, src, n);
    w->p += n;
    return NATPMP_OK;
}

static natpmp_status natpmp_wr_zero(struct natpmp_wr *w, size_t n) {
    if (natpmp_wr_left(w) < n)
        return NATPMP_NOSPACE;
    if (n != 0)
        memset(w->p, 0, n);
    w->p += n;
    return NATPMP_OK;
}

#define NATPMP_CHECK(cond) \
    do { \
        if (!(cond)) \
            return false; \
    } while (0)

#define NATPMP_TRY(expr) \
    do { \
        natpmp_status st_ = (expr); \
        if (st_ != NATPMP_OK) \
            return st_; \
    } while (0)

/* ---- message AddressRequest --------------------------------------------- */

static bool natpmp_address_request_read(struct natpmp_rd *r,
                                        struct natpmp_address_request *out) {
    uint8_t bits0;
    NATPMP_CHECK(natpmp_rd_u8(r, &bits0));
    NATPMP_CHECK(bits0 == 0);
    uint8_t bits1;
    NATPMP_CHECK(natpmp_rd_u8(r, &bits1));
    NATPMP_CHECK((bits1 >> 7) == 0);
    out->opcode = (enum natpmp_opcode)(bits1 & 0x7f);
    return true;
}

static natpmp_status natpmp_address_request_write(
    struct natpmp_wr *w, const struct natpmp_address_request *in) {
    NATPMP_TRY(natpmp_wr_u8(w, (uint8_t)(0)));
    if (in->opcode > 0x7f)
        return NATPMP_INVALID;
    return natpmp_wr_u8(w, (uint8_t)(in->opcode));
}

static size_t natpmp_address_request_size(
    const struct natpmp_address_request *in) {
    (void)in;
    return 2;
}

natpmp_status natpmp_address_request_decode(struct natpmp_address_request *out,
                                            const uint8_t *buf, size_t len) {
    struct natpmp_rd r = { buf, len != 0 ? buf + len : buf };
    /* The message must account for every byte of the input. */
    if (!natpmp_address_request_read(&r, out) || r.p != r.end)
        return NATPMP_INVALID;
    return NATPMP_OK;
}

natpmp_status natpmp_address_request_encode(
    const struct natpmp_address_request *in, uint8_t *buf, size_t cap,
    size_t *written) {
    struct natpmp_wr w = { buf, cap != 0 ? buf + cap : buf };
    NATPMP_TRY(natpmp_address_request_write(&w, in));
    *written = (size_t)(w.p - buf);
    return NATPMP_OK;
}

size_t natpmp_address_request_encoded_size(
    const struct natpmp_address_request *in) {
    return natpmp_address_request_size(in);
}

/* ---- message MapRequest ------------------------------------------------- */

static bool natpmp_map_request_read(struct natpmp_rd *r,
                                    struct natpmp_map_request *out) {
    uint8_t bits0;
    NATPMP_CHECK(natpmp_rd_u8(r, &bits0));
    NATPMP_CHECK(bits0 == 0);
    uint8_t bits1;
    NATPMP_CHECK(natpmp_rd_u8(r, &bits1));
    NATPMP_CHECK((bits1 >> 7) == 0);
    out->opcode = (enum natpmp_opcode)(bits1 & 0x7f);
    NATPMP_CHECK(natpmp_rd_skip(r, 2));
    NATPMP_CHECK(natpmp_rd_u16(r, &out->internal_port));
    NATPMP_CHECK(natpmp_rd_u16(r, &out->external_port));
    return natpmp_rd_u32(r, &out->lifetime);
}

static natpmp_status natpmp_map_request_write(
    struct natpmp_wr *w, const struct natpmp_map_request *in) {
    NATPMP_TRY(natpmp_wr_u8(w, (uint8_t)(0)));
    if (in->opcode > 0x7f)
        return NATPMP_INVALID;
    NATPMP_TRY(natpmp_wr_u8(w, (uint8_t)(in->opcode)));
    NATPMP_TRY(natpmp_wr_zero(w, 2));
    NATPMP_TRY(natpmp_wr_u16(w, in->internal_port));
    NATPMP_TRY(natpmp_wr_u16(w, in->external_port));
    return natpmp_wr_u32(w, in->lifetime);
}

static size_t natpmp_map_request_size(const struct natpmp_map_request *in) {
    (void)in;
    return 12;
}

natpmp_status natpmp_map_request_decode(struct natpmp_map_request *out,
                                        const uint8_t *buf, size_t len) {
    struct natpmp_rd r = { buf, len != 0 ? buf + len : buf };
    /* The message must account for every byte of the input. */
    if (!natpmp_map_request_read(&r, out) || r.p != r.end)
        return NATPMP_INVALID;
    return NATPMP_OK;
}

natpmp_status natpmp_map_request_encode(const struct natpmp_map_request *in,
                                        uint8_t *buf, size_t cap,
                                        size_t *written) {
    struct natpmp_wr w = { buf, cap != 0 ? buf + cap : buf };
    NATPMP_TRY(natpmp_map_request_write(&w, in));
    *written = (size_t)(w.p - buf);
    return NATPMP_OK;
}

size_t natpmp_map_request_encoded_size(const struct natpmp_map_request *in) {
    return natpmp_map_request_size(in);
}

/* ---- message ResponseHeader --------------------------------------------- */

static bool natpmp_response_header_read(struct natpmp_rd *r,
                                        struct natpmp_response_header *out) {
    uint8_t bits0;
    NATPMP_CHECK(natpmp_rd_u8(r, &bits0));
    NATPMP_CHECK(bits0 == 0);
    uint8_t bits1;
    NATPMP_CHECK(natpmp_rd_u8(r, &bits1));
    out->r = (uint8_t)(bits1 >> 7);
    out->opcode = (enum natpmp_opcode)(bits1 & 0x7f);
    NATPMP_CHECK(natpmp_rd_u16(r, &out->result));
    NATPMP_CHECK(natpmp_rd_u32(r, &out->epoch));
    return natpmp_rd_rest(r, &out->body);
}

static natpmp_status natpmp_response_header_write(
    struct natpmp_wr *w, const struct natpmp_response_header *in) {
    NATPMP_TRY(natpmp_wr_u8(w, (uint8_t)(0)));
    if (in->r > 0x1)
        return NATPMP_INVALID;
    if (in->opcode > 0x7f)
        return NATPMP_INVALID;
    NATPMP_TRY(natpmp_wr_u8(w, (uint8_t)(((uint8_t)in->r << 7) | in->opcode)));
    NATPMP_TRY(natpmp_wr_u16(w, in->result));
    NATPMP_TRY(natpmp_wr_u32(w, in->epoch));
    return natpmp_wr_raw(w, in->body.ptr, in->body.len);
}

static size_t natpmp_response_header_size(
    const struct natpmp_response_header *in) {
    return 8 + in->body.len;
}

natpmp_status natpmp_response_header_decode(struct natpmp_response_header *out,
                                            const uint8_t *buf, size_t len) {
    if (len > NATPMP_RESPONSE_HEADER_MAX_SIZE)
        return NATPMP_INVALID;
    struct natpmp_rd r = { buf, len != 0 ? buf + len : buf };
    /* The message must account for every byte of the input. */
    if (!natpmp_response_header_read(&r, out) || r.p != r.end)
        return NATPMP_INVALID;
    return NATPMP_OK;
}

natpmp_status natpmp_response_header_encode(
    const struct natpmp_response_header *in, uint8_t *buf, size_t cap,
    size_t *written) {
    if (natpmp_response_header_size(in) > NATPMP_RESPONSE_HEADER_MAX_SIZE)
        return NATPMP_INVALID;
    struct natpmp_wr w = { buf, cap != 0 ? buf + cap : buf };
    NATPMP_TRY(natpmp_response_header_write(&w, in));
    *written = (size_t)(w.p - buf);
    return NATPMP_OK;
}

size_t natpmp_response_header_encoded_size(
    const struct natpmp_response_header *in) {
    return natpmp_response_header_size(in);
}

/* ---- message AddressResponse -------------------------------------------- */

static bool natpmp_address_response_read(struct natpmp_rd *r,
                                         struct natpmp_address_response *out) {
    return natpmp_rd_raw(r, out->external_ip, 4);
}

static natpmp_status natpmp_address_response_write(
    struct natpmp_wr *w, const struct natpmp_address_response *in) {
    return natpmp_wr_raw(w, in->external_ip, 4);
}

static size_t natpmp_address_response_size(
    const struct natpmp_address_response *in) {
    (void)in;
    return 4;
}

natpmp_status natpmp_address_response_decode(
    struct natpmp_address_response *out, const uint8_t *buf, size_t len) {
    struct natpmp_rd r = { buf, len != 0 ? buf + len : buf };
    /* The message must account for every byte of the input. */
    if (!natpmp_address_response_read(&r, out) || r.p != r.end)
        return NATPMP_INVALID;
    return NATPMP_OK;
}

natpmp_status natpmp_address_response_encode(
    const struct natpmp_address_response *in, uint8_t *buf, size_t cap,
    size_t *written) {
    struct natpmp_wr w = { buf, cap != 0 ? buf + cap : buf };
    NATPMP_TRY(natpmp_address_response_write(&w, in));
    *written = (size_t)(w.p - buf);
    return NATPMP_OK;
}

size_t natpmp_address_response_encoded_size(
    const struct natpmp_address_response *in) {
    return natpmp_address_response_size(in);
}

/* ---- message MapResponse ------------------------------------------------ */

static bool natpmp_map_response_read(struct natpmp_rd *r,
                                     struct natpmp_map_response *out) {
    NATPMP_CHECK(natpmp_rd_u16(r, &out->internal_port));
    NATPMP_CHECK(natpmp_rd_u16(r, &out->external_port));
    return natpmp_rd_u32(r, &out->lifetime);
}

static natpmp_status natpmp_map_response_write(
    struct natpmp_wr *w, const struct natpmp_map_response *in) {
    NATPMP_TRY(natpmp_wr_u16(w, in->internal_port));
    NATPMP_TRY(natpmp_wr_u16(w, in->external_port));
    return natpmp_wr_u32(w, in->lifetime);
}

static size_t natpmp_map_response_size(const struct natpmp_map_response *in) {
    (void)in;
    return 8;
}

natpmp_status natpmp_map_response_decode(struct natpmp_map_response *out,
                                         const uint8_t *buf, size_t len) {
    struct natpmp_rd r = { buf, len != 0 ? buf + len : buf };
    /* The message must account for every byte of the input. */
    if (!natpmp_map_response_read(&r, out) || r.p != r.end)
        return NATPMP_INVALID;
    return NATPMP_OK;
}

natpmp_status natpmp_map_response_encode(const struct natpmp_map_response *in,
                                         uint8_t *buf, size_t cap,
                                         size_t *written) {
    struct natpmp_wr w = { buf, cap != 0 ? buf + cap : buf };
    NATPMP_TRY(natpmp_map_response_write(&w, in));
    *written = (size_t)(w.p - buf);
    return NATPMP_OK;
}

size_t natpmp_map_response_encoded_size(const struct natpmp_map_response *in) {
    return natpmp_map_response_size(in);
}
