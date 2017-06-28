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

#include "common.h"
#include "playqueue.h"

typedef struct PQListNode_s {
    struct PQListNode_s *next;
    struct PQListNode_s *prev;
    PlayNode_s node;
} PQListNode_s;

typedef struct PQList_s {
    PQListNode_s *availNodes;   // ->next only LL
    PQListNode_s *queuedNodes;  // double LL
    PQListNode_s *queuedHead;
} PQList_s;

// ----------------------------------------------------------------------------

static inline bool _iterate_list_and_find_node(INOUT PQListNode_s **listNodePtr, long nodeId) {
    while (*listNodePtr) {
        if ((*listNodePtr)->node.nodeId == nodeId) {
            return true;
        }
        *listNodePtr = (*listNodePtr)->next;
    }
    return false;
}

static long playq_enqueue(PlayQueue_s *_this, INOUT PlayNode_s *node) {
    long err = 0;

    do {
        PQList_s *list = (PQList_s *)(_this->_internal);

        // detach a node from the available pool
        PQListNode_s *listNode = list->availNodes;
        if (!listNode) {
            ERRLOG("Cannot enqueue: no slots available");
            err = -1;
            break;
        }
        list->availNodes = listNode->next;

        // exchange data
        listNode->node.numBytes = node->numBytes;
        listNode->node.bytes = node->bytes;
        node->nodeId = listNode->node.nodeId;

        // enqueue node
        listNode->next = list->queuedNodes;
        listNode->prev = NULL;
        if (list->queuedNodes) {
            list->queuedNodes->prev = listNode;
        }
        list->queuedNodes = listNode;

        // reset head
        if (listNode->next == NULL) {
            list->queuedHead = listNode;
        }
    } while (0);

    return err;
}

static long playq_dequeue(PlayQueue_s *_this, OUTPARM PlayNode_s *node) {
    PQList_s *list = (PQList_s *)(_this->_internal);
    if (node) {
        *node = (PlayNode_s){ 0 };
    }

    bool found = (list->queuedHead != NULL);
    if (found) {
        PQListNode_s *listNode = list->queuedHead;

        // detach from queued
        list->queuedHead = list->queuedHead->prev;
        if (list->queuedHead) {
            list->queuedHead->next = NULL;
        } else {
            list->queuedNodes = NULL;
        }

        // copy data
        if (node) {
            *node = listNode->node;
        }

        // attach to available pool
        listNode->prev = NULL;
        listNode->next = list->availNodes;
        list->availNodes = listNode;

        /*listNode->node.nodeId = 0;IMMUTABLE*/
        listNode->node.numBytes = 0;
        listNode->node.bytes = NULL;
    } else {
        assert(list->queuedNodes == NULL);
    }

    return found ? 0 : -1;
}

static long playq_remove(PlayQueue_s *_this, INOUT PlayNode_s *node) {

    PQList_s *list = (PQList_s *)(_this->_internal);
    PQListNode_s *listNode = list->queuedNodes;

    bool found = _iterate_list_and_find_node(&listNode, node->nodeId);
    if (found) {

        // reset head
        if (listNode->next == NULL) {
            list->queuedHead = listNode->prev;
        }

        // detach listNode from queued list ...
        if (listNode->prev) {
            listNode->prev->next = listNode->next;
            if (listNode->next) {
                listNode->next->prev = listNode->prev;
            }
        } else {
            list->queuedNodes = listNode->next;
            if (listNode->next) {
                listNode->next->prev = NULL;
            }
        }

        *node = listNode->node; // copy data

        listNode->next = list->availNodes;
        listNode->prev = NULL;
        list->availNodes = listNode;

        /*listNode->node.nodeId = 0;IMMUTABLE*/
        listNode->node.numBytes = 0;
        listNode->node.bytes = NULL;
    }

    return found ? 0 : -1;
}

static void playq_drain(PlayQueue_s *_this) {
    long err = 0;
    do {
        err = _this->Dequeue(_this, NULL);
    } while (err == 0);
}

