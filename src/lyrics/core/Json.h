// LyricsFlow port - minimal JSON reader (no dependencies, so the lyrics core and its unit tests build anywhere).
// Original code for Bedrock Forge. Reads RFC 8259 JSON into a small DOM; \uXXXX escapes (incl. surrogate pairs)
// become UTF-8. Numbers are kept as double. Not a writer.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bf::lyrics::json {

class Value {
public:
    enum class Type : uint8_t { Null, Bool, Number, String, Array, Object };

    Value() = default;
    static Value makeBool(bool b);
    static Value makeNumber(double d);
    static Value makeString(std::string s);
    static Value makeArray();
    static Value makeObject();

    Type type() const { return mType; }
    bool isNull() const { return mType == Type::Null; }
    bool isBool() const { return mType == Type::Bool; }
    bool isNumber() const { return mType == Type::Number; }
    bool isString() const { return mType == Type::String; }
    bool isArray() const { return mType == Type::Array; }
    bool isObject() const { return mType == Type::Object; }

    bool               asBool(bool def = false) const;
    double             asNumber(double def = 0.0) const; // numbers, and numeric strings ("123")
    int64_t            asInt(int64_t def = 0) const;
    std::string const& asString() const; // empty for non-strings
    /// Java-ish getAsString(): strings as-is, numbers formatted without a trailing ".0", bools "true"/"false".
    std::string        str() const;

    // arrays
    size_t              size() const;
    Value const&        operator[](size_t i) const;
    std::vector<Value>& items();
    std::vector<Value> const& items() const;
    void                push(Value v);

    // objects (insertion order is not preserved; LyricsFlow never relies on it)
    bool         has(std::string_view key) const;
    /// has(key) and not null
    bool         hasValue(std::string_view key) const;
    Value const& operator[](std::string_view key) const; // Null value when missing
    void         set(std::string key, Value v);
    std::map<std::string, Value, std::less<>> const& members() const;

private:
    Type                                                 mType = Type::Null;
    bool                                                 mBool = false;
    double                                               mNum  = 0.0;
    std::string                                          mStr;
    std::shared_ptr<std::vector<Value>>                  mArr;
    std::shared_ptr<std::map<std::string, Value, std::less<>>> mObj;
};

/// Parse a whole document. nullopt on any syntax error (callers treat that like a failed request).
std::optional<Value> parse(std::string_view text);

} // namespace bf::lyrics::json
