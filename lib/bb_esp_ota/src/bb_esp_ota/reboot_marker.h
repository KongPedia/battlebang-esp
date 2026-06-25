#pragma once

namespace battlebang::esp::ota {

bool writeRebootMarker(const char* nvsNamespace, const char* key, bool value);
bool consumeRebootMarker(const char* nvsNamespace, const char* key);

}  // namespace battlebang::esp::ota
