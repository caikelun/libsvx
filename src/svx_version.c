/*
 * This source code has been dedicated to the public domain by the authors.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

#include "svx_version.h"

#define SVX_VERSION_MAJOR 0
#define SVX_VERSION_MINOR 0
#define SVX_VERSION_EXTRA 11

#define SVX_VERSION ((SVX_VERSION_MAJOR << 16) | (SVX_VERSION_MINOR <<  8) | (SVX_VERSION_EXTRA))

#define SVX_VERSION_TO_STR_HELPER(x) #x
#define SVX_VERSION_TO_STR(x) SVX_VERSION_TO_STR_HELPER(x)

#define SVX_VERSION_STR SVX_VERSION_TO_STR(SVX_VERSION_MAJOR) "." \
                        SVX_VERSION_TO_STR(SVX_VERSION_MINOR) "." \
                        SVX_VERSION_TO_STR(SVX_VERSION_EXTRA)

#define SVX_VERSION_STR_FULL "libSVX (service X library) "SVX_VERSION_STR

unsigned int svx_version()
{
    return SVX_VERSION;
}

const char *svx_version_str()
{
    return SVX_VERSION_STR;
}

const char *svx_version_str_full()
{
    return SVX_VERSION_STR_FULL;
}