static long playq_getHead(PlayQueue_s *_this, OUTPARM PlayNode_s *node) {
    long err = 0;

    PQList_s *list = (PQList_s *)(_this->_internal);
    if (list->queuedHead) {
        *node = list->queuedHead->node;
    } else {
        *node = (PlayNode_s){ 0 };
        err = -1;
    }

    return err;
}

static long playq_get(PlayQueue_s *_this, OUTPARM PlayNode_s *node) {
    long err = 0;

    PQList_s *list = (PQList_s *)(_this->_internal);
    PQListNode_s *listNode = list->queuedNodes;

    _iterate_list_and_find_node(&listNode, node->nodeId);
    if (listNode) {
        *node = listNode->node;
    } else {
        *node = (PlayNode_s){ 0 };
        err = -1;
    }

    return err;
}

static bool playq_canEnqueue(PlayQueue_s *_this) {
    PQList_s *list = (PQList_s *)(_this->_internal);
    return (list->availNodes != NULL);
}

// ----------------------------------------------------------------------------

void playq_destroyPlayQueue(INOUT PlayQueue_s **queue) {

    if (!(*queue)) {
        return;
    }

    if ((*queue)->Drain) {
        (*queue)->Drain(*queue);
    }

    PQList_s *list = (PQList_s *)((*queue)->_internal);
    if (list) {
        PQListNode_s *node = list->availNodes;
        while (node) {
            PQListNode_s *p = node;
            node = node->next;
            FREE(p);
        }
    }

    FREE(list);
    FREE(*queue);
}

PlayQueue_s *playq_createPlayQueue(const long *nodeIdPtr, unsigned long numBuffers) {
    PlayQueue_s *playq = NULL;

    assert(numBuffers <= MAX_PLAYQ_BUFFERS);

    do {
        playq = CALLOC(1, sizeof(PlayQueue_s));
        if (!playq) {
            ERRLOG("no memory");
            break;
        }

        PQList_s *list = CALLOC(1, sizeof(PQList_s));
        playq->_internal = list;
        if (!list) {
            ERRLOG("no memory");
            break;
        }

        bool allocSuccess = true;
        for (unsigned long i=0; i<numBuffers; i++) {
            PQListNode_s *listNode = CALLOC(1, sizeof(PQListNode_s));
            LOG("CREATING PlayNode_s node ID: %ld", nodeIdPtr[i]);
            listNode->node.nodeId = nodeIdPtr[i];
            if (!listNode) {
                ERRLOG("no memory");
                allocSuccess = false;
                break;
            }
            listNode->next = list->availNodes;
            list->availNodes = listNode;
        }
        if (!allocSuccess) {
            break;
        }

        playq->Enqueue = &playq_enqueue;
        playq->Dequeue = &playq_dequeue;
        playq->Remove  = &playq_remove;
        playq->Drain   = &playq_drain;
        playq->GetHead = &playq_getHead;
        playq->Get     = &playq_get;
        playq->CanEnqueue = &playq_canEnqueue;

        return playq;
    } while (0);

    if (playq) {
        playq_destroyPlayQueue(&playq);
    }

    return NULL;
}

#define SELF_TEST 0
#if SELF_TEST
bool do_logging = true;
FILE *error_log = NULL;

static void _test_creation(void) {
    LOG("begin test");
    const unsigned long maxNodes = 8;
    long nodeIds[maxNodes];
    for (unsigned long i=0; i<maxNodes; i++) {
        nodeIds[i] = i+42;
    }
    PlayQueue_s *pq = playq_createPlayQueue(nodeIds, maxNodes);
    assert(pq != NULL);
    playq_destroyPlayQueue(&pq);
    assert(pq == NULL);
}

