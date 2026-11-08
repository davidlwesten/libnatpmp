#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * @brief A point in time in milliseconds
 *
 * Has to come from a monotonic clock like CLOCK_MONOTONIC, not wall-clock time.
 * Any origin works, only differences matter.
 */
typedef uint64_t natpmp_time_ms_t;

typedef struct natpmp_optional_time_ms {
  bool has_value;
  natpmp_time_ms_t value;
} natpmp_optional_time_ms_t;

typedef struct natpmp_ipv4_address {
  uint8_t octets[4];
} natpmp_ipv4_address_t;

typedef struct natpmp_allocator {
  void *(*alloc)(void *ctx, size_t size);
  void (*free)(void *ctx, void *ptr);
  void *ctx;
} natpmp_allocator_t;

typedef struct natpmp_config {
  natpmp_allocator_t allocator;
  /// How many requests can wait to go out, 0 for 8
  uint16_t queue_size;
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
 */
void natpmp_session_end(natpmp_session_t *session);

/**
 * @brief Identifies a request in the events that answer it
 */
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
 * @returns The handle, or an error (ENOBUFS if the queue is full)
 */
natpmp_handle_or_error_t
natpmp_session_address_request(natpmp_session_t *session);

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
    /// The gateway refused the request or didn't answer. This is the last
    /// event for the handle.
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
 * @param[in] size    The size of `data`
 * @param[in] data    The datagram's payload
 */
void natpmp_session_on_datagram(natpmp_session_t *session,
                                size_t size,
                                uint8_t const data[]);
