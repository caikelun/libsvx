/*
 * This source code has been dedicated to the public domain by the author.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

/*!
 * \file   svx_threadpool.h
 * \brief  
 *
 * \author Alan Choi
 * \date   2015-11-19
 */

#ifndef SVX_THREADPOOL_H
#define SVX_THREADPOOL_H 1

#include <sys/types.h>

/*!
 * \defgroup Threadpool Threadpool
 * \ingroup  Base
 *
 * \brief    Provides a pool of threads that can be used to execute tasks.
 *
 * \{
 */

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * Signature for task callback.
 *
 * \param[in] arg  The argument which passed by \link svx_threadpool_dispatch \endlink.
 */
typedef void (*svx_threadpool_func_t)(void *arg);

/*!
 * The type for threadpool.
 */
typedef struct svx_threadpool svx_threadpool_t;

/*!
 * To create a new thread pool.
 *
 * \param[out] self                 The pointer for return the thread pool object.
 * \param[in]  threads_cnt          The thread's count in the pool.
 * \param[in]  max_task_queue_size  The maximum size of the task queue in the thread pool.
 *                                  Zero for unlimited size.
 *
 * \note
 * If the queued task's count is exceeds max_task_queue_size, \link svx_threadpool_dispatch \endlink
 * will return \link SVX_ERRNO_REACH \endlink.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_threadpool_create(svx_threadpool_t **self, size_t threads_cnt, size_t max_task_queue_size);

/*!
 * To destroy the thread pool.
 *
 * \param[in, out] self  The second rank pointer of the thread pool.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_threadpool_destroy(svx_threadpool_t **self);

/*!
 * Dispatch a new task to the thread pool.
 *
 * \param[in] self   The address of the thread pool.
 * \param[in] run    The callback fucntion for running the task.
 * \param[in] clean  The callback fucntion for cleaning data when the task can't be run.
 * \param[in] arg    The argument pass the run or clean callback function.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_threadpool_dispatch(svx_threadpool_t *self, svx_threadpool_func_t run, svx_threadpool_func_t clean,  void *arg);

#ifdef __cplusplus
}
#endif

/* \} */

#endif
