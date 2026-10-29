// LyricsFlow port - minimal JSON reader. Original code for Bedrock Forge.
#include "lyrics/core/Json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace bf::lyrics::json {

namespace {
Value const&       nullValue() { static Value v; return v; }
std::string const& emptyString() { static std::string s; return s; }

void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) out += char(cp);
    else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
    else { out += char(0xF0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 0x3F)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
}

struct Parser {
    std::string_view s;
    size_t           i = 0;
    int              depth = 0;

    void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i; }
    bool lit(std::string_view w) {
        if (s.substr(i, w.size()) != w) return false;
        i += w.size();
        return true;
    }
    bool hex4(uint32_t& v) {
        if (i + 4 > s.size()) return false;
        v = 0;
        for (int k = 0; k < 4; ++k) {
            char c = s[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= uint32_t(c - '0');
            else if (c >= 'a' && c <= 'f') v |= uint32_t(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= uint32_t(c - 'A' + 10);
            else return false;
        }
        return true;
    }
    bool string(std::string& out) {
        if (i >= s.size() || s[i] != '"') return false;
        ++i;
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) return false;
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
                uint32_t cp;
                if (!hex4(cp)) return false;
                if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                    size_t   save = i;
                    uint32_t lo;
                    i += 2;
                    if (hex4(lo) && lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    else i = save;
                }
                appendUtf8(out, cp);
                break;
            }
            default: return false;
            }
        }
        return false;
    }
    bool number(double& out) {
        size_t b = i;
        if (i < s.size() && s[i] == '-') ++i;
        while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' || s[i] == 'e' || s[i] == 'E' || s[i] == '+' || s[i] == '-')) ++i;
        if (i == b) return false;
        std::string tmp(s.substr(b, i - b));
        char*       end = nullptr;
        out             = std::strtod(tmp.c_str(), &end);
        return end && *end == '\0';
    }
    bool value(Value& out) {
        if (++depth > 256) return false;
        ws();
        if (i >= s.size()) return false;
        char c = s[i];
        bool ok = true;
        if (c == '{') {
            ++i;
            out = Value::makeObject();
            ws();
            if (i < s.size() && s[i] == '}') { ++i; --depth; return true; }
            for (;;) {
                ws();
                std::string k;
                if (!string(k)) return false;
                ws();
                if (i >= s.size() || s[i] != ':') return false;
                ++i;
                Value v;
                if (!value(v)) return false;
                out.set(std::move(k), std::move(v));
                ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; break; }
                return false;
            }
        } else if (c == '[') {
            ++i;
            out = Value::makeArray();
            ws();
            if (i < s.size() && s[i] == ']') { ++i; --depth; return true; }
            for (;;) {
                Value v;
                if (!value(v)) return false;
                out.push(std::move(v));
                ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; break; }
                return false;
            }
        } else if (c == '"') {
            std::string str;
            ok  = string(str);
            out = Value::makeString(std::move(str));
        } else if (lit("true")) out = Value::makeBool(true);
        else if (lit("false")) out = Value::makeBool(false);
        else if (lit("null")) out = Value();
        else {
            double d = 0;
            ok       = number(d);
            out      = Value::makeNumber(d);
        }
        --depth;
        return ok;
    }
};
} // namespace

Value Value::makeBool(bool b) { Value v; v.mType = Type::Bool; v.mBool = b; return v; }
Value Value::makeNumber(double d) { Value v; v.mType = Type::Number; v.mNum = d; return v; }
Value Value::makeString(std::string s) { Value v; v.mType = Type::String; v.mStr = std::move(s); return v; }
Value Value::makeArray() { Value v; v.mType = Type::Array; v.mArr = std::make_shared<std::vector<Value>>(); return v; }
Value Value::makeObject() { Value v; v.mType = Type::Object; v.mObj = std::make_shared<std::map<std::string, Value, std::less<>>>(); return v; }

bool Value::asBool(bool def) const {
    if (mType == Type::Bool) return mBool;
    if (mType == Type::Number) return mNum != 0.0;
    return def;
}
double Value::asNumber(double def) const {
    if (mType == Type::Number) return mNum;
    if (mType == Type::String && !mStr.empty()) {
        char*  end = nullptr;
        double d   = std::strtod(mStr.c_str(), &end);
        if (end && *end == '\0') return d;
    }
    if (mType == Type::Bool) return mBool ? 1.0 : 0.0;
    return def;
}
int64_t Value::asInt(int64_t def) const {
    if (mType == Type::String && !mStr.empty()) {
        char*     end = nullptr;
        long long v   = std::strtoll(mStr.c_str(), &end, 10);
        if (end && *end == '\0') return v;
    }
    double d = asNumber(std::nan(""));
    return std::isnan(d) ? def : int64_t(std::llround(d));
}
std::string const& Value::asString() const { return mType == Type::String ? mStr : emptyString(); }
std::string Value::str() const {
    switch (mType) {
    case Type::String: return mStr;
    case Type::Bool: return mBool ? "true" : "false";
    case Type::Number: {
        if (std::fabs(mNum) < 9.0e15 && mNum == std::floor(mNum)) return std::to_string(int64_t(mNum));
        char b[64];
        std::snprintf(b, sizeof b, "%.17g", mNum);
        return b;
    }
    default: return {};
    }
}
size_t Value::size() const {
    if (mType == Type::Array) return mArr->size();
    if (mType == Type::Object) return mObj->size();
    return 0;
}
Value const& Value::operator[](size_t i) const { return (mType == Type::Array && i < mArr->size()) ? (*mArr)[i] : nullValue(); }
std::vector<Value>& Value::items() {
    if (mType != Type::Array) *this = makeArray();
    return *mArr;
}
std::vector<Value> const& Value::items() const {
    static std::vector<Value> const empty;
    return mType == Type::Array ? *mArr : empty;
}
void Value::push(Value v) { items().push_back(std::move(v)); }
bool Value::has(std::string_view key) const { return mType == Type::Object && mObj->find(key) != mObj->end(); }
bool Value::hasValue(std::string_view key) const { return has(key) && !(*this)[key].isNull(); }
Value const& Value::operator[](std::string_view key) const {
    if (mType != Type::Object) return nullValue();
    auto it = mObj->find(key);
    return it == mObj->end() ? nullValue() : it->second;
}
void Value::set(std::string key, Value v) {
    if (mType != Type::Object) *this = makeObject();
    (*mObj)[std::move(key)] = std::move(v);
}
std::map<std::string, Value, std::less<>> const& Value::members() const {
    static std::map<std::string, Value, std::less<>> const empty;
    return mType == Type::Object ? *mObj : empty;
}

std::optional<Value> parse(std::string_view text) {
    Parser p{text};
    // tolerate a UTF-8 BOM
    if (p.s.size() >= 3 && uint8_t(p.s[0]) == 0xEF && uint8_t(p.s[1]) == 0xBB && uint8_t(p.s[2]) == 0xBF) p.i = 3;
    Value v;
    if (!p.value(v)) return std::nullopt;
    p.ws();
    if (p.i != p.s.size()) return std::nullopt;
    return v;
}

} // namespace bf::lyrics::json
