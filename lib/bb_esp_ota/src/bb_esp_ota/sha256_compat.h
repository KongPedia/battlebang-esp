#pragma once

#include <mbedtls/sha256.h>
#include <mbedtls/version.h>

namespace battlebang::esp::ota::detail {

inline int sha256Starts(mbedtls_sha256_context* context, int is224) {
#if MBEDTLS_VERSION_MAJOR >= 3
  return mbedtls_sha256_starts(context, is224);
#else
  return mbedtls_sha256_starts_ret(context, is224);
#endif
}

inline int sha256Update(mbedtls_sha256_context* context, const unsigned char* input, size_t length) {
#if MBEDTLS_VERSION_MAJOR >= 3
  return mbedtls_sha256_update(context, input, length);
#else
  return mbedtls_sha256_update_ret(context, input, length);
#endif
}

inline int sha256Finish(mbedtls_sha256_context* context, unsigned char output[32]) {
#if MBEDTLS_VERSION_MAJOR >= 3
  return mbedtls_sha256_finish(context, output);
#else
  return mbedtls_sha256_finish_ret(context, output);
#endif
}

}  // namespace battlebang::esp::ota::detail
