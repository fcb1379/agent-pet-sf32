/*
 * Agent Pet MD5 backend.
 *
 * The SiFli SDK already contains the Apache-2.0 mbedTLS MD5 implementation,
 * but the watch target does not enable that optional translation unit. Keep
 * the feature define local and compile the SDK implementation through this
 * project-owned wrapper so generated objects remain inside the build tree.
 */
#define MBEDTLS_MD5_C
#include "../../../../sdk/external/mbedtls/library/md5.c"
