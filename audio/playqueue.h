/*
 * Apple // emulator for *ix
 *
 * This software package is subject to the GNU General Public License
 * version 3 or later (your choice) as published by the Free Software
 * Foundation.
 *
 * Copyright 2013-2015 Aaron Culliney
 *
 */

/*
 * A simple audio buffer play queue.
 *
 * WARNING : non-thread-safe API ... locking is callee's responsibility (if needed)
 *
 */

#ifndef _PLAYQUEUE_H_
#define _PLAYQUEUE_H_

#define MAX_PLAYQ_BUFFERS 16
#define INVALID_NODE_ID INT_MIN

typedef struct PlayNode_s {
    long nodeId;
    unsigned long numBytes;
    uint8_t *bytes;
} PlayNode_s;

typedef struct PlayQueue_s {
    PRIVATE void *_internal;

    // enqueues a node (IN : numBytes, bytes OUT : nodeId)
    long (*Enqueue)(struct PlayQueue_s *_this, INOUT PlayNode_s *node);

    // dequeues the head of the queue (OUT : full PlayNode_s data if param is non-null)
    long (*Dequeue)(struct PlayQueue_s *_this, OUTPARM PlayNode_s *node);

    // finds and removes a specific node (IN : nodeId OUT : full PlayNode_s data)
    long (*Remove)(struct PlayQueue_s *_this, INOUT PlayNode_s *node);

    // removes all nodes from the queue
    void (*Drain)(struct PlayQueue_s *_this);

    // gets the head node (OUT : full PlayNode_s data)
    long (*GetHead)(struct PlayQueue_s *_this, OUTPARM PlayNode_s *node);

    // gets a reference to a specific node (IN : nodeId OUT : full PlayNode_s data)
    long (*Get)(struct PlayQueue_s *_this, INOUT PlayNode_s *node);

    // true if we can enqueue moar data
    bool (*CanEnqueue)(struct PlayQueue_s *_this);
} PlayQueue_s;

// create a play queue object
PlayQueue_s *playq_createPlayQueue(const long *nodeIdPtr, unsigned long numBuffers);

// destroy a play queue object
void playq_destroyPlayQueue(INOUT PlayQueue_s **queue);

#endif /* whole file */

