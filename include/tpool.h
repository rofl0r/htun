/* -------------------------------------------------------------------------
 * tpool.h - htun thread pool defs
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
/* $Id: tpool.h,v 2.1 2002/03/25 17:15:33 jehsom Exp $ */

#ifndef _TPOOL_H_
#define _TPOOL_H_

#include  <stdio.h>
#include  <pthread.h>

/*
 * a generic thread pool creation routines
 */

typedef struct tpool_work{
  void (*handler_routine)();
  void *arg;
  struct tpool_work *next;
} tpool_work_t;

typedef struct tpool{
  int num_threads;
  int max_queue_size;

  int do_not_block_when_full;
  pthread_t *threads;
  int cur_queue_size;
  tpool_work_t *queue_head;
  tpool_work_t *queue_tail;
  pthread_mutex_t queue_lock;
  pthread_cond_t queue_not_full;
  pthread_cond_t queue_not_empty;
  pthread_cond_t queue_empty;
  int queue_closed;
  int shutdown;
} tpool_t;

/*
 * returns a newly chreated thread pool
 */
extern tpool_t *tpool_init(int num_worker_threads,
    int max_queue_size, int do_not_block_when_full);

/*
 * returns -1 if work queue is busy
 * otherwise places it on queue for processing, returning 0
 *
 * the routine is a func. ptr to the routine which will handle the
 * work, arg is the arguments to that same routine
 */
extern int tpool_add_work(tpool_t *pool, void  (*routine)(), void *arg);

/*
 * cleanup and close,
 * if finish is set the any working threads will be allowd to finish
 */
extern int tpool_destroy(tpool_t *pool, int finish);

/* private */
/*extern void tpool_thread(tpool_t *pool); */


#endif /* _TPOOL_H_ */
