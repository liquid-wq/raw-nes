#pragma once
#include "json.h"
#include <string>

// Haelt intern exakt dieselbe Struktur
// Gebuendeltes Schreiben (dirty >= 25 oder force=true), atomarer Replace via .tmp-Datei.
class RaCache {
public:
    explicit RaCache(std::string path);

    // Laedt Cache-Datei einmalig).
    // Bei Fehler/fehlender Datei: leerer Cache {"gameid": {}, "patch": {}}.
    Json& cache();

    // gameid-Sektion
    const Json* gameid_get(const std::string& md5) const;
    void gameid_set(const std::string& md5, Json value);

    // patch-Sektion
    const Json* patch_get(const std::string& key) const;
    void patch_set(const std::string& key, Json value);

    // Entspricht _cache_save(force): erhoeht dirty, schreibt nur wenn
    // force oder dirty >= 25, setzt dirty danach zurueck.
    void save(bool force = false);

    int dirty_count() const { return dirty_; }

private:
    std::string path_;
    bool loaded_ = false;
    int dirty_ = 0;
    Json data_;

    void load();
    void write_file() const;
};
