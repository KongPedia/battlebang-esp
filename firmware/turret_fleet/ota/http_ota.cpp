#include "http_ota.h"

#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <mbedtls/sha256.h>

namespace battlebang {
namespace turret_fleet {
namespace {

String toHex(const uint8_t* bytes, size_t len) {
  static const char* kHex = "0123456789abcdef";
  String out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; ++i) {
    out += kHex[(bytes[i] >> 4) & 0x0F];
    out += kHex[bytes[i] & 0x0F];
  }
  return out;
}

bool beginHttp(HTTPClient& http,
               WiFiClient& plainClient,
               WiFiClientSecure& secureClient,
               const String& url) {
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(15000);

  if (url.startsWith("https://")) {
    // Prototype mode for GitHub Release tests. Production should pin a CA
    // certificate or verify a signed manifest/firmware image.
    secureClient.setInsecure();
    return http.begin(secureClient, url);
  }

  return http.begin(plainClient, url);
}

}  // namespace

bool fetchHttpText(const String& url, size_t maxBytes, String& body, String& error) {
  WiFiClient plainClient;
  WiFiClientSecure secureClient;
  HTTPClient http;

  Serial.print("[fleet][http] GET ");
  Serial.println(url);

  if (!beginHttp(http, plainClient, secureClient, url)) {
    error = "http.begin failed";
    return false;
  }

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    error = String("http status ") + code;
    http.end();
    return false;
  }

  const int contentLength = http.getSize();
  if (contentLength > 0 && static_cast<size_t>(contentLength) > maxBytes) {
    error = "response too large";
    http.end();
    return false;
  }

  body = http.getString();
  http.end();
  if (body.length() > maxBytes) {
    error = "response too large";
    return false;
  }

  error = "";
  return true;
}

OtaResult runHttpOta(const OtaManifest& manifest) {
  OtaResult result;
  WiFiClient plainClient;
  WiFiClientSecure secureClient;
  HTTPClient http;

  Serial.print("[fleet][ota] downloading ");
  Serial.println(manifest.url);

  if (!beginHttp(http, plainClient, secureClient, manifest.url)) {
    result.message = "http.begin failed";
    return result;
  }

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    result.message = String("http status ") + code;
    http.end();
    return result;
  }

  const int contentLength = http.getSize();
  const size_t updateSize = contentLength > 0 ? static_cast<size_t>(contentLength) : UPDATE_SIZE_UNKNOWN;
  if (manifest.size > 0 && contentLength > 0 && static_cast<uint32_t>(contentLength) != manifest.size) {
    result.message = "content-length does not match manifest.size";
    http.end();
    return result;
  }

  if (!Update.begin(updateSize)) {
    result.message = String("Update.begin failed: ") + Update.errorString();
    http.end();
    return result;
  }

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts_ret(&sha, 0);

  uint8_t buffer[1024];
  WiFiClient* stream = http.getStreamPtr();
  size_t written = 0;
  unsigned long lastProgressMs = millis();

  while (http.connected() && (contentLength < 0 || written < static_cast<size_t>(contentLength))) {
    const size_t available = stream->available();
    if (available == 0) {
      delay(2);
      continue;
    }

    const size_t toRead = min(available, sizeof(buffer));
    const int bytesRead = stream->readBytes(buffer, toRead);
    if (bytesRead <= 0) continue;

    mbedtls_sha256_update_ret(&sha, buffer, bytesRead);
    const size_t bytesWritten = Update.write(buffer, bytesRead);
    if (bytesWritten != static_cast<size_t>(bytesRead)) {
      result.message = String("Update.write failed: ") + Update.errorString();
      Update.abort();
      mbedtls_sha256_free(&sha);
      http.end();
      return result;
    }

    written += bytesWritten;
    const unsigned long now = millis();
    if (now - lastProgressMs > 2000) {
      lastProgressMs = now;
      Serial.print("[fleet][ota] bytes=");
      Serial.println(written);
    }
  }

  uint8_t digest[32];
  mbedtls_sha256_finish_ret(&sha, digest);
  mbedtls_sha256_free(&sha);
  const String actualSha = toHex(digest, sizeof(digest));

  if (!actualSha.equalsIgnoreCase(manifest.sha256)) {
    result.message = String("sha256 mismatch actual=") + actualSha;
    Update.abort();
    http.end();
    return result;
  }

  if (!Update.end(true)) {
    result.message = String("Update.end failed: ") + Update.errorString();
    http.end();
    return result;
  }

  http.end();
  result.ok = true;
  result.message = "ota applied; reboot required";
  return result;
}

}  // namespace turret_fleet
}  // namespace battlebang