static void _test_internal_list_creation_integrity(void) {
    LOG("begin test");
    const unsigned long maxNodes = 8;
    long nodeIds[maxNodes];
    for (unsigned long i=0; i<maxNodes; i++) {
        nodeIds[i] = i+42;
    }
    PlayQueue_s *pq = playq_createPlayQueue(nodeIds, maxNodes);
    assert(pq != NULL);
    assert(pq->_internal != NULL);

    PQList_s *list = (PQList_s *)(pq->_internal);

    assert(list->availNodes);
    assert(list->queuedNodes == NULL);
    assert(list->queuedHead == NULL);

    PQListNode_s *listNode = list->availNodes;
    unsigned int count = 0;
    while (listNode) {
        listNode = listNode->next;
        ++count;
    }
    assert (count == maxNodes);

    playq_destroyPlayQueue(&pq);
    assert(pq == NULL);
}

static void _test_enqueue_dequeue(void) {
    LOG("begin test");
    const unsigned long maxNodes = 4;
    long nodeIds[maxNodes];
    for (unsigned long i=0; i<maxNodes; i++) {
        nodeIds[i] = i+42;
    }
    PlayQueue_s *pq = playq_createPlayQueue(nodeIds, maxNodes);
    assert(pq != NULL);

    uint8_t *bytesPtr = (uint8_t *)&bytesPtr;
    unsigned long numBytes = 42;

    PQList_s *list = (PQList_s *)(pq->_internal);

    assert(list->availNodes);
    assert(list->queuedNodes == NULL);
    assert(list->queuedHead == NULL);

    long err = 0;

    PlayNode_s node0 = {
        .numBytes = numBytes++,
        .bytes = bytesPtr++,
    };
    err = pq->Enqueue(pq, &node0);
    assert(err == 0);

    assert(list->queuedNodes);
    assert(list->queuedHead);

    PlayNode_s node1 = {
        .numBytes = numBytes++,
        .bytes = bytesPtr++,
    };
    err = pq->Enqueue(pq, &node1);
    assert(err == 0);

    PlayNode_s node2 = {
        .numBytes = numBytes++,
        .bytes = bytesPtr++,
    };
    err = pq->Enqueue(pq, &node2);
    assert(err == 0);

    PlayNode_s node3 = {
        .numBytes = numBytes++,
        .bytes = bytesPtr++,
    };
    err = pq->Enqueue(pq, &node3);
    assert(err == 0);

    assert(list->availNodes == NULL);

    // test over-enqueue

    PlayNode_s node4 = {
        .numBytes = numBytes++,
        .bytes = bytesPtr++,
    };
    err = pq->Enqueue(pq, &node4);
    assert(err != 0 && "this should fail");

    // check internal list integrity forward

    PQListNode_s *listNode = list->queuedNodes;
    assert(listNode->node.nodeId   == node3.nodeId);
    assert(listNode->node.numBytes == node3.numBytes);
    assert(listNode->node.bytes    == node3.bytes);

    listNode = listNode->next;
    assert(listNode->node.nodeId   == node2.nodeId);
    assert(listNode->node.numBytes == node2.numBytes);
    assert(listNode->node.bytes    == node2.bytes);

    listNode = listNode->next;
    assert(listNode->node.nodeId   == node1.nodeId);
    assert(listNode->node.numBytes == node1.numBytes);
    assert(listNode->node.bytes    == node1.bytes);

    listNode = listNode->next;
    assert(listNode == list->queuedHead);
    assert(listNode->next == NULL);

    assert(listNode->node.nodeId   == node0.nodeId);
    assert(listNode->node.numBytes == node0.numBytes);
    assert(listNode->node.bytes    == node0.bytes);

    // check internal list integrity backward

    listNode = listNode->prev;
    PQListNode_s *prevHead1 = listNode;
    assert(listNode->node.nodeId   == node1.nodeId);
    assert(listNode->node.numBytes == node1.numBytes);
    assert(listNode->node.bytes    == node1.bytes);

    listNode = listNode->prev;
    PQListNode_s *prevHead2 = listNode;
    assert(listNode->node.nodeId   == node2.nodeId);
    assert(listNode->node.numBytes == node2.numBytes);
    assert(listNode->node.bytes    == node2.bytes);

    listNode = listNode->prev;
    PQListNode_s *prevHead3 = listNode;
    assert(listNode->node.nodeId   == node3.nodeId);
    assert(listNode->node.numBytes == node3.numBytes);
    assert(listNode->node.bytes    == node3.bytes);

    assert(listNode == list->queuedNodes);
    assert(prevHead3 == list->queuedNodes);
    assert(listNode->prev == NULL);

    // test one dequeue

    PlayNode_s dqNode = { 0 };
    err = pq->Dequeue(pq, &dqNode);
    assert(err == 0);
    assert(dqNode.nodeId   == node0.nodeId);
    assert(dqNode.numBytes == node0.numBytes);
    assert(dqNode.bytes    == node0.bytes);
    assert(list->queuedHead == prevHead1);
    assert(list->availNodes != NULL);
    assert(list->availNodes->next == NULL);

    // test successful re-enqueue of node4

    err = pq->Enqueue(pq, &node4);
    assert(err == 0);
    assert(list->availNodes == NULL);
    assert(list->queuedHead == prevHead1);
    assert(list->queuedNodes != prevHead3);

    // test dequeue all the things ...

    err = pq->Dequeue(pq, &dqNode);
    assert(err == 0);
    assert(dqNode.nodeId   == node1.nodeId);
    assert(dqNode.numBytes == node1.numBytes);
    assert(dqNode.bytes    == node1.bytes);
    assert(list->queuedHead == prevHead2);
    assert(list->availNodes != NULL);
    assert(list->availNodes->next == NULL);

    err = pq->Dequeue(pq, &dqNode);
    assert(err == 0);
    assert(dqNode.nodeId   == node2.nodeId);
    assert(dqNode.numBytes == node2.numBytes);
    assert(dqNode.bytes    == node2.bytes);
    assert(list->queuedHead == prevHead3);
    assert(list->availNodes != NULL);
    assert(list->availNodes->next != NULL);
    assert(list->availNodes->next->next == NULL);

    err = pq->Dequeue(pq, &dqNode);
    assert(err == 0);
    assert(dqNode.nodeId   == node3.nodeId);
    assert(dqNode.numBytes == node3.numBytes);
    assert(dqNode.bytes    == node3.bytes);
    assert(list->availNodes != NULL);
    assert(list->availNodes->next != NULL);
    assert(list->availNodes->next->next != NULL);
    assert(list->availNodes->next->next->next == NULL);

    err = pq->Dequeue(pq, &dqNode);
    assert(err == 0);
    assert(dqNode.nodeId   == node4.nodeId);
    assert(dqNode.numBytes == node4.numBytes);
    assert(dqNode.bytes    == node4.bytes);
    assert(list->queuedHead == NULL);
    assert(list->queuedNodes == NULL);
    assert(list->availNodes != NULL);
    assert(list->availNodes->next != NULL);
    assert(list->availNodes->next->next != NULL);
    assert(list->availNodes->next->next->next != NULL);
    assert(list->availNodes->next->next->next->next == NULL);

    // test over-dequeue
    err = pq->Dequeue(pq, &dqNode);
    assert (err != 0 && "cannot dequeue with nothing there");

    assert(list->queuedNodes == NULL);
    assert(list->queuedHead == NULL);

    // cleanup

    playq_destroyPlayQueue(&pq);
    assert(pq == NULL);
}

