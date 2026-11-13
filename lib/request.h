#pragma once

#include "packet.h"
#include <byo/natpmp/natpmp.h>

/**
 * @brief A request, from when it's made until its last event
 *
 * Mappings live in the session's slots, and the external address request in
 * the session itself. Each moves through these states:
 *
 *   FREE        -> QUEUED    a request is made in it
 *   QUEUED      -> SENT      it's at the head of the queue and goes out
 *   SENT        -> ANSWERED  the gateway answers it
 *   SENT        -> FAILED    the gateway refuses it, or maps it for no time
 *   QUEUED/SENT -> FAILED    the head is given up on or the gateway can't be
 *                            reached, which fails everything queued
 *   FREE/FAILED -> ANSWERED  the gateway announces a new external address, or
 *                            answers with one too late, for the external
 *                            address request
 *   ANSWERED    -> FREE      an external address request's answer is polled
 *   ANSWERED    -> ACTIVE    a mapping's answer is polled
 *   FAILED      -> FREE      its failure is polled
 *   ACTIVE      -> QUEUED    `map.renew_at` passes, and it joins the back of
 *                            the queue
 *
 * Releasing a mapping makes it ask for its deletion, and frees it without an
 * event once that's answered, refused or given up on. A QUEUED one keeps its
 * place in the queue:
 *
 *   ANSWERED/ACTIVE -> QUEUED  it joins the back of the queue
 *   SENT            -> QUEUED  it goes out again right away
 *   QUEUED/SENT     -> FREE    its deletion is done
 *   FAILED          -> FREE    right away, there's nothing to delete
 *
 * A request is in the queue exactly while it's QUEUED or SENT, and only the
 * head is ever SENT. It stays SENT while it's retransmitted.
 */
typedef struct natpmp_request {
  /// The request behind this one in the queue
  struct natpmp_request *next;
  natpmp_handle_t handle;
  /// Which request this is, and so which member of the union is in use
  enum natpmp_opcode opcode;
  enum {
    NATPMP_REQUEST_FREE,
    /// Waiting to go out
    NATPMP_REQUEST_QUEUED,
    /// On the wire, waiting for the gateway's answer
    NATPMP_REQUEST_SENT,
    NATPMP_REQUEST_ANSWERED,
    NATPMP_REQUEST_FAILED,
    /// A mapping that was reported, until `map.renew_at`
    NATPMP_REQUEST_ACTIVE,
  } state;
  union {
    /// Once ANSWERED
    natpmp_ipv4_address_t external_ip;
    /// `external_port` and `lifetime` are asked for, then granted once
    /// ANSWERED, and renewals ask for them again. Both are 0 once released.
    struct {
      natpmp_port_t internal_port;
      natpmp_port_t external_port;
      natpmp_time_s_t lifetime;
      natpmp_time_ms_t renew_at;
    } map;
  };
} natpmp_request_t;

/**
 * @brief Whether it's a released mapping
 *
 * A mapping asks for its deletion with lifetime 0, which it's never granted.
 */
bool natpmp_request_released(natpmp_request_t const *request);

/**
 * @brief Whether its last event is still to be polled
 */
bool natpmp_request_done(natpmp_request_t const *request);

/**
 * @brief The event for a request that's done, which moves it on
 *
 * A mapping that was granted becomes ACTIVE, anything else FREE.
 */
natpmp_event_t natpmp_request_take_event(natpmp_request_t *request);

bool natpmp_request_matches(natpmp_request_t const *request,
                            natpmp_response_t const *response);

void natpmp_request_apply_answer(natpmp_request_t *request,
                                 natpmp_response_t const *response,
                                 natpmp_time_ms_t now);

/**
 * @brief Give up on it, the gateway won't answer
 *
 * A released mapping is freed, since it just expires on the gateway.
 */
void natpmp_request_fail(natpmp_request_t *request);

/**
 * @brief Make a mapping in use ask for its deletion
 *
 * Ignored if it was already released.
 *
 * @returns Whether it has to join the queue
 */
bool natpmp_request_release(natpmp_request_t *request);
