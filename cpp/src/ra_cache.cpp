#include "ra_cache.h"
#include <fstream>
#include <sstream>
#include <cstdio>

RaCache::RaCache(std::string path) : path_(std::move(path)) {}

void RaCache::load() {
    Json d = Json::object();
    d.set("gameid", Json::object());
    d.set("patch", Json::object());
    std::ifstream f(path_, std::ios::binary);
    if (f) {
        std::stringstream ss;
        ss << f.rdbuf();
        try {
            Json loaded = Json::parse(ss.str());
            if (loaded.type() == Json::Type::Object) {
                const Json* g = loaded.find("gameid");
                const Json* p = loaded.find("patch");
                d.set("gameid", g ? *g : Json::object());
                d.set("patch", p ? *p : Json::object());
            }
        } catch (...) {
            //
        }
    }
    data_ = std::move(d);
    loaded_ = true;
}

Json& RaCache::cache() {
    if (!loaded_) load();
    return data_;
}

const Json* RaCache::gameid_get(const std::string& md5) const {
    const Json* self_cache = &const_cast<RaCache*>(this)->cache();
    const Json* g = self_cache->find("gameid");
    return g ? g->find(md5) : nullptr;
}

void RaCache::gameid_set(const std::string& md5, Json value) {
    Json& c = cache();
    Json* g = const_cast<Json*>(c.find("gameid"));
    g->set(md5, std::move(value));
}

const Json* RaCache::patch_get(const std::string& key) const {
    const Json* self_cache = &const_cast<RaCache*>(this)->cache();
    const Json* p = self_cache->find("patch");
    return p ? p->find(key) : nullptr;
}

void RaCache::patch_set(const std::string& key, Json value) {
    Json& c = cache();
    Json* p = const_cast<Json*>(c.find("patch"));
    p->set(key, std::move(value));
}

void RaCache::write_file() const {
    std::string tmp = path_ + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary);
        f << data_.dump();
    }
    std::remove(path_.c_str());       // Windows erlaubt kein rename ueber existierende Datei
    std::rename(tmp.c_str(), path_.c_str());
}

void RaCache::save(bool force) {
    dirty_ += 1;
    if (!force && dirty_ < 25) return;
    dirty_ = 0;
    write_file();
}
