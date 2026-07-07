#include <bb_esp_ota/http_ota.h>

#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <mbedtls/sha256.h>
#include <time.h>

namespace battlebang::esp::ota {
namespace {

constexpr unsigned long kOtaNoProgressTimeoutMs = 15000;
constexpr unsigned long kTlsClockSyncTimeoutMs = 5000;
constexpr time_t kMinValidTlsUnixTime = 1767225600;  // 2026-01-01T00:00:00Z.

// GitHub Releases currently chains through this Sectigo CA for github.com.
// Keep OTA trust anchored to a pinned CA instead of disabling TLS
// verification; local mirrors that cannot use this CA should use http:// on a
// trusted bench network or move to signed manifests before enabling auto OTA.
constexpr char kGithubReleaseRootCaPem[] = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDRjCCAsugAwIBAgIQGp6v7G3o4ZtcGTFBto2Q3TAKBggqhkjOPQQDAzCBiDEL
MAkGA1UEBhMCVVMxEzARBgNVBAgTCk5ldyBKZXJzZXkxFDASBgNVBAcTC0plcnNl
eSBDaXR5MR4wHAYDVQQKExVUaGUgVVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNVBAMT
JVVTRVJUcnVzdCBFQ0MgQ2VydGlmaWNhdGlvbiBBdXRob3JpdHkwHhcNMjEwMzIy
MDAwMDAwWhcNMzgwMTE4MjM1OTU5WjBfMQswCQYDVQQGEwJHQjEYMBYGA1UEChMP
U2VjdGlnbyBMaW1pdGVkMTYwNAYDVQQDEy1TZWN0aWdvIFB1YmxpYyBTZXJ2ZXIg
QXV0aGVudGljYXRpb24gUm9vdCBFNDYwdjAQBgcqhkjOPQIBBgUrgQQAIgNiAAR2
+pmpbiDt+dd34wc7qNs9Xzjoq1WmVk/WSOrsfy2qw7LFeeyZYX8QeccCWvkEN/U0
NSt3zn8gj1KjAIns1aeibVvjS5KToID1AZTc8GgHHs3u/iVStSBDHBv+6xnOQ6Oj
ggEgMIIBHDAfBgNVHSMEGDAWgBQ64QmG1M8ZwpZ2dEl23OA1xmNjmjAdBgNVHQ4E
FgQU0SLaTFnxS18mOKqd1u7rDcP7qWEwDgYDVR0PAQH/BAQDAgGGMA8GA1UdEwEB
/wQFMAMBAf8wHQYDVR0lBBYwFAYIKwYBBQUHAwEGCCsGAQUFBwMCMBEGA1UdIAQK
MAgwBgYEVR0gADBQBgNVHR8ESTBHMEWgQ6BBhj9odHRwOi8vY3JsLnVzZXJ0cnVz
dC5jb20vVVNFUlRydXN0RUNDQ2VydGlmaWNhdGlvbkF1dGhvcml0eS5jcmwwNQYI
KwYBBQUHAQEEKTAnMCUGCCsGAQUFBzABhhlodHRwOi8vb2NzcC51c2VydHJ1c3Qu
Y29tMAoGCCqGSM49BAMDA2kAMGYCMQCMCyBit99vX2ba6xEkDe+YO7vC0twjbkv9
PKpqGGuZ61JZryjFsp+DFpEclCVy4noCMQCwvZDXD/m2Ko1HA5Bkmz7YQOFAiNDD
49IWa2wdT7R3DtODaSXH/BiXv8fwB9su4tU=
-----END CERTIFICATE-----
)EOF";

bool tlsClockLooksValid() {
  return time(nullptr) >= kMinValidTlsUnixTime;
}

bool ensureTlsClock() {
  if (tlsClockLooksValid()) return true;
  Serial.println("[bb_esp_ota] syncing clock for HTTPS certificate validation");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  const unsigned long startedMs = millis();
  while (millis() - startedMs < kTlsClockSyncTimeoutMs) {
    if (tlsClockLooksValid()) return true;
    delay(100);
  }
  Serial.println("[bb_esp_ota] TLS clock sync timed out");
  return false;
}

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
    if (!ensureTlsClock()) return false;
    secureClient.setCACert(kGithubReleaseRootCaPem);
    return http.begin(secureClient, url);
  }

  return http.begin(plainClient, url);
}

}  // namespace

bool fetchHttpText(const String& url, size_t maxBytes, String& body, String& error) {
  WiFiClient plainClient;
  WiFiClientSecure secureClient;
  HTTPClient http;

  Serial.print("[bb_esp_ota][http] GET ");
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

  Serial.print("[bb_esp_ota] downloading ");
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
  unsigned long lastByteMs = millis();
  unsigned long lastProgressLogMs = lastByteMs;

  while (http.connected() && (contentLength < 0 || written < static_cast<size_t>(contentLength))) {
    const size_t available = stream->available();
    if (available == 0) {
      if (millis() - lastByteMs > kOtaNoProgressTimeoutMs) {
        result.message = "download stalled with no progress";
        Update.abort();
        mbedtls_sha256_free(&sha);
        http.end();
        return result;
      }
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
    lastByteMs = now;
    if (now - lastProgressLogMs > 2000) {
      lastProgressLogMs = now;
      Serial.print("[bb_esp_ota] bytes=");
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

}  // namespace battlebang::esp::ota
