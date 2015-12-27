/*
 * This source code has been dedicated to the public domain by the authors.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

/*!
 * \file   svx_looper.h
 * \brief  
 *
 * \author Alan Choi
 * \date   2015-12-09
 */

#ifndef SVX_LOOPER_H
#define SVX_LOOPER_H 1

#include <stdint.h>
#include <sys/types.h>
#include "svx_channel.h"

/*!
 * \defgroup Looper Looper
 * \ingroup  Network
 *
 * \brief    Module used to run a event loop for a thread. With \c poller and
 *           \c channel module, this three modules provides a mechanism to 
 *           execute a callback function when a specific event occurs on a 
 *           file descriptor or after a timeout has been reached.
 *
 * \{
 */

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * The unique ID for a timer.
 */
typedef struct
{
    time_t   create_time; /*!< The create time. */
    uint64_t sequence;    /*!< An incremental sequence value. */
} svx_looper_timer_id_t;

/*!
 * The initializer for timer ID.
 */
#define SVX_LOOPER_TIMER_ID_INITIALIZER {0, 0}

/*!
 * To initialize a given timer ID.
 */
#define SVX_LOOPER_TIMER_ID_INIT(timer_id)          \
    do {                                            \
        (timer_id)->create_time = 0;                \
        (timer_id)->sequence = 0;                   \
    } while(0)

/*!
 * To check if the given timer ID have a initial value.
 */
#define SVX_LOOPER_TIMER_ID_IS_INITIALIZER(timer_id) \
    (0 == (timer_id)->create_time && 0 == (timer_id)->sequence)

/*!
 * Signature for timer callback.
 *
 * \param[in] arg  The argument which passed by \link svx_looper_run_at \endlink,
 *                 \link svx_looper_run_after \endlink and \link svx_looper_run_every \endlink.
 */
typedef void (*svx_looper_func_t)(void *arg);

/*!
 * The type for looper.
 */
typedef struct svx_looper svx_looper_t;

/*!
 * To create a new looper.
 *
 * \param[out] self  The pointer for return the looper object.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_looper_create(svx_looper_t **self);

/*!
 * To destroy a looper.
 *
 * \param[in, out] self  The second rank pointer of the looper.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_looper_destroy(svx_looper_t **self);

/*!
 * To initialize the given channel for the current looper.
 *
 * \warning  This function is for internal use.
 *
 * \param[in] self     The address of the looper.
 * \param[in] channel  The channel we want to initialize.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_looper_init_channel(svx_looper_t *self, svx_channel_t *channel);

/*!
 * To update the looper's status according to the given channel.
 *
 * \warning  This function is for internal use.
 *
 * \param[in] self     The address of the looper.
 * \param[in] channel  The channel with the newest status.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_looper_update_channel(svx_looper_t *self, svx_channel_t *channel);

/*!
 * Run the event loop in this thread.
 *
 * \note  The calling thread will blocked here until another thread call 
 *        \link svx_looper_quit \endlink.
 *
 * \param[in] self  The address of the looper.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_looper_loop(svx_looper_t *self);

/*!
 * Quits the event loop.
 *
 * \param[in] self  The address of the looper.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_looper_quit(svx_looper_t *self);

/*!
 * Wake up the event loop. This will cause the looper to execute pending jobs immediately 
 *
 * \param[in] self  The address of the looper.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_looper_wakeup(svx_looper_t *self);

/*!
 * Add a timer task which will run once at a specified time.
 *
 * \param[in]  self      The address of the looper.
 * \param[in]  run       The callback fucntion for running the task.
 * \param[in]  clean     The callback fucntion for cleaning data when the task can't be run.
 * \param[in]  arg       The argument pass the \c run or \c clean callback function.
 * \param[in]  when_ms   The time on millisecond when to run the task.
 * \param[out] timer_id  Return the Unique timer ID.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_looper_run_at(svx_looper_t *self, svx_looper_func_t run, svx_looper_func_t clean, 
                             void *arg, int64_t when_ms, svx_looper_timer_id_t *timer_id);

/*!
 * Add a timer task which will run once after a delay from now.
 *
 * \param[in]  self      The address of the looper.
 * \param[in]  run       The callback fucntion for running the task.
 * \param[in]  clean     The callback fucntion for cleaning data when the task can't be run.
 * \param[in]  arg       The argument pass the \c run or \c clean callback function.
 * \param[in]  delay_ms  The delay on millisecond from now.
 * \param[out] timer_id  Return the Unique timer ID.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_looper_run_after(svx_looper_t *self, svx_looper_func_t run, svx_looper_func_t clean, 
                                void *arg, int64_t delay_ms, svx_looper_timer_id_t *timer_id);

/*!
 * Add a timer task which will run repeatedly after a delay from now and with a given interval.
 *
 * \param[in]  self         The address of the looper.
 * \param[in]  run          The callback fucntion for running the task.
 * \param[in]  clean        The callback fucntion for cleaning data when the task can't be run.
 * \param[in]  arg          The argument pass the \c run or \c clean callback function.
 * \param[in]  delay_ms     The delay on millisecond from now.
 * \param[in]  interval_ms  The interval on millisecond.
 * \param[out] timer_id     Return the unique timer ID.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_looper_run_every(svx_looper_t *self, svx_looper_func_t run, svx_looper_func_t clean, 
                                void *arg, int64_t delay_ms, int64_t interval_ms, svx_looper_timer_id_t *timer_id);

/*!
 * Cancel a timer task use it's unique timer ID.
 *
 * \param[in] self      The address of the looper.
 * \param[in] timer_id  The unique timer ID.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_looper_cancel(svx_looper_t *self, svx_looper_timer_id_t timer_id);

/*!
 * To check if the calling thread is the one running the event loop.
 *
 * \param[in] self  The address of the looper.
 *
 * \return  Return \c 1 for TURE, \c 0 for FLASE.
 */
