#pragma once

#include "request.h"
#include "retransmit.h"
#include <byo/natpmp/natpmp.h>

/**
 * @brief The requests waiting to go to the gateway, sent one at a time
 *
 * Starts zeroed, empty.
 */
typedef struct natpmp_queue {
  /// The request going out, with the QUEUED ones behind it through `next`, in
  /// the order they were made or came up for renewal. Only it is ever SENT.
  natpmp_request_t *head;
  /// For `head`
  natpmp_retransmit_t retransmit;
  uint8_t data[NATPMP_MAP_REQUEST_MAX_SIZE];
} natpmp_queue_t;

typedef struct natpmp_queue_status {
  natpmp_retransmit_status_t status;
  /// How much of `data` to send, when DUE
  size_t size;
} natpmp_queue_status_t;

/**
 * @brief Put the request at the back of the queue
 */
void natpmp_queue_push(natpmp_queue_t *queue, natpmp_request_t *request);

/**
 * @brief Fail everything waiting, the head included
 *
 * For when the gateway is gone.
 */
void natpmp_queue_fail_all(natpmp_queue_t *queue);

/**
 * @brief Write the head into `data` when it's due
 *
 * Only call it while the queue isn't empty.
 */
natpmp_queue_status_t natpmp_queue_poll(natpmp_queue_t *queue,
                                        natpmp_time_ms_t now);

/**
 * @brief Answer the request on the wire, if the response is for it
 */
void natpmp_queue_on_response(natpmp_queue_t *queue,
                              natpmp_time_ms_t now,
                              natpmp_response_t const *response);
