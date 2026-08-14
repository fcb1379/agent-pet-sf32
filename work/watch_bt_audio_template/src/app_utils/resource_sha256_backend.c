/*
 * The watch target does not otherwise enable the optional mbedTLS SHA-256
 * translation unit. Compile the SDK implementation through a project-owned
 * wrapper, matching the existing MD5 backend arrangement.
 */
#define MBEDTLS_SHA256_C
#include "../../../../sdk/external/mbedtls/library/sha256.c"
