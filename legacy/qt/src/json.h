#pragma once
#include <string>
#include <vector>
#include <memory>
#include <variant>
#include <cstdint>

// Minimaler JSON-Werttyp, der Pythons json.dump(obj) Default-Ausgabe
// byte-identisch nachbildet: separators=(", ", ": "), ensure_ascii=True,
// Einfuegereihenfolge der Objekt-Keys bleibt erhalten.
class Json {
public:
    enum class Type { Null, Bool, Int, Double, String, Array, Object };

    Json() : type_(Type::Null) {}
    static Json null() { return Json(); }
    static Json boolean(bool b) { Json j; j.type_ = Type::Bool; j.b_ = b; return j; }
    static Json integer(int64_t i) { Json j; j.type_ = Type::Int; j.i_ = i; return j; }
    static Json real(double d) { Json j; j.type_ = Type::Double; j.d_ = d; return j; }
    static Json string(std::string s) { Json j; j.type_ = Type::String; j.s_ = std::move(s); return j; }
    static Json array() { Json j; j.type_ = Type::Array; return j; }
    static Json object() { Json j; j.type_ = Type::Object; return j; }

    Type type() const { return type_; }

    // Array
    void push_back(Json v) { arr_.push_back(std::move(v)); }
    const std::vector<Json>& items() const { return arr_; }

    // Object (Reihenfolge = Einfuegereihenfolge,
    void set(const std::string& key, Json v);
    const Json* find(const std::string& key) const;
    const std::vector<std::pair<std::string, Json>>& entries() const { return obj_; }

    bool is_null() const { return type_ == Type::Null; }
    int64_t as_int() const { return i_; }
    double as_double() const { return type_ == Type::Int ? static_cast<double>(i_) : d_; }
    const std::string& as_string() const { return s_; }
    bool as_bool() const { return b_; }

    // Serialisiert
    // dump() = einzeilig (wie json.dump default). dump(indent) = mehrzeilig
    // mit indent Leerzeichen pro Ebene).
    std::string dump() const;
    std::string dump(int indent) const;

    // Parst einen JSON-Text. Wirft std::runtime_error bei Syntaxfehlern.
    static Json parse(const std::string& text);

private:
    Type type_;
    bool b_ = false;
    int64_t i_ = 0;
    double d_ = 0;
    std::string s_;
    std::vector<Json> arr_;
    std::vector<std::pair<std::string, Json>> obj_;

    void dump_to(std::string& out) const;
    void dump_to_indent(std::string& out, int indent, int level) const;
};