static void _test_remove_head_of_queue(void) {
    LOG("begin test");
    const unsigned long maxNodes = 4;
    long nodeIds[maxNodes];
    for (unsigned long i=0; i<maxNodes; i++) {
        nodeIds[i] = i+42;
    }
    PlayQueue_s *pq = playq_createPlayQueue(nodeIds, maxNodes);
    assert(pq != NULL);

    uint8_t *bytesPtr = (uint8_t *)&bytesPtr;
    unsigned long numBytes = 42;
    long err = 0;

    PQList_s *list = (PQList_s *)(pq->_internal);
    assert(list->availNodes  != NULL);
    assert(list->queuedNodes == NULL);
    assert(list->queuedHead  == NULL);

    PlayNode_s node0 = {
        .numBytes = numBytes++,
        .bytes = bytesPtr++,
    };
    err = pq->Enqueue(pq, &node0);
    assert(err == 0);

    assert(list->queuedNodes != NULL);
    assert(list->queuedHead  != NULL);
    PQListNode_s *listHead0 = list->queuedHead;

    PlayNode_s node1 = {
        .numBytes = numBytes++,
        .bytes = bytesPtr++,
    };
    err = pq->Enqueue(pq, &node1);
    assert(err == 0);

    PlayNode_s node2 = {
        .numBytes = numBytes++,
        .bytes = bytesPtr++,
    };
    err = pq->Enqueue(pq, &node2);
    assert(err == 0);

    PlayNode_s node3 = {
        .numBytes = numBytes++,
        .bytes = bytesPtr++,
    };
    err = pq->Enqueue(pq, &node3);
    assert(err == 0);

    assert(list->queuedHead == listHead0);

    // dequeue head node using remove

    assert(list->availNodes == NULL);

    PlayNode_s dqNode = {
        .nodeId = node0.nodeId,
    };
    err = pq->Remove(pq, &dqNode);
    assert(dqNode.nodeId   == node0.nodeId);
    assert(dqNode.numBytes == node0.numBytes);
    assert(dqNode.bytes    == node0.bytes);

    // check integrity of inner list

    assert(list->availNodes  != NULL);
    assert(list->availNodes  == listHead0);
    assert(list->availNodes->next == NULL);
    assert(list->availNodes->prev == NULL);
    assert(list->queuedNodes != NULL);
    assert(list->queuedHead  != NULL);

    PQListNode_s *listNode = list->queuedNodes;
    assert(listNode->prev == NULL);
    assert(listNode->next->prev == listNode);

    listNode = listNode->next;
    assert(listNode->next->prev == listNode);

    listNode = listNode->next;
    assert(listNode->next == NULL);
    assert(list->queuedHead == listNode);

    // cleanup

    playq_destroyPlayQueue(&pq);
    assert(pq == NULL);
}

