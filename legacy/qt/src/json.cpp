#include "json.h"
#include <stdexcept>
#include <sstream>
#include <cstdio>
#include <cstring>

void Json::set(const std::string& key, Json v) {
    for (auto& kv : obj_) {
        if (kv.first == key) { kv.second = std::move(v); return; }
    }
    obj_.emplace_back(key, std::move(v));
}

const Json* Json::find(const std::string& key) const {
    for (const auto& kv : obj_) if (kv.first == key) return &kv.second;
    return nullptr;
}

namespace {

// Dekodiert einen UTF-8-String zu Unicode-Codepoints.
std::vector<uint32_t> utf8_decode(const std::string& s) {
    std::vector<uint32_t> out;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp; int len;
        if ((c & 0x80) == 0) { cp = c; len = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
        else { cp = c; len = 1; } // ungueltiges Byte: als einzelnes Zeichen behandeln
        for (int k = 1; k < len && i + k < s.size(); ++k)
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        out.push_back(cp);
        i += len;
    }
    return out;
}

void utf8_encode_append(std::string& out, uint32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

void dump_string(const std::string& s, std::string& out) {
    out.push_back('"');
    for (uint32_t cp : utf8_decode(s)) {
        switch (cp) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (cp < 0x20 || cp > 0x7E) {
                    char buf[16];
                    if (cp > 0xFFFF) {
                        // Surrogatpaar (Python-Verhalten für astrale Zeichen)
                        uint32_t v = cp - 0x10000;
                        uint32_t hi = 0xD800 + (v >> 10);
                        uint32_t lo = 0xDC00 + (v & 0x3FF);
                        std::snprintf(buf, sizeof(buf), "\\u%04x", hi);
                        out += buf;
                        std::snprintf(buf, sizeof(buf), "\\u%04x", lo);
                        out += buf;
                    } else {
                        std::snprintf(buf, sizeof(buf), "\\u%04x", cp);
                        out += buf;
                    }
                } else {
                    out.push_back(static_cast<char>(cp));
                }
        }
    }
    out.push_back('"');
}

} // namespace

void Json::dump_to(std::string& out) const {
    switch (type_) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += b_ ? "true" : "false"; break;
        case Type::Int: out += std::to_string(i_); break;
        case Type::Double: {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.17g", d_);
            out += buf;
            break;
        }
        case Type::String: dump_string(s_, out); break;
        case Type::Array: {
            out.push_back('[');
            for (size_t k = 0; k < arr_.size(); ++k) {
                if (k) out += ", ";
                arr_[k].dump_to(out);
            }
            out.push_back(']');
            break;
        }
        case Type::Object: {
            out.push_back('{');
            for (size_t k = 0; k < obj_.size(); ++k) {
                if (k) out += ", ";
                dump_string(obj_[k].first, out);
                out += ": ";
                obj_[k].second.dump_to(out);
            }
            out.push_back('}');
            break;
        }
    }
}

std::string Json::dump() const {
    std::string out;
    dump_to(out);
    return out;
}

void Json::dump_to_indent(std::string& out, int indent, int level) const {
    std::string pad((level + 1) * indent, ' ');
    std::string pad_close(level * indent, ' ');
    if (type_ == Type::Array) {
        if (arr_.empty()) { out += "[]"; return; }
        out += "[\n";
        for (size_t k = 0; k < arr_.size(); ++k) {
            out += pad;
            arr_[k].dump_to_indent(out, indent, level + 1);
            if (k + 1 < arr_.size()) out += ",";
            out += "\n";
        }
        out += pad_close + "]";
    } else if (type_ == Type::Object) {
        if (obj_.empty()) { out += "{}"; return; }
        out += "{\n";
        for (size_t k = 0; k < obj_.size(); ++k) {
            out += pad;
            Json keyJson = Json::string(obj_[k].first);
            std::string ks; keyJson.dump_to(ks);
            out += ks + ": ";
            obj_[k].second.dump_to_indent(out, indent, level + 1);
            if (k + 1 < obj_.size()) out += ",";
            out += "\n";
        }
        out += pad_close + "}";
    } else {
        dump_to(out); // Skalare: wie einzeilig
    }
}

std::string Json::dump(int indent) const {
    std::string out;
    dump_to_indent(out, indent, 0);
    return out;
}

namespace {

struct Parser {
    const std::string& s;
    size_t pos = 0;
    explicit Parser(const std::string& str) : s(str) {}

    void skip_ws() { while (pos < s.size() && (s[pos]==' '||s[pos]=='\t'||s[pos]=='\n'||s[pos]=='\r')) pos++; }

    [[noreturn]] void err(const std::string& msg) {
        throw std::runtime_error("JSON-Parserfehler bei Position " + std::to_string(pos) + ": " + msg);
    }

