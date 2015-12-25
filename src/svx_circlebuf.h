/*
 * This source code has been dedicated to the public domain by the author.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

/*!
 * \file   svx_circlebuf.h
 * \brief  
 *
 * \author Alan Choi
 * \date   2015-12-08
 */

#ifndef SVX_CIRCLEBUF_H
#define SVX_CIRCLEBUF_H 1

#include <stdint.h>
#include <sys/types.h>

/*!
 * \defgroup Circlebuf Circlebuf
 * \ingroup  Network
 *
 * \brief    This is a circular buffer used as TCP connection's read/write buffer.
 *
 * \{
 */

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * The type for circlebuf.
 */
typedef struct svx_circlebuf svx_circlebuf_t;

/*!
 * To create a new circlebuf.
 *
 * \param[out] self      The pointer for return the circlebuf object.
 * \param[in]  max_len   The maximum length for the circlebuf.
 * \param[in]  min_len   The minimum(default) length for the circlebuf.
 * \param[in]  min_step  The minimum length for each step when expand the circlebuf.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_circlebuf_create(svx_circlebuf_t **self, size_t max_len, size_t min_len, size_t min_step);

/*!
 * To destroy a circlebuf.
 *
 * \param[in, out] self  The second rank pointer of the circlebuf.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_circlebuf_destroy(svx_circlebuf_t **self);

/*!
 * Get current buffer length for the circlebuf.
 *
 * \param[in]  self  The address of the circlebuf.
 * \param[out] len   Return the current buffer length.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_circlebuf_get_buf_len(svx_circlebuf_t *self, size_t *len);

/*!
 * Get current data length for the circlebuf.
 *
 * \param[in]  self  The address of the circlebuf.
 * \param[out] len   Return the current data length.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_circlebuf_get_data_len(svx_circlebuf_t *self, size_t *len);

/*!
 * Get current free space length for the circlebuf.
 *
 * \param[in]  self  The address of the circlebuf.
 * \param[out] len   Return the current free space length.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_circlebuf_get_freespace_len(svx_circlebuf_t *self, size_t *len);

/*!
 * To expand the buffer for the circlebuf.
 *
 * \param[in] self            The address of the circlebuf.
 * \param[in] freespace_need  The free space length we need after expanding the buffer.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_circlebuf_expand(svx_circlebuf_t *self, size_t freespace_need);

/*!
 * To shrink the buffer for the circlebuf.
 *
 * \param[in] self            The address of the circlebuf.
 * \param[in] freespace_keep  The free space length we should keep after shrinking the buffer.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_circlebuf_shrink(svx_circlebuf_t *self, size_t freespace_keep);

/*!
 * To erase all data for the circlebuf.
 *
 * \param[in] self  The address of the circlebuf.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_circlebuf_erase_all_data(svx_circlebuf_t *self);

/*!
 * To erase some data from the head of the circlebuf.
 *
 * \param[in] self  The address of the circlebuf.
 * \param[in] len   The length of data we need to erase.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_circlebuf_erase_data(svx_circlebuf_t *self, size_t len);

/*!
 * To commit some data at the end of the circlebuf.
 *
 * \param[in] self  The address of the circlebuf.
 * \param[in] len   The length of data we need to commit.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_circlebuf_commit_data(svx_circlebuf_t *self, size_t len);

/*!
 * To append some data at the end of the circlebuf.
 *
 * \param[in] self     The address of the circlebuf.
 * \param[in] buf      The buffer we will append to the circlebuf.
 * \param[in] buf_len  The buffer's length.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_circlebuf_append_data(svx_circlebuf_t *self, const uint8_t *buf, size_t buf_len);

/*!
 * Get the pointer and length of the free space buffers from the circlebuf.
 *
 * \param[in]  self      The address of the circlebuf.
 * \param[out] buf1      Return the pointer of the first free space buffer.
 * \param[out] buf1_len  Return the length of the first free space buffer.
 * \param[out] buf2      Return the pointer of the second free space buffer.
 * \param[out] buf2_len  Return the length of the second free space buffer.
 * 
 * \return  On success, return zero; on error, return an error number greater than zero.
*/
extern int svx_circlebuf_get_freespace_ptr(svx_circlebuf_t *self, uint8_t **buf1, size_t *buf1_len,
                                           uint8_t **buf2, size_t *buf2_len);

/*!
 * Get the pointer and length of the data buffers from the circlebuf.
 *
 * \param[in]  self      The address of the circlebuf.
 * \param[out] buf1      Return the pointer of the first data buffer.
 * \param[out] buf1_len  Return the length of the first data buffer.
 * \param[out] buf2      Return the pointer of the second data buffer.
 * \param[out] buf2_len  Return the length of the second data buffer.
 * 
 * \return  On success, return zero; on error, return an error number greater than zero.
*/
extern int svx_circlebuf_get_data_ptr(svx_circlebuf_t *self, uint8_t **buf1, size_t *buf1_len, 
                                      uint8_t **buf2, size_t *buf2_len);

/*!
 * Get data according to length.
 *
 * \param[in]  self     The address of the circlebuf.
 * \param[out] buf      Return the data we want.
 * \param[in]  buf_len  The data length we want.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_circlebuf_get_data(svx_circlebuf_t *self, uint8_t *buf, size_t buf_len);

/*!
 * Get data according to an ending mark.
 *
 * \param[in]  self        The address of the circlebuf.
 * \param[in]  ending      The ending mark.
 * \param[in]  ending_len  The ending mark's length.
 * \param[out] buf         Return the data we want.
 * \param[in]  buf_len     The length of the \c buf.
 * \param[out] ret_len     The data length returned by \c buf.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_circlebuf_get_data_by_ending(svx_circlebuf_t *self, const uint8_t *ending, size_t ending_len,
                                            uint8_t *buf, size_t buf_len, size_t *ret_len);

#ifdef __cplusplus
}
#endif

/* \} */

#endif
