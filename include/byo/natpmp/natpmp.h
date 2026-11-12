#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Milliseconds from a monotonic clock like CLOCK_MONOTONIC
 *
 * Not wall-clock time. Any origin works, only differences matter.
 */
typedef uint64_t natpmp_time_ms_t;

typedef struct natpmp_optional_time_ms {
  bool has_value;
  natpmp_time_ms_t value;
} natpmp_optional_time_ms_t;

typedef uint32_t natpmp_time_s_t;

typedef struct natpmp_ipv4_address {
  uint8_t octets[4];
} natpmp_ipv4_address_t;

typedef uint16_t natpmp_port_t;

typedef struct natpmp_allocator {
  void *(*alloc)(void *ctx, size_t size);
  void (*free)(void *ctx, void *ptr);
  void *ctx;
} natpmp_allocator_t;

typedef struct natpmp_config {
  natpmp_allocator_t allocator;
  /// How many handles the session can have in use at once, 0 for 8. A released
  /// mapping still takes up one until its deletion is done.
  uint16_t capacity;
} natpmp_config_t;

typedef struct natpmp_session natpmp_session_t;

/**
 * @brief Create a session
 *
 * If `config.allocator` is left zeroed, malloc and free are used.
 *
 * @param[in] config The session's configuration, copied into the session
 *
 * @returns The session, or nullptr if it couldn't be allocated
 */
natpmp_session_t *natpmp_session_begin(natpmp_config_t config);

/**
 * @brief Free the session
 *
 * Doesn't delete the mappings, which stay on the gateway until their lifetime
 * runs out. For a clean shutdown, release all mappings and keep polling until
 * NATPMP_EVENT_TIMEOUT has no deadline.
 */
void natpmp_session_end(natpmp_session_t *session);

typedef uint16_t natpmp_handle_t;

typedef struct natpmp_handle_or_error {
  bool has_value;
  union {
    natpmp_handle_t value;
    /// An errno value, e.g. ENOBUFS
    int error;
  };
} natpmp_handle_or_error_t;

/**
 * @brief Ask the gateway for its external address
 *
 * Answered by NATPMP_EVENT_ADDRESS or NATPMP_EVENT_FAILED. Requests go to the
 * gateway one at a time, in the order they were made.
 *
 * The application should poll the session afterwards, see natpmp_session_poll.
 *
 * @param[in] session The session that owns this request
 *
 * @returns The handle, or an error (ENOBUFS if the session is at capacity)
 */
natpmp_handle_or_error_t
natpmp_session_address_request(natpmp_session_t *session);

typedef enum natpmp_protocol {
  NATPMP_PROTOCOL_TCP = 6,
  NATPMP_PROTOCOL_UDP = 17,
} natpmp_protocol_t;

/**
 * @brief Ask the gateway to map an external port to an internal one
 *
 * Answered by NATPMP_EVENT_MAPPED or NATPMP_EVENT_FAILED. Requests go to the
 * gateway one at a time, in the order they were made. The session renews the
 * mapping halfway through every lifetime the gateway grants, with another
 * NATPMP_EVENT_MAPPED each time, until it fails or is released (see
 * natpmp_session_release).
 *
 * The application should poll the session afterwards, see natpmp_session_poll.
 *
 * @param[in] session       The session that owns this request
 * @param[in] lifetime      The lifetime to ask for (not 0)
 * @param[in] protocol      NATPMP_PROTOCOL_UDP or NATPMP_PROTOCOL_TCP
 * @param[in] port          The internal port to map (not 0)
 * @param[in] external_port The external port to ask for, which the gateway may
 *                          not grant. 0 means any port.
 *
 * @returns The handle, or an error (ENOBUFS if the session is at capacity)
 */
natpmp_handle_or_error_t
natpmp_session_map_request(natpmp_session_t *session,
                           natpmp_time_s_t lifetime,
                           natpmp_protocol_t protocol,
                           natpmp_port_t port,
                           natpmp_port_t external_port);

/**
 * @brief Release a mapping the application no longer needs
 *
 * The gateway gets asked to delete it, so poll afterwards. The handle is
 * invalid after this and no more events will refer to it.
 *
 * @param[in] session The session that owns the mapping
 * @param[in] handle  The mapping to release. Ignored if it was already
 *                    released or failed, or isn't a mapping.
 */
void natpmp_session_release(natpmp_session_t *session, natpmp_handle_t handle);

/**
 * @brief Something the application has to handle, see natpmp_session_poll
 *
 * Later versions may add event types, so ignore any you don't know, e.g. with
 * a default case.
 */
typedef struct natpmp_event {
  enum {
    /// Send the request described by `send` to the gateway
    NATPMP_EVENT_SEND,
    /// The gateway's external address is `address.external_ip`. This is the
    /// last event for the handle.
    NATPMP_EVENT_ADDRESS,
    /// Each time the gateway grants the mapping: `mapped.external_port` to the
    /// internal port for `mapped.lifetime` seconds. Both can differ from what
    /// was asked for.
    NATPMP_EVENT_MAPPED,
    /// The gateway refused the request, didn't answer, or couldn't be reached.
    /// This is the last event for the handle.
    NATPMP_EVENT_FAILED,
    /// Nothing more to do before `timeout.deadline`, see natpmp_session_poll
    NATPMP_EVENT_TIMEOUT,
  } type;
  union {
    struct {
      size_t size;
      /// Points into the session and is only valid until the next poll. Copy
      /// it if you want to send it later. If sending fails, it's fine to just
      /// drop it, the retransmission is already scheduled.
      uint8_t const *data;
    } send;
    struct {
      natpmp_handle_t handle;
      natpmp_ipv4_address_t external_ip;
    } address;
    struct {
      natpmp_handle_t handle;
      natpmp_port_t external_port;
      natpmp_time_s_t lifetime;
    } mapped;
    struct {
      natpmp_handle_t handle;
    } failed;
    struct {
      /// Always later than `now`. Empty if nothing is scheduled, in which case
      /// just wait for the next datagram.
      natpmp_optional_time_ms_t deadline;
    } timeout;
  };
} natpmp_event_t;

/**
 * @brief Advance the session and get the next event
 *
 * Call this until it returns NATPMP_EVENT_TIMEOUT whenever something happens:
 * a datagram arrived, the deadline passed or a request was made. Then wait
 * until `timeout.deadline` or the next datagram.
 *
 * Every call invalidates the previous event's data (see natpmp_event_t::send).
 *
 * @param[in] session The session to advance
 * @param[in] now     The current time
 *
 * @returns The next event to handle
 */
natpmp_event_t natpmp_session_poll(natpmp_session_t *session,
                                   natpmp_time_ms_t now);

/**
 * @brief Hand a datagram received from the gateway to the session
 *
 * The application should poll the session afterwards, see natpmp_session_poll.
 *
 * @param[in] session The session to update
 * @param[in] now     The current time
 * @param[in] size    The size of `data`
 * @param[in] data    The datagram's payload
 */
void natpmp_session_on_datagram(natpmp_session_t *session,
                                natpmp_time_ms_t now,
                                size_t size,
                                uint8_t const data[]);

/**
 * @brief Tell the session its requests can't reach the gateway
 *
 * For an ICMP Port Unreachable from the gateway, e.g. ECONNREFUSED from a
 * connected socket. The gateway doesn't speak NAT-PMP then, so every queued
 * request fails right away instead of after all the retransmissions.
 *
 * The application should poll the session afterwards, see natpmp_session_poll.
 *
 * @param[in] session The session to update
 */
void natpmp_session_on_host_unreachable(natpmp_session_t *session);
