/* -------------------------------------------------------------------------
 * queue.c - htun packet queuing functions
 * Copyright (C) 2002 Moshe Jacobson <moshe@runslinux.net>, 
 *                    Ola Nordström <ola@triblock.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 * -------------------------------------------------------------------------
 */
/* $Id: queue.c,v 2.22 2005/10/27 11:11:10 jehsom Exp $ */

#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>

#include "queue.h"
#include "common.h"
#include "log.h"

/* Locking with q_lock() guarantees cancel-safe critical sections */
#define q_lock(q, cnt) do { int _old; \
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED,&_old); \
    pthread_cleanup_push(q_sempost,q); \
    pthread_cleanup_push(q_release,q); \
    pthread_cleanup_push(q_decrement, cnt); \
    pthread_mutex_lock(&((q)->mutex)); \
    (*(cnt))++; \
    pthread_testcancel()

/* q_unlock() is called when intentionally exiting critical section */
#define q_unlock(cond) \
    pthread_testcancel(); \
    pthread_cleanup_pop(1); \
    pthread_cleanup_pop(1); \
    pthread_cleanup_pop(1); \
    if(cond) pthread_cond_broadcast(cond); \
    pthread_setcanceltype(_old,NULL); } while(0)

/* Even in debug mode, the queue debug is a bit much */
#undef dprintf
#define dprintf(...)

/* release the mutex of the passed-in queue */
static inline void q_release(void *q_in) {
    queue_t *q = (queue_t *)q_in;

    dprintf(log, DEBUG, "at head");
    if( pthread_mutex_unlock(&q->mutex) != 0 ) {
        lprintf( log, ERROR, "Unlocking mutex for q at %08X: %s", q,
                strerror(errno) );
    }
}

/* Simply decrement the int pointed to by cntp */
static inline void q_decrement(void *cntp) {
    int *cnt = (int*)cntp;
    dprintf(log, DEBUG, "at head. *cnt = %d", *cnt);
    (*cnt)--;
    dprintf(log, DEBUG, "finished. *cnt = %d", *cnt);
}

/* Tell the shutdown process to try again */
static inline void q_sempost( void *q_in ) {
    queue_t *q = (queue_t *)q_in;

    if( !q->shutdown ) return;
    dprintf(log, DEBUG, "posting semaphore");
    sem_post(&q->cleanup_sem);
    dprintf(log, DEBUG, "done");
}

/* Adds a request to the queue head or tail depending on flags. */
int q_add( queue_t *q, void *data, int flags, size_t size ){
    qnode_t *newnode;
    int rc=0;
    
    if( !q ) {
        lprintf(log, WARN, "passed null queue!");
        return -1;
    }

    /* This is the start of the lock with cleanup */
    q_lock(q, &q->writers);

    /* This is the case where we're full and not waiting. We just return */
    if( !(flags&Q_WAIT) && q->max_nodes && q->nr_nodes >= q->max_nodes ) {
        dprintf( log, DEBUG, "Returning without adding." );
        rc = -1;
        goto cleanup;
    }

    /* If we've reached here, we must be allocating a new node */
    if( (newnode=malloc(sizeof(qnode_t))) == NULL ) {
        lprintf( log, ERROR, "Unable to malloc space for new node!" );
        rc = -1;
        goto cleanup;
    }

    newnode->data=data;
    newnode->size=size;
    newnode->next=NULL;

    /* This is the case where we will be able to add the item */
    if( flags & Q_WAIT ) {
        while( q->max_nodes && (q->nr_nodes >= q->max_nodes) ) {
            /* This is the case where we're full but waiting */
            dprintf( log, DEBUG, "Waiting for q-notfull signal" );
            pthread_cond_wait(&q->writer_cond,&q->mutex);

            /* We are being told nicely to shut down */
            if( q->shutdown ) {
                dprintf(log, DEBUG, "Returning on shutdown");
                free(data);
                free(newnode);
                rc = -1;
                goto cleanup;
            }

            dprintf(log, DEBUG, "got signal! q no longer full!");
        }
    }

    if( flags&Q_PUSH ) {
        /* Add the request to the head of the queue */
        newnode->next = q->head;
        q->head = newnode;
    } else {
        /* Add the request to the tail of the queue */
        *(q->tail) = newnode;
        q->tail = &newnode->next;
    }

    q->nr_nodes++;
    q->totsize += size;
    gettimeofday(&(q->lastadd), NULL);

    dprintf( log, DEBUG, "Returning after adding %d-byte packet.",
            iplen((char*)data) );

cleanup:
    /* This is the end of the lock with cleanup */
    q_unlock(rc==-1?NULL:&q->reader_cond);

    return rc;
}