static void _test_remove_tail_of_queue(void) {
    LOG("begin test");
    const unsigned long maxNodes = 4;
    long nodeIds[maxNodes];
    for (unsigned long i=0; i<maxNodes; i++) {
        nodeIds[i] = i+42;
    }
    PlayQueue_s *pq = playq_createPlayQueue(nodeIds, maxNodes);
    assert(pq != NULL);

    uint8_t *bytesPtr = (uint8_t *)&bytesPtr;
    unsigned long numBytes = 42;
    long err = 0;

    PQList_s *list = (PQList_s *)(pq->_internal);
    assert(list->availNodes  != NULL);
    assert(list->queuedNodes == NULL);
    assert(list->queuedHead  == NULL);

    PlayNode_s node0 = {
        .numBytes = numBytes++,
        .bytes = bytesPtr++,
    };
    err = pq->Enqueue(pq, &node0);
    assert(err == 0);

    assert(list->queuedNodes != NULL);
    assert(list->queuedHead  != NULL);
    PQListNode_s *listHead0 = list->queuedHead;

    PlayNode_s node1 = {
        .numBytes = numBytes++,
        .bytes = bytesPtr++,
    };
    err = pq->Enqueue(pq, &node1);
    assert(err == 0);

    PlayNode_s node2 = {
        .numBytes = numBytes++,
        .bytes = bytesPtr++,
    };
    err = pq->Enqueue(pq, &node2);
    assert(err == 0);

    PlayNode_s node3 = {
        .numBytes = numBytes++,
        .bytes = bytesPtr++,
    };
    err = pq->Enqueue(pq, &node3);
    assert(err == 0);
    PQListNode_s *listHead3 = list->queuedNodes;

    assert(list->queuedHead == listHead0);

    // dequeue head node using remove

    assert(list->availNodes == NULL);

    PlayNode_s dqNode = {
        .nodeId = node3.nodeId,
    };
    err = pq->Remove(pq, &dqNode);
    assert(dqNode.nodeId   == node3.nodeId);
    assert(dqNode.numBytes == node3.numBytes);
    assert(dqNode.bytes    == node3.bytes);

    // check integrity of inner list

    assert(list->availNodes  != NULL);
    assert(list->availNodes  == listHead3);
    assert(list->availNodes->prev == NULL);
    assert(list->availNodes->next == NULL);
    assert(list->queuedNodes != NULL);
    assert(list->queuedHead  != NULL);
    assert(list->queuedHead  == listHead0);

    PQListNode_s *listNode = list->queuedNodes;
    assert(listNode->prev == NULL);
    assert(listNode->next->prev == listNode);

    listNode = listNode->next;
    assert(listNode->next->prev == listNode);

    listNode = listNode->next;
    assert(listNode->next == NULL);
    assert(list->queuedHead == listNode);

    // cleanup

    playq_destroyPlayQueue(&pq);
    assert(pq == NULL);
}