extern int svx_looper_is_loop_thread(svx_looper_t *self);

/*!
 * Add a task to the pending task queue. The task will be run on the next round in the event loop.
 *
 * \param[in] self            The address of the looper.
 * \param[in] run             The callback fucntion for running the task.
 * \param[in] clean           The callback fucntion for cleaning data when the task can't be run.
 * \param[in] arg_block       The argument pass the run or clean callback function. This arguments
 *                            will be shallow copy to the pending task queue.
 * \param[in] arg_block_size  The argument total size.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_looper_dispatch(svx_looper_t *self, svx_looper_func_t run, svx_looper_func_t clean,
                               void *arg_block, size_t arg_block_size);

/*!
 * To generate \c run function wrapper for the given function without argument.
 */
#define SVX_LOOPER_GENERATE_RUN_0(f)                                    \
    static void f##_run(void *arg) {                                    \
        f(NULL);                                                        \
    }

/*!
 * To generate \c run function wrapper for the given function with 1 argument.
 */
#define SVX_LOOPER_GENERATE_RUN_1(f, t1, p1)                            \
    typedef struct {                                                    \
        t1 p1;                                                          \
    } f##_param_t;                                                      \
    static void f##_run(void *arg) {                                    \
        f##_param_t *p = (f##_param_t *)arg;                            \
        f(p->p1);                                                       \
    }

/*!
 * To generate \c run function wrapper for the given function with 2 arguments.
 */
#define SVX_LOOPER_GENERATE_RUN_2(f, t1, p1, t2, p2)                    \
    typedef struct {                                                    \
        t1 p1; t2 p2;                                                   \
    } f##_param_t;                                                      \
    static void f##_run(void *arg) {                                    \
        f##_param_t *p = (f##_param_t *)arg;                            \
        f(p->p1, p->p2);                                                \
    }

/*!
 * To generate \c run function wrapper for the given function with 3 arguments.
 */
#define SVX_LOOPER_GENERATE_RUN_3(f, t1, p1, t2, p2, t3, p3)            \
    typedef struct {                                                    \
        t1 p1; t2 p2; t3 p3;                                            \
    } f##_param_t;                                                      \
    static void f##_run(void *arg) {                                    \
        f##_param_t *p = (f##_param_t *)arg;                            \
        f(p->p1, p->p2, p->p3);                                         \
    }

/*!
 * To generate \c run function wrapper for the given function with 4 arguments.
 */