    Json parse_value() {
        skip_ws();
        if (pos >= s.size()) err("unerwartetes Ende");
        char c = s[pos];
        if (c == '{') return parse_object();
        if (c == '[') return parse_array();
        if (c == '"') return Json::string(parse_string());
        if (c == 't') { expect_lit("true"); return Json::boolean(true); }
        if (c == 'f') { expect_lit("false"); return Json::boolean(false); }
        if (c == 'n') { expect_lit("null"); return Json::null(); }
        return parse_number();
    }

    void expect_lit(const char* lit) {
        size_t n = std::strlen(lit);
        if (s.compare(pos, n, lit) != 0) err(std::string("erwartet: ") + lit);
        pos += n;
    }

    std::string parse_string() {
        if (s[pos] != '"') err("erwartet: \"");
        pos++;
        std::string out;
        while (true) {
            if (pos >= s.size()) err("unerwartetes Ende in String");
            unsigned char c = s[pos];
            if (c == '"') { pos++; break; }
            if (c == '\\') {
                pos++;
                if (pos >= s.size()) err("unerwartetes Ende nach \\");
                char e = s[pos];
                switch (e) {
                    case '"': out.push_back('"'); pos++; break;
                    case '\\': out.push_back('\\'); pos++; break;
                    case '/': out.push_back('/'); pos++; break;
                    case 'b': out.push_back('\b'); pos++; break;
                    case 'f': out.push_back('\f'); pos++; break;
                    case 'n': out.push_back('\n'); pos++; break;
                    case 'r': out.push_back('\r'); pos++; break;
                    case 't': out.push_back('\t'); pos++; break;
                    case 'u': {
                        pos++;
                        if (pos + 4 > s.size()) err("unvollstaendiges \\u");
                        uint32_t cp = std::stoul(s.substr(pos, 4), nullptr, 16);
                        pos += 4;
                        if (cp >= 0xD800 && cp <= 0xDBFF && pos + 6 <= s.size() &&
                            s[pos] == '\\' && s[pos+1] == 'u') {
                            uint32_t lo = std::stoul(s.substr(pos + 2, 4), nullptr, 16);
                            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                                pos += 6;
                            }
                        }
                        utf8_encode_append(out, cp);
                        break;
                    }
                    default: err("unbekanntes Escape");
                }
            } else {
                out.push_back(static_cast<char>(c));
                pos++;
            }
        }
        return out;
    }

    Json parse_number() {
        size_t start = pos;
        bool is_double = false;
        if (pos < s.size() && (s[pos] == '-' || s[pos] == '+')) pos++;
        while (pos < s.size() && isdigit(static_cast<unsigned char>(s[pos]))) pos++;
        if (pos < s.size() && s[pos] == '.') { is_double = true; pos++; while (pos < s.size() && isdigit(static_cast<unsigned char>(s[pos]))) pos++; }
        if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
            is_double = true; pos++;
            if (pos < s.size() && (s[pos] == '-' || s[pos] == '+')) pos++;
            while (pos < s.size() && isdigit(static_cast<unsigned char>(s[pos]))) pos++;
        }
        std::string tok = s.substr(start, pos - start);
        if (tok.empty()) err("ungueltige Zahl");
        if (is_double) return Json::real(std::stod(tok));
        return Json::integer(std::stoll(tok));
    }

    Json parse_array() {
        Json j = Json::array();
        pos++; // '['
        skip_ws();
        if (pos < s.size() && s[pos] == ']') { pos++; return j; }
        while (true) {
            j.push_back(parse_value());
            skip_ws();
            if (pos >= s.size()) err("unerwartetes Ende in Array");
            if (s[pos] == ',') { pos++; continue; }
            if (s[pos] == ']') { pos++; break; }
            err("',' oder ']' erwartet");
        }
        return j;
    }

    Json parse_object() {
        Json j = Json::object();
        pos++; // '{'
        skip_ws();
        if (pos < s.size() && s[pos] == '}') { pos++; return j; }
        while (true) {
            skip_ws();
            std::string key = parse_string();
            skip_ws();
            if (pos >= s.size() || s[pos] != ':') err("':' erwartet");
            pos++;
            Json val = parse_value();
            j.set(key, std::move(val));
            skip_ws();
            if (pos >= s.size()) err("unerwartetes Ende in Objekt");
            if (s[pos] == ',') { pos++; continue; }
            if (s[pos] == '}') { pos++; break; }
            err("',' oder '}' erwartet");
        }
        return j;
    }
};

} // namespace

Json Json::parse(const std::string& text) {
    Parser p(text);
    Json result = p.parse_value();
    p.skip_ws();
    return result;
}
