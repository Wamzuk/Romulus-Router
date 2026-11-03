// json.hpp - a tiny, dependency-free JSON parser and writer.
//
// This is intentionally small: it supports exactly what the Romulus Router
// server needs (objects, arrays, strings, numbers, booleans, null) and nothing
// more. It is not a general-purpose JSON library, but it is correct for the
// GeoJSON and API payloads this project uses.
#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <stdexcept>
#include <sstream>
#include <cmath>
#include <cstdint>

namespace json {

enum class Type { Null, Bool, Number, String, Array, Object };

class Value;
using Array = std::vector<Value>;
using Object = std::vector<std::pair<std::string, Value>>; // preserves insertion order

class Value {
public:
    Type type = Type::Null;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::shared_ptr<Array> arr;
    std::shared_ptr<Object> obj;

    Value() : type(Type::Null) {}
    Value(std::nullptr_t) : type(Type::Null) {}
    Value(bool v) : type(Type::Bool), b(v) {}
    Value(double v) : type(Type::Number), num(v) {}
    Value(int v) : type(Type::Number), num(v) {}
    Value(long v) : type(Type::Number), num(static_cast<double>(v)) {}
    Value(size_t v) : type(Type::Number), num(static_cast<double>(v)) {}
    Value(const char* v) : type(Type::String), str(v) {}
    Value(const std::string& v) : type(Type::String), str(v) {}

    static Value makeArray() { Value v; v.type = Type::Array; v.arr = std::make_shared<Array>(); return v; }
    static Value makeObject() { Value v; v.type = Type::Object; v.obj = std::make_shared<Object>(); return v; }

    bool isNull() const { return type == Type::Null; }
    bool isObject() const { return type == Type::Object; }
    bool isArray() const { return type == Type::Array; }
    bool isString() const { return type == Type::String; }
    bool isNumber() const { return type == Type::Number; }

    // --- object helpers ---
    const Value* find(const std::string& key) const {
        if (type != Type::Object || !obj) return nullptr;
        for (auto& kv : *obj) if (kv.first == key) return &kv.second;
        return nullptr;
    }
    void set(const std::string& key, Value v) {
        if (type != Type::Object) { type = Type::Object; obj = std::make_shared<Object>(); }
        for (auto& kv : *obj) if (kv.first == key) { kv.second = std::move(v); return; }
        obj->emplace_back(key, std::move(v));
    }

    // --- array helpers ---
    void push_back(Value v) {
        if (type != Type::Array) { type = Type::Array; arr = std::make_shared<Array>(); }
        arr->push_back(std::move(v));
    }

    // --- typed accessors (safe defaults) ---
    std::string asString(const std::string& def = "") const { return type == Type::String ? str : def; }
    double asNumber(double def = 0.0) const { return type == Type::Number ? num : def; }
    bool asBool(bool def = false) const { return type == Type::Bool ? b : def; }
};

// ------------------------- Parser -------------------------
class Parser {
public:
    explicit Parser(const std::string& text) : s(text), i(0), n(text.size()) {}

    Value parse() {
        skipWs();
        Value v = parseValue();
        skipWs();
        // Allow trailing whitespace only.
        if (i != n) throw std::runtime_error("Trailing characters after JSON value");
        return v;
    }

private:
    const std::string& s;
    size_t i, n;

    [[noreturn]] void fail(const std::string& msg) {
        throw std::runtime_error("JSON parse error at byte " + std::to_string(i) + ": " + msg);
    }

    void skipWs() {
        while (i < n) {
            char c = s[i];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') i++;
            else break;
        }
    }

    char peek() { return i < n ? s[i] : '\0'; }

    Value parseValue() {
        skipWs();
        if (i >= n) fail("Unexpected end of input");
        char c = s[i];
        switch (c) {
            case '{': return parseObject();
            case '[': return parseArray();
            case '"': { Value v; v.type = Type::String; v.str = parseString(); return v; }
            case 't': case 'f': return parseBool();
            case 'n': return parseNull();
            default:
                if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
                fail(std::string("Unexpected character '") + c + "'");
        }
    }

    Value parseObject() {
        Value v = Value::makeObject();
        i++; // {
        skipWs();
        if (peek() == '}') { i++; return v; }
        while (true) {
            skipWs();
            if (peek() != '"') fail("Expected string key in object");
            std::string key = parseString();
            skipWs();
            if (peek() != ':') fail("Expected ':' after object key");
            i++;
            Value val = parseValue();
            v.obj->emplace_back(std::move(key), std::move(val));
            skipWs();
            char c = peek();
            if (c == ',') { i++; continue; }
            if (c == '}') { i++; break; }
            fail("Expected ',' or '}' in object");
        }
        return v;
    }