#define SVX_LOOPER_GENERATE_RUN_4(f, t1, p1, t2, p2, t3, p3, t4, p4)    \
    typedef struct {                                                    \
        t1 p1; t2 p2; t3 p3; t4 p4;                                     \
    } f##_param_t;                                                      \
    static void f##_run(void *arg) {                                    \
        f##_param_t *p = (f##_param_t *)arg;                            \
        f(p->p1, p->p2, p->p3, p->p4);                                  \
    }

/*!
 * To generate \c run function wrapper for the given function with 5 arguments.
 */
#define SVX_LOOPER_GENERATE_RUN_5(f, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5) \
    typedef struct {                                                    \
        t1 p1; t2 p2; t3 p3; t4 p4; t5 p5;                              \
    } f##_param_t;                                                      \
    static void f##_run(void *arg) {                                    \
        f##_param_t *p = (f##_param_t *)arg;                            \
        f(p->p1, p->p2, p->p3, p->p4, p->p5);                           \
    }

/*!
 * To generate \c run function wrapper for the given function with 6 arguments.
 */
#define SVX_LOOPER_GENERATE_RUN_6(f, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6) \
    typedef struct {                                                    \
        t1 p1; t2 p2; t3 p3; t4 p4; t5 p5; t6 p6;                       \
    } f##_param_t;                                                      \
    static void f##_run(void *arg) {                                    \
        f##_param_t *p = (f##_param_t *)arg;                            \
        f(p->p1, p->p2, p->p3, p->p4, p->p5, p->p6);                    \
    }

/*!
 * To generate \c run function wrapper for the given function with 7 arguments.
 */
#define SVX_LOOPER_GENERATE_RUN_7(f, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6, t7, p7) \
    typedef struct {                                                    \
        t1 p1; t2 p2; t3 p3; t4 p4; t5 p5; t6 p6; t7 p7;                \
    } f##_param_t;                                                      \
    static void f##_run(void *arg) {                                    \
        f##_param_t *p = (f##_param_t *)arg;                            \
        f(p->p1, p->p2, p->p3, p->p4, p->p5, p->p6, p->p7);             \
    }

/*!
 * To generate \c run function wrapper for the given function with 8 arguments.
 */
#define SVX_LOOPER_GENERATE_RUN_8(f, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6, t7, p7, t8, p8) \
    typedef struct {                                                    \
        t1 p1; t2 p2; t3 p3; t4 p4; t5 p5; t6 p6; t7 p7; t8 p8;         \
    } f##_param_t;                                                      \
    static void f##_run(void *arg) {                                    \
        f##_param_t *p = (f##_param_t *)arg;                            \
        f(p->p1, p->p2, p->p3, p->p4, p->p5, p->p6, p->p7, p->p8);      \
    }

/*!
 * To generate \c run function wrapper for the given function with 9 arguments.
 */
#define SVX_LOOPER_GENERATE_RUN_9(f, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6, t7, p7, t8, p8, t9, p9) \
    typedef struct {                                                    \
        t1 p1; t2 p2; t3 p3; t4 p4; t5 p5; t6 p6; t7 p7; t8 p8; t9 p9;  \
    } f##_param_t;                                                      \
    static void f##_run(void *arg) {                                    \
        f##_param_t *p = (f##_param_t *)arg;                            \
        f(p->p1, p->p2, p->p3, p->p4, p->p5, p->p6, p->p7, p->p8, p->p9); \
    }

/*!
 * To generate the dispatch code snippet for the given function without argument.
 */