static void _test_remove_middle_of_queue(void) {
    LOG("begin test");
    const unsigned long maxNodes = 4;
    long nodeIds[maxNodes];
    for (unsigned long i=0; i<maxNodes; i++) {
        nodeIds[i] = i+42;
    }
    PlayQueue_s *pq = playq_createPlayQueue(nodeIds, maxNodes);
    assert(pq != NULL);

    uint8_t *bytesPtr = (uint8_t *)&bytesPtr;
    unsigned long numBytes = 42;
    long err = 0;

    PQList_s *list = (PQList_s *)(pq->_internal);
    assert(list->availNodes  != NULL);
    assert(list->queuedNodes == NULL);
    assert(list->queuedHead  == NULL);

    PlayNode_s node0 = {
        .numBytes = numBytes++,
        .bytes = bytesPtr++,
    };
    err = pq->Enqueue(pq, &node0);
    assert(err == 0);

    assert(list->queuedNodes != NULL);
    assert(list->queuedHead  != NULL);
    PQListNode_s *listHead0 = list->queuedHead;

    PlayNode_s node1 = {
        .numBytes = numBytes++,
        .bytes = bytesPtr++,
    };
    err = pq->Enqueue(pq, &node1);
    assert(err == 0);

    PlayNode_s node2 = {
        .numBytes = numBytes++,
        .bytes = bytesPtr++,
    };
    err = pq->Enqueue(pq, &node2);
    assert(err == 0);
    PQListNode_s *listHead2 = list->queuedNodes;

    PlayNode_s node3 = {
        .numBytes = numBytes++,
        .bytes = bytesPtr++,
    };
    err = pq->Enqueue(pq, &node3);
    assert(err == 0);

    assert(list->queuedHead == listHead0);

    // dequeue head node using remove

    assert(list->availNodes == NULL);

    PlayNode_s dqNode = {
        .nodeId = node2.nodeId,
    };
    err = pq->Remove(pq, &dqNode);
    assert(dqNode.nodeId   == node2.nodeId);
    assert(dqNode.numBytes == node2.numBytes);
    assert(dqNode.bytes    == node2.bytes);

    // check integrity of inner list

    assert(list->availNodes  != NULL);
    assert(list->availNodes  == listHead2);
    assert(list->availNodes->prev == NULL);
    assert(list->availNodes->next == NULL);
    assert(list->queuedNodes != NULL);
    assert(list->queuedHead  != NULL);
    assert(list->queuedHead  == listHead0);

    PQListNode_s *listNode = list->queuedNodes;
    assert(listNode->prev == NULL);
    assert(listNode->next->prev == listNode);

    listNode = listNode->next;
    assert(listNode->next->prev == listNode);

    listNode = listNode->next;
    assert(listNode->next == NULL);
    assert(list->queuedHead == listNode);

    // cleanup

    playq_destroyPlayQueue(&pq);
    assert(pq == NULL);
}

int main(int argc, char **argv) {
#warning use Valgrind to check proper memory management
    error_log = stdout;
    LOG("beginning tests");
    _test_creation();
    _test_internal_list_creation_integrity();
    _test_enqueue_dequeue();
    _test_remove_head_of_queue();
    _test_remove_tail_of_queue();
    _test_remove_middle_of_queue();
    LOG("all tests successful");
    return 0;
}
#endif

