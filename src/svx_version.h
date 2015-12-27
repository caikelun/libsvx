/*
 * This source code has been dedicated to the public domain by the authors.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

/*!
 * \file   svx_version.h
 * \brief  
 *
 * \author Alan Choi
 * \date   2015-10-28
 */

#ifndef SVX_VERSION_H
#define SVX_VERSION_H 1

/*!
 * \defgroup Base Base
 */

/*!
 * \defgroup Network Network
 */

/*!
 * \defgroup Version Version
 * \ingroup  Base
 *
 * \brief    Get the version number of libsvx.
 *
 * \{
 */

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * Get the version number as an unsigned integer.
 *
 * \return  The version number as an unsigned integer.
 */
extern unsigned int svx_version();

/*!
 * Get the version number as a string.
 *
 * \return  The version number as a string.
 */
extern const char *svx_version_str();

/*!
 * Get the version number as a descriptive string.
 *
 * \return  The version number as a descriptive string.
 */
extern const char *svx_version_str_full();

#ifdef __cplusplus
}
#endif

/* \} */

#endif