#define SVX_LOOPER_DISPATCH_HELPER_0(looper, f)                         \
    do{                                                                 \
        svx_looper_dispatch(looper, f##_run, NULL, NULL, 0);            \
    } while(0)

/*!
 * To generate the dispatch code snippet for the given function with 1 argument.
 */
#define SVX_LOOPER_DISPATCH_HELPER_1(looper, f, p1)                     \
    do{                                                                 \
        f##_param_t p = {p1};                                           \
        svx_looper_dispatch(looper, f##_run, NULL, &p, sizeof(p));      \
    } while(0)

/*!
 * To generate the dispatch code snippet for the given function with 2 arguments.
 */
#define SVX_LOOPER_DISPATCH_HELPER_2(looper, f, p1, p2)                 \
    do {                                                                \
        f##_param_t p = {p1, p2};                                       \
        svx_looper_dispatch(looper, f##_run, NULL, &p, sizeof(p));      \
    } while(0)

/*!
 * To generate the dispatch code snippet for the given function with 3 arguments.
 */
#define SVX_LOOPER_DISPATCH_HELPER_3(looper, f, p1, p2, p3)             \
    do {                                                                \
        f##_param_t p = {p1, p2, p3};                                   \
        svx_looper_dispatch(looper, f##_run, NULL, &p, sizeof(p));      \
    } while(0)

/*!
 * To generate the dispatch code snippet for the given function with 4 arguments.
 */
#define SVX_LOOPER_DISPATCH_HELPER_4(looper, f, p1, p2, p3, p4)         \
    do {                                                                \
        f##_param_t p = {p1, p2, p3, p4};                               \
        svx_looper_dispatch(looper, f##_run, NULL, &p, sizeof(p));      \
    } while(0)

/*!
 * To generate the dispatch code snippet for the given function with 5 arguments.
 */
#define SVX_LOOPER_DISPATCH_HELPER_5(looper, f, p1, p2, p3, p4, p5)     \
    do {                                                                \
        f##_param_t p = {p1, p2, p3, p4, p5};                           \
        svx_looper_dispatch(looper, f##_run, NULL, &p, sizeof(p));      \
    } while(0)

/*!
 * To generate the dispatch code snippet for the given function with 6 arguments.
 */
#define SVX_LOOPER_DISPATCH_HELPER_6(looper, f, p1, p2, p3, p4, p5, p6) \
    do {                                                                \
        f##_param_t p = {p1, p2, p3, p4, p5, p6};                       \
        svx_looper_dispatch(looper, f##_run, NULL, &p, sizeof(p));      \
    } while(0)

/*!
 * To generate the dispatch code snippet for the given function with 7 arguments.
 */
#define SVX_LOOPER_DISPATCH_HELPER_7(looper, f, p1, p2, p3, p4, p5, p6, p7) \
    do {                                                                \
        f##_param_t p = {p1, p2, p3, p4, p5, p6, p7};                   \
        svx_looper_dispatch(looper, f##_run, NULL, &p, sizeof(p));      \
    } while(0)

/*!
 * To generate the dispatch code snippet for the given function with 8 arguments.
 */
#define SVX_LOOPER_DISPATCH_HELPER_8(looper, f, p1, p2, p3, p4, p5, p6, p7, p8) \
    do {                                                                \
        f##_param_t p = {p1, p2, p3, p4, p5, p6, p7, p8};               \
        svx_looper_dispatch(looper, f##_run, NULL, &p, sizeof(p));      \
    } while(0)

/*!
 * To generate the dispatch code snippet for the given function with 9 arguments.
 */
#define SVX_LOOPER_DISPATCH_HELPER_9(looper, f, p1, p2, p3, p4, p5, p6, p7, p8, p9) \
    do {                                                                \
        f##_param_t p = {p1, p2, p3, p4, p5, p6, p7, p8, p9};           \
        svx_looper_dispatch(looper, f##_run, NULL, &p, sizeof(p));      \
    } while(0)

/*!
 * To generate the check and dispatch code snippet for the given function without argument.
 */
#define SVX_LOOPER_CHECK_DISPATCH_HELPER_0(looper, f)                   \
    do {                                                                \
        if(!svx_looper_is_loop_thread((looper))) {                      \
            svx_looper_dispatch(looper, f##_run, NULL, NULL, 0);        \
            return 0;                                                   \
        }                                                               \
    } while(0)

/*!
 * To generate the check and dispatch code snippet for the given function with 1 argument.
 */
#define SVX_LOOPER_CHECK_DISPATCH_HELPER_1(looper, f, p1)               \
    do {                                                                \
        if(!svx_looper_is_loop_thread((looper))) {                      \
            f##_param_t p = {p1};                                       \
            svx_looper_dispatch(looper, f##_run, NULL, &p, sizeof(p));  \
            return 0;                                                   \
        }                                                               \
    } while(0)

/*!
 * To generate the check and dispatch code snippet for the given function with 2 arguments.
 */
#define SVX_LOOPER_CHECK_DISPATCH_HELPER_2(looper, f, p1, p2)           \
    do {                                                                \
        if(!svx_looper_is_loop_thread((looper))) {                      \
            f##_param_t p = {p1, p2};                                   \
            svx_looper_dispatch(looper, f##_run, NULL, &p, sizeof(p));  \
            return 0;                                                   \
        }                                                               \
    } while(0)

/*!
 * To generate the check and dispatch code snippet for the given function with 3 arguments.
 */
#define SVX_LOOPER_CHECK_DISPATCH_HELPER_3(looper, f, p1, p2, p3)       \
    do {                                                                \
        if(!svx_looper_is_loop_thread((looper))) {                      \
            f##_param_t p = {p1, p2, p3};                               \
            svx_looper_dispatch(looper, f##_run, NULL, &p, sizeof(p));  \
            return 0;                                                   \
        }                                                               \
    } while(0)

/*!
 * To generate the check and dispatch code snippet for the given function with 4 arguments.
 */
#define SVX_LOOPER_CHECK_DISPATCH_HELPER_4(looper, f, p1, p2, p3, p4)   \
    do {                                                                \
        if(!svx_looper_is_loop_thread((looper))) {                      \
            f##_param_t p = {p1, p2, p3, p4};                           \
            svx_looper_dispatch(looper, f##_run, NULL, &p, sizeof(p));  \
            return 0;                                                   \
        }                                                               \
    } while(0)

/*!
 * To generate the check and dispatch code snippet for the given function with 5 arguments.
 */
#define SVX_LOOPER_CHECK_DISPATCH_HELPER_5(looper, f, p1, p2, p3, p4, p5) \
    do {                                                                \
        if(!svx_looper_is_loop_thread((looper))) {                      \
            f##_param_t p = {p1, p2, p3, p4, p5};                       \
            svx_looper_dispatch(looper, f##_run, NULL, &p, sizeof(p));  \
            return 0;                                                   \
        }                                                               \
    } while(0)

/*!
 * To generate the check and dispatch code snippet for the given function with 6 arguments.
 */
#define SVX_LOOPER_CHECK_DISPATCH_HELPER_6(looper, f, p1, p2, p3, p4, p5, p6) \
    do {                                                                \
        if(!svx_looper_is_loop_thread((looper))) {                      \
            f##_param_t p = {p1, p2, p3, p4, p5, p6};                   \
            svx_looper_dispatch(looper, f##_run, NULL, &p, sizeof(p));  \
            return 0;                                                   \
        }                                                               \
    } while(0)

/*!
 * To generate the check and dispatch code snippet for the given function with 7 arguments.
 */
#define SVX_LOOPER_CHECK_DISPATCH_HELPER_7(looper, f, p1, p2, p3, p4, p5, p6, p7) \
    do {                                                                \
        if(!svx_looper_is_loop_thread((looper))) {                      \
            f##_param_t p = {p1, p2, p3, p4, p5, p6, p7};               \
            svx_looper_dispatch(looper, f##_run, NULL, &p, sizeof(p));  \
            return 0;                                                   \
        }                                                               \
    } while(0)

/*!
 * To generate the check and dispatch code snippet for the given function with 8 arguments.
 */
#define SVX_LOOPER_CHECK_DISPATCH_HELPER_8(looper, f, p1, p2, p3, p4, p5, p6, p7, p8) \
    do {                                                                \
        if(!svx_looper_is_loop_thread((looper))) {                      \
            f##_param_t p = {p1, p2, p3, p4, p5, p6, p7, p8};           \
            svx_looper_dispatch(looper, f##_run, NULL, &p, sizeof(p));  \
            return 0;                                                   \
        }                                                               \
    } while(0)

/*!
 * To generate the check and dispatch code snippet for the given function with 9 arguments.
 */
#define SVX_LOOPER_CHECK_DISPATCH_HELPER_9(looper, f, p1, p2, p3, p4, p5, p6, p7, p8, p9) \
    do {                                                                \
        if(!svx_looper_is_loop_thread((looper))) {                      \
            f##_param_t p = {p1, p2, p3, p4, p5, p6, p7, p8, p9};       \
            svx_looper_dispatch(looper, f##_run, NULL, &p, sizeof(p));  \
            return 0;                                                   \
        }                                                               \
    } while(0)

#ifdef __cplusplus
}
#endif

/* \} */

#endif
