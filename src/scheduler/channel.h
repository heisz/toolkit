/**
 * Public API for Go-like channels for inter-fiber communication.
 *
 * Copyright (C) 2024-2026 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */

#ifndef GMP_CHANNEL_H
#define GMP_CHANNEL_H 1

#include <stdint.h>

/* Declarations for clarity */
typedef struct GMPS_Channel GMPS_Channel;

/**
 * Create a channel for inter-fiber communication.  Capacity of zero * creates
 * an unbuffered (synchronous) channel, otherwise a buffered channel with the
 * given capacity.  Returns NULL on allocation failure.
 */
GMPS_Channel *GMPS_ChannelCreate(uint32_t capacity);

/**
 * Send a value on the specified channel.  Blocks the calling fiber until a
 * receiver is available (unbuffered) or a buffer slot is available (buffered).
 * Returns TRUE on success, FALSE if the channel is closed.
 */
int GMPS_ChannelSend(GMPS_Channel *ch, void *val);

/**
 * Receive a value from the channel.  Blocks the calling fiber until a sender
 * provides a message (unbuffered) or a buffered value is ready.  On success
 * stores the value in valRef and returns TRUE.  Returns FALSE if the channel
 * is closed and empty (valRef is set to NULL).
 */
int GMPS_ChannelRecv(GMPS_Channel *ch, void **valRef);

/**
 * Close the channel.  Wakes all blocked senders (returning FALSE) and all
 * blocked receivers (returning FALSE with valRef set to NULL).  Subsequent
 * sends return FALSE.  Receives on a closed channel still drain any buffered
 * values before returning FALSE.
 */
void GMPS_ChannelClose(GMPS_Channel *ch);

/**
 * Destroy a channel and free all associated resources.  The channel must not
 * have any fibers blocked on it when destroyed.
 */
void GMPS_ChannelDestroy(GMPS_Channel *ch);

#endif