    Value parseArray() {
        Value v = Value::makeArray();
        i++; // [
        skipWs();
        if (peek() == ']') { i++; return v; }
        while (true) {
            Value val = parseValue();
            v.arr->push_back(std::move(val));
            skipWs();
            char c = peek();
            if (c == ',') { i++; continue; }
            if (c == ']') { i++; break; }
            fail("Expected ',' or ']' in array");
        }
        return v;
    }

    std::string parseString() {
        std::string out;
        i++; // opening quote
        while (i < n) {
            char c = s[i++];
            if (c == '"') return out;
            if (c == '\\') {
                if (i >= n) fail("Unterminated escape");
                char e = s[i++];
                switch (e) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u': {
                        if (i + 4 > n) fail("Bad \\u escape");
                        unsigned code = hex4();
                        // Handle surrogate pairs.
                        if (code >= 0xD800 && code <= 0xDBFF) {
                            if (i + 6 <= n && s[i] == '\\' && s[i + 1] == 'u') {
                                i += 2;
                                unsigned lo = hex4();
                                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                    code = 0x10000 + ((code - 0xD800) << 10) + (lo - 0xDC00);
                                }
                            }
                        }
                        appendUtf8(out, code);
                        break;
                    }
                    default: fail("Invalid escape character");
                }
            } else {
                out += c;
            }
        }
        fail("Unterminated string");
    }

    unsigned hex4() {
        unsigned v = 0;
        for (int k = 0; k < 4; k++) {
            char c = s[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (c - '0');
            else if (c >= 'a' && c <= 'f') v |= (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (c - 'A' + 10);
            else fail("Invalid hex digit in \\u escape");
        }
        return v;
    }

    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp <= 0x7F) out += static_cast<char>(cp);
        else if (cp <= 0x7FF) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp <= 0xFFFF) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    Value parseNumber() {
        size_t start = i;
        if (peek() == '-') i++;
        while (i < n && s[i] >= '0' && s[i] <= '9') i++;
        if (peek() == '.') { i++; while (i < n && s[i] >= '0' && s[i] <= '9') i++; }
        if (peek() == 'e' || peek() == 'E') {
            i++;
            if (peek() == '+' || peek() == '-') i++;
            while (i < n && s[i] >= '0' && s[i] <= '9') i++;
        }
        Value v;
        v.type = Type::Number;
        v.num = std::strtod(s.c_str() + start, nullptr);
        return v;
    }

    Value parseBool() {
        if (s.compare(i, 4, "true") == 0) { i += 4; return Value(true); }
        if (s.compare(i, 5, "false") == 0) { i += 5; return Value(false); }
        fail("Invalid literal");
    }

    Value parseNull() {
        if (s.compare(i, 4, "null") == 0) { i += 4; return Value(nullptr); }
        fail("Invalid literal");
    }
};

inline Value parse(const std::string& text) {
    Parser p(text);
    return p.parse();
}

// ------------------------- Writer -------------------------
inline void writeString(std::string& out, const std::string& s) {
    out += '"';
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
}

inline void writeNumber(std::string& out, double v) {
    if (std::isnan(v) || std::isinf(v)) { out += "null"; return; }
    // Integers print cleanly; fractions get enough precision for coordinates.
    if (v == std::floor(v) && std::fabs(v) < 1e15) {
        out += std::to_string(static_cast<long long>(v));
    } else {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.10g", v);
        out += buf;
    }
}

inline void write(std::string& out, const Value& v) {
    switch (v.type) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += v.b ? "true" : "false"; break;
        case Type::Number: writeNumber(out, v.num); break;
        case Type::String: writeString(out, v.str); break;
        case Type::Array: {
            out += '[';
            if (v.arr) {
                for (size_t k = 0; k < v.arr->size(); k++) {
                    if (k) out += ',';
                    write(out, (*v.arr)[k]);
                }
            }
            out += ']';
            break;
        }
        case Type::Object: {
            out += '{';
            if (v.obj) {
                for (size_t k = 0; k < v.obj->size(); k++) {
                    if (k) out += ',';
                    writeString(out, (*v.obj)[k].first);
                    out += ':';
                    write(out, (*v.obj)[k].second);
                }
            }
            out += '}';
            break;
        }
    }
}

inline std::string dump(const Value& v) {
    std::string out;
    write(out, v);
    return out;
}

} // namespace json