/* Pop a request from the head of the queue. */
void *q_remove( queue_t *q, int flags, const struct timespec *wait ){
    qnode_t *tmp;
    void *data;

    if( !q ) {
        lprintf(log, WARN, "q is null!");
        return NULL;
    }

    q_lock(q, &q->readers);

    /* This is the case where we have no nodes but are not waiting */
    if( !(flags&Q_WAIT) && !(q->nr_nodes) ) {
        dprintf( log, DEBUG, "Returning with no data" );
        data = NULL;
        goto cleanup;
    }

    /* If we've reached here, we must be removing a node */
    if( flags & Q_WAIT ) {
        while( !(q->nr_nodes) ) {
            int rc;
            dprintf( log, DEBUG, "Waiting on not empty signal." );
            if( wait ) {
                struct timeval tv;
                struct timespec ts;

                gettimeofday(&tv,NULL);
                ts.tv_sec = tv.tv_sec + wait->tv_sec;
                ts.tv_nsec = tv.tv_usec*1000 + wait->tv_nsec;
                rc = pthread_cond_timedwait(&q->reader_cond,&q->mutex,wait);
            } else {
                rc = pthread_cond_wait(&q->reader_cond, &q->mutex);
            }

            if( q->shutdown ) {
                dprintf(log, DEBUG, "returning on shutdown");
                data = NULL;
                goto cleanup;
            }

            if( rc == ETIMEDOUT ) {
                dprintf( log, DEBUG, "Timed out waiting for data." );
                data = NULL;
                goto cleanup;
            } else {
                dprintf( log, DEBUG, "We get signal! Q not empty!" );
            }
        }
    }

    /* We can safely assume there is data on the queue now */
    tmp=q->head;
    data=tmp->data;
    q->totsize -= tmp->size;
    if( (q->head=tmp->next) == NULL ) q->tail = &q->head;
    free(tmp);
    q->nr_nodes--;

    dprintf( log, DEBUG, "Returning with data.");

cleanup:
    q_unlock((data?&q->writer_cond:NULL));
    return data;
}

/* 
 * Waits up to the specified amount of time for data to appear on the queue.
 * Returns 0 on error or when no data has appeared, 
 * Returns nonzero when data has appeared.
 */
int q_timedwait( queue_t *q, struct timespec *ts_in ) {
    int rc;
    struct timeval tv;
    struct timespec ts;

    dprintf(log, DEBUG, "starting");
    if( !q ) {
        lprintf(log, WARN, "passed null queue!");
        return 0;
    }

    q_lock(q, &q->readers);

    gettimeofday(&tv,NULL);
    ts.tv_sec = tv.tv_sec + ts_in->tv_sec;
    ts.tv_nsec = tv.tv_usec*1000 + ts_in->tv_nsec;

    if( q->nr_nodes ) {
        rc = 1;
    } else {
        dprintf(log, DEBUG, "entering pthread_cond_timedwait");
        rc = !pthread_cond_timedwait(&q->reader_cond,&q->mutex,&ts);
        dprintf(log, DEBUG, 
                "pthread_cond_timedwait returned %d", !rc);
        if( q->shutdown ) { 
            dprintf(log, DEBUG, 
                    "got shutdown signal. Posting semaphore.");
            rc = 0;
        }
    }

    q_unlock(NULL);
    dprintf(log, DEBUG, "exiting");
    return rc;
}

/* Allocates and initializes a new queue_t */
queue_t *q_init( void ) {
    queue_t *q = calloc(1, sizeof(queue_t));

    if(!q) {
        lprintf(log, ERROR, "Could not malloc() new queue!");
        goto cleanup_a;
    }
    q->tail = &q->head;

    if( pthread_mutex_init(&q->mutex,NULL) != 0 ) {
        lprintf(log, ERROR, "Could not initialize queue mutex!");
        goto cleanup_b;
    }
    if( pthread_cond_init(&q->reader_cond,NULL) != 0 ) {
        lprintf(log, ERROR, "Could not initlize q reader cond!");
        goto cleanup_c;
    }
    if( pthread_cond_init(&q->writer_cond,NULL) != 0 ) {
        lprintf(log, ERROR, "Could not initlize q writer cond!");
        goto cleanup_d;
    }
    if( sem_init(&q->cleanup_sem, 0, 0) == -1 ) {
        lprintf(log, ERROR, "Could not init q cleanup semaphore!");
        goto cleanup_e;
    }
    return q;

cleanup_e:
    pthread_cond_destroy(&q->writer_cond);
cleanup_d:
    pthread_cond_destroy(&q->reader_cond);
cleanup_c:
    pthread_mutex_destroy(&q->mutex);
cleanup_b:
    free(q);
cleanup_a:
    lprintf( log, ERROR, "Error initializing queue!!" );
    return NULL;
}

/* Destroys a queue and all elements in it. */
void q_destroy( queue_t **qp ) {
    void *data;
    queue_t *q;

    if( !qp || !*qp ) {
        lprintf(log, WARN, "passed null queue!");
        return;
    }

    q = *qp;
    *qp = NULL;
    q->shutdown = 1;

    while( q->writers ) {
        dprintf(log, DEBUG, 
                "signalling one q writer to quit (%d writers)",
                q->writers);
        pthread_cond_signal(&q->writer_cond);
        sem_wait(&q->cleanup_sem);
    }

    while( (data=q_remove(q,0,NULL)) ) free(data);

    while( q->readers ) {
        dprintf(log, DEBUG, 
                "signalling one q reader to quit. (%d readers)",
                q->readers);
        pthread_cond_signal(&q->reader_cond);
        sem_wait(&q->cleanup_sem);
        dprintf(log, DEBUG, "Got cleanup semaphore.");
    }

    pthread_mutex_lock(&q->mutex);
    dprintf(log, DEBUG, "got mutex lock");
    pthread_cond_destroy(&q->writer_cond);
    pthread_cond_destroy(&q->reader_cond);
    sem_destroy(&q->cleanup_sem);
    pthread_mutex_unlock(&q->mutex);

    pthread_mutex_destroy(&q->mutex);
    free(q);
    dprintf(log, DEBUG, "done");
    return;
}

