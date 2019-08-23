#ifndef TINYTOML_H_
#define TINYTOML_H_

#include <algorithm>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <istream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace toml {

// ----------------------------------------------------------------------
// Declarations

class Value;
typedef std::chrono::system_clock::time_point Time;
typedef std::vector<Value> Array;
typedef std::map<std::string, Value> Table;

namespace internal {
template<typename T> struct call_traits_value {
    typedef T return_type;
};
template<typename T> struct call_traits_ref {
    typedef const T& return_type;
};
} // namespace internal

template<typename T> struct call_traits;
template<> struct call_traits<bool> : public internal::call_traits_value<bool> {};
template<> struct call_traits<int> : public internal::call_traits_value<int> {};
template<> struct call_traits<int64_t> : public internal::call_traits_value<int64_t> {};
template<> struct call_traits<double> : public internal::call_traits_value<double> {};
template<> struct call_traits<std::string> : public internal::call_traits_ref<std::string> {};
template<> struct call_traits<Time> : public internal::call_traits_ref<Time> {};
template<> struct call_traits<Array> : public internal::call_traits_ref<Array> {};
template<> struct call_traits<Table> : public internal::call_traits_ref<Table> {};

// A value is returned for std::vector<T>. Not reference.
// This is because a fresh vector is made.
template<typename T> struct call_traits<std::vector<T>> : public internal::call_traits_value<std::vector<T>> {};

// Formatting flags
enum FormatFlag {
    FORMAT_NONE = 0,
    FORMAT_INDENT = 1
};

class Value {
public:
    enum Type {
        NULL_TYPE,
        BOOL_TYPE,
        INT_TYPE,
        DOUBLE_TYPE,
        STRING_TYPE,
        TIME_TYPE,
        ARRAY_TYPE,
        TABLE_TYPE,
    };

    Value() : type_(NULL_TYPE), null_(nullptr), lineNo(-1) {}
    Value(bool v, int line) : type_(BOOL_TYPE), bool_(v), lineNo(line) {}
    Value(int v, int line) : type_(INT_TYPE), int_(v), lineNo(line) {}
    Value(int64_t v, int line) : type_(INT_TYPE), int_(v), lineNo(line) {}
    Value(double v, int line) : type_(DOUBLE_TYPE), double_(v), lineNo(line) {}
    Value(const std::string& v, int line) : type_(STRING_TYPE), string_(new std::string(v)), lineNo(line) {}
    Value(const char* v, int line) : type_(STRING_TYPE), string_(new std::string(v)), lineNo(line) {}
    Value(const Time& v, int line) : type_(TIME_TYPE), time_(new Time(v)), lineNo(line) {}
    Value(const Array& v, int line) : type_(ARRAY_TYPE), array_(new Array(v)), lineNo(line) {}
    Value(const Table& v, int line) : type_(TABLE_TYPE), table_(new Table(v)), lineNo(line) {}
    Value(std::string&& v, int line) : type_(STRING_TYPE), string_(new std::string(std::move(v))), lineNo(line) {}
    Value(Array&& v, int line) : type_(ARRAY_TYPE), array_(new Array(std::move(v))), lineNo(line) {}
    Value(Table&& v, int line) : type_(TABLE_TYPE), table_(new Table(std::move(v))), lineNo(line) {}

    Value(const Value& v);
    Value(Value&& v) noexcept;
    Value& operator=(const Value& v);
    Value& operator=(Value&& v) noexcept;

    // Guards from unexpected Value construction.
    // Someone might use a value like this:
    //   toml::Value v = x->find("foo");
    // But this is wrong. Without this constructor,
    // value will be unexpectedly initialized with bool.
    Value(const void* v) = delete;
    ~Value();

    // Retruns Value size.
    // 0 for invalid value.
    // The number of inner elements for array or table.
    // 1 for other types.
    size_t size() const;
    bool empty() const;
    Type type() const { return type_; }

    bool valid() const { return type_ != NULL_TYPE; }
    template<typename T> bool is() const;
    template<typename T> typename call_traits<T>::return_type as() const;

    friend bool operator==(const Value& lhs, const Value& rhs);
    friend bool operator!=(const Value& lhs, const Value& rhs) { return !(lhs == rhs); }

    // ----------------------------------------------------------------------
    // For integer/floating value

    // Returns true if the value is int or double.
    bool isNumber() const;
    // Returns number. Convert to double.
    double asNumber() const;

    // ----------------------------------------------------------------------
    // For Time value

    // Converts to time_t if the internal value is Time.
    // We don't have as<std::time_t>(). Since time_t is basically signed long,
    // it's something like a method to converting to (normal) integer.
    std::time_t as_time_t() const;

    // ----------------------------------------------------------------------
    // For Table value
    template<typename T> typename call_traits<T>::return_type get(const std::string&) const;
    Value* set(const std::string& key, const Value& v);
    // Finds a Value with |key|. |key| can contain '.'
    // Note: if you would like to find a child value only, you need to use findChild.
    const Value* find(const std::string& key) const;
    Value* find(const std::string& key);
    bool has(const std::string& key) const { return find(key) != nullptr; }
    bool erase(const std::string& key);

    Value& operator[](const std::string& key);

    // Merge table. Returns true if succeeded. Otherwise, |this| might be corrupted.
    // When the same key exists, it will be overwritten.
    bool merge(const Value&);

    // Finds a value with |key|. It searches only children.
    Value* findChild(const std::string& key);
    const Value* findChild(const std::string& key) const;
    // Sets a value, and returns the pointer to the created value.
    // When the value having the same key exists, it will be overwritten.
    Value* setChild(const std::string& key, const Value& v);
    Value* setChild(const std::string& key, Value&& v);
    bool eraseChild(const std::string& key);

    // ----------------------------------------------------------------------
    // For Array value

    template<typename T> typename call_traits<T>::return_type get(size_t index) const;
    const Value* find(size_t index) const;
    Value* find(size_t index);
    Value* push(const Value& v);
    Value* push(Value&& v);

    // ----------------------------------------------------------------------
    // Others

    // Writer.
    static std::string spaces(int num);
    static std::string escapeKey(const std::string& key);

    void write(std::ostream*, const std::string& keyPrefix = std::string(), int indent = -1) const;
    void writeFormatted(std::ostream*, FormatFlag flags) const;

    friend std::ostream& operator<<(std::ostream&, const Value&);

	int lineNo;
private:
    static const char* typeToString(Type);

    template<typename T> void assureType() const;
    Value* ensureValue(const std::string& key);

    template<typename T> struct ValueConverter;

    Type type_;
    union {
        void* null_;
        bool bool_;
        int64_t int_;
        double double_;
        std::string* string_;
        Time* time_;
        Array* array_;
        Table* table_;
    };	

    template<typename T> friend struct ValueConverter;
};

// parse() returns ParseResult.
struct ParseResult {
    ParseResult(toml::Value v, std::string er) :
        value(std::move(v)),
        errorReason(std::move(er)) {}

    bool valid() const { return value.valid(); }

    toml::Value value;
    std::string errorReason;
};

// Parses from std::istream.
ParseResult parse(std::istream&);
// Parses a file.
ParseResult parseFile(const std::string& filename);

// ----------------------------------------------------------------------
// Declarations for Implementations
//   You don't need to understand the below to use this library.

#if defined(_WIN32)
// Windows does not have timegm but have _mkgmtime.
inline time_t timegm(std::tm* timeptr)
{
    return _mkgmtime(timeptr);
}

// On Windows, Visual Studio does not define gmtime_r. However, mingw might
// do (or might not do). See https://github.com/mayah/tinytoml/issues/25,
#ifndef gmtime_r
inline struct tm* gmtime_r(const time_t* t, struct tm* r)
{
    // gmtime is threadsafe in windows because it uses TLS
    struct tm *theTm = gmtime(t);
    if (theTm) {
        *r = *theTm;
        return r;
    } else {
        return 0;
    }
}
#endif  // gmtime_r
#endif  // _WIN32

namespace internal {

enum class TokenType {
    ERROR_TOKEN,
    END_OF_FILE,
    END_OF_LINE,
    IDENT,
    STRING,
    MULTILINE_STRING,
    BOOL,
    INT,
    DOUBLE,
    TIME,
    COMMA,
    DOT,
    EQUAL,
    LBRACKET,
    RBRACKET,
    LBRACE,
    RBRACE,
};

class Token {
public:
    explicit Token(TokenType type) : type_(type) {}
    Token(TokenType type, const std::string& v) : type_(type), str_value_(v) {}
    Token(TokenType type, bool v) : type_(type), int_value_(v) {}
    Token(TokenType type, std::int64_t v) : type_(type), int_value_(v) {}
    Token(TokenType type, double v) : type_(type), double_value_(v) {}
    Token(TokenType type, std::chrono::system_clock::time_point tp) : type_(type), time_value_(tp) {}

    TokenType type() const { return type_; }
    const std::string& strValue() const { return str_value_; }
    bool boolValue() const { return int_value_ != 0; }
    std::int64_t intValue() const { return int_value_; }
    double doubleValue() const { return double_value_; }
    std::chrono::system_clock::time_point timeValue() const { return time_value_; }

private:
    TokenType type_;
    std::string str_value_;
    std::int64_t int_value_;
    double double_value_;
    std::chrono::system_clock::time_point time_value_;
};

class Lexer {
public:
    explicit Lexer(std::istream& is) : is_(is), lineNo_(1) {}

    Token nextKeyToken();
    Token nextValueToken();

    int lineNo() const { return lineNo_; }

    // Skips if UTF8BOM is found.
    // Returns true if success. Returns false if intermediate state is left.
    bool skipUTF8BOM();

private:
    bool current(char* c);
    void next();
    bool consume(char c);

    Token nextToken(bool isValueToken);

    void skipUntilNewLine();

    Token nextStringDoubleQuote();
    Token nextStringSingleQuote();

    Token nextKey();
    Token nextValue();

    Token parseAsTime(const std::string&);

    std::istream& is_;
    int lineNo_;
};

class Parser {
public:
    explicit Parser(std::istream& is) : lexer_(is), token_(TokenType::ERROR_TOKEN)
    {
        if (!lexer_.skipUTF8BOM()) {
            token_ = Token(TokenType::ERROR_TOKEN, std::string("Invalid UTF8 BOM"));
        } else {
            nextKey();
        }
    }

    // Parses. If failed, value should be invalid value.
    // You can get the error by calling errorReason().
    Value parse();
    const std::string& errorReason();

private:
    const Token& token() const { return token_; }
    void nextKey() { token_ = lexer_.nextKeyToken(); }
    void nextValue() { token_ = lexer_.nextValueToken(); }

    void skipForKey();
    void skipForValue();

    bool consumeForKey(TokenType);
    bool consumeForValue(TokenType);
    bool consumeEOLorEOFForKey();

    Value* parseGroupKey(Value* root);

    bool parseKeyValue(Value*);
    bool parseKey(std::string*);
    bool parseValue(Value*);
    bool parseBool(Value*);
    bool parseNumber(Value*);
    bool parseArray(Value*);
    bool parseInlineTable(Value*);

    void addError(const std::string& reason);

    Lexer lexer_;
    Token token_;
    std::string errorReason_;
};

} // namespace internal

// ----------------------------------------------------------------------
// Implementations

inline ParseResult parse(std::istream& is)
{
    if (!is) {
        return ParseResult(toml::Value(), "stream is in bad state. file does not exist?");
    }

    internal::Parser parser(is);
    toml::Value v = parser.parse();

    if (v.valid())
        return ParseResult(std::move(v), std::string());

    return ParseResult(std::move(v), std::move(parser.errorReason()));
}

inline ParseResult parseFile(const std::string& filename)
{
    std::ifstream ifs(filename);
    if (!ifs) {
        return ParseResult(toml::Value(),
                           std::string("could not open file: ") + filename);
    }

    return parse(ifs);
}

inline std::string format(std::stringstream& ss)
{
    return ss.str();
}

template<typename T, typename... Args>
std::string format(std::stringstream& ss, T&& t, Args&&... args)
{
    ss << std::forward<T>(t);
    return format(ss, std::forward<Args>(args)...);
}

// If you want to compile without exception,
//   1. Define TOML_HAVE_FAILWITH_REPLACEMENT
//   2. Define your own toml::failwith.
// e.g. You can just abort here instead of exception.
#ifndef TOML_HAVE_FAILWITH_REPLACEMENT

template<typename... Args>
#if defined(_MSC_VER)
__declspec(noreturn)
#else
[[noreturn]]
#endif
void failwith(Args&&... args)
{
    std::stringstream ss;
    throw std::runtime_error(format(ss, std::forward<Args>(args)...));
}

#endif

namespace internal {

inline std::string removeDelimiter(const std::string& s)
{
    std::string r;
    for (char c : s) {
        if (c == '_')
            continue;
        r += c;
    }
    return r;
}

inline std::string unescape(const std::string& codepoint)
{
    std::uint32_t x;
    std::uint8_t buf[8];
    std::stringstream ss(codepoint);

    ss >> std::hex >> x;

    if (x <= 0x7FUL) {
        // 0xxxxxxx
        buf[0] = 0x00 | ((x >> 0) & 0x7F);
        buf[1] = '\0';
    } else if (x <= 0x7FFUL) {
        // 110yyyyx 10xxxxxx
        buf[0] = 0xC0 | ((x >> 6) & 0xDF);
        buf[1] = 0x80 | ((x >> 0) & 0xBF);
        buf[2] = '\0';
    } else if (x <= 0xFFFFUL) {
        // 1110yyyy 10yxxxxx 10xxxxxx
        buf[0] = 0xE0 | ((x >> 12) & 0xEF);
        buf[1] = 0x80 | ((x >> 6) & 0xBF);
        buf[2] = 0x80 | ((x >> 0) & 0xBF);
        buf[3] = '\0';
    } else if (x <= 0x10FFFFUL) {
        // 11110yyy 10yyxxxx 10xxxxxx 10xxxxxx
        buf[0] = 0xF0 | ((x >> 18) & 0xF7);
        buf[1] = 0x80 | ((x >> 12) & 0xBF);
        buf[2] = 0x80 | ((x >> 6) & 0xBF);
        buf[3] = 0x80 | ((x >> 0) & 0xBF);
        buf[4] = '\0';
    } else {
        buf[0] = '\0';
    }

    return reinterpret_cast<char*>(buf);
}

// Returns true if |s| is integer.
// [+-]?\d+(_\d+)*
inline bool isInteger(const std::string& s)
{
    if (s.empty())
        return false;

    std::string::size_type p = 0;
    if (s[p] == '+' || s[p] == '-')
        ++p;

    while (p < s.size() && '0' <= s[p] && s[p] <= '9') {
        ++p;
        if (p < s.size() && s[p] == '_') {
            ++p;
            if (!(p < s.size() && '0' <= s[p] && s[p] <= '9'))
                return false;
        }
    }

    return p == s.size();
}

// Returns true if |s| is double.
// [+-]? (\d+(_\d+)*)? (\.\d+(_\d+)*)? ([eE] [+-]? \d+(_\d+)*)?
//       1-----------  2-------------  3----------------------
// 2 or (1 and 3) should exist.
inline bool isDouble(const std::string& s)
{
    if (s.empty())
        return false;

    std::string::size_type p = 0;
    if (s[p] == '+' || s[p] == '-')
        ++p;

    bool ok = false;
    while (p < s.size() && '0' <= s[p] && s[p] <= '9') {
        ++p;
        ok = true;

        if (p < s.size() && s[p] == '_') {
            ++p;
            if (!(p < s.size() && '0' <= s[p] && s[p] <= '9'))
                return false;
        }
    }

    if (p < s.size() && s[p] == '.')
        ++p;

    while (p < s.size() && '0' <= s[p] && s[p] <= '9') {
        ++p;
        ok = true;

        if (p < s.size() && s[p] == '_') {
            ++p;
            if (!(p < s.size() && '0' <= s[p] && s[p] <= '9'))
                return false;
        }
    }

    if (!ok)
        return false;

    ok = false;
    if (p < s.size() && (s[p] == 'e' || s[p] == 'E')) {
        ++p;
        if (p < s.size() && (s[p] == '+' || s[p] == '-'))
            ++p;
        while (p < s.size() && '0' <= s[p] && s[p] <= '9') {
            ++p;
            ok = true;

            if (p < s.size() && s[p] == '_') {
                ++p;
                if (!(p < s.size() && '0' <= s[p] && s[p] <= '9'))
                    return false;
            }
        }
        if (!ok)
            return false;
    }

    return p == s.size();
}

// static
inline std::string escapeString(const std::string& s)
{
    std::stringstream ss;
    for (size_t i = 0; i < s.size(); ++i) {
        switch (s[i]) {
        case '\n': ss << "\\n"; break;
        case '\r': ss << "\\r"; break;
        case '\t': ss << "\\t"; break;
        case '\"': ss << "\\\""; break;
        case '\'': ss << "\\\'"; break;
        case '\\': ss << "\\\\"; break;
        default: ss << s[i]; break;
        }
    }

    return ss.str();
}

} // namespace internal

// ----------------------------------------------------------------------
// Lexer

namespace internal {

inline bool Lexer::skipUTF8BOM()
{
    // Check [EF, BB, BF]

    int x1 = is_.peek();
    if (x1 != 0xEF) {
        // When the first byte is not 0xEF, it's not UTF8 BOM.
        // Just return true.
        return true;
    }

    is_.get();
    int x2 = is_.get();
    if (x2 != 0xBB) {
        return false;
    }

    int x3 = is_.get();
    if (x3 != 0xBF) {
        return false;
    }

    return true;
}

inline bool Lexer::current(char* c)
{
    int x = is_.peek();
    if (x == EOF)
        return false;
    *c = static_cast<char>(x);
    return true;
}

inline void Lexer::next()
{
    int x = is_.get();
    if (x == '\n')
        ++lineNo_;
}

inline bool Lexer::consume(char c)
{
    char x;
    if (!current(&x))
        return false;
    if (x != c)
        return false;
    next();
    return true;
}

inline void Lexer::skipUntilNewLine()
{
    char c;
    while (current(&c)) {
        if (c == '\n')
            return;
        next();
    }
}

inline Token Lexer::nextStringDoubleQuote()
{
    if (!consume('"'))
        return Token(TokenType::ERROR_TOKEN, std::string("String didn't start with '\"'"));

    std::string s;
    char c;
    bool multiline = false;

    if (current(&c) && c == '"') {
        next();
        if (!current(&c) || c != '"') {
            // OK. It's empty string.
            return Token(TokenType::STRING, std::string());
        }

        next();
        // raw string literal started.
        // Newline just after """ should be ignored.
        while (current(&c) && (c == ' ' || c == '\t'))
            next();
        if (current(&c) && c == '\n')
            next();
        multiline = true;
    }

    while (current(&c)) {
        next();
        if (c == '\\') {
            if (!current(&c))
                return Token(TokenType::ERROR_TOKEN, std::string("String has unknown escape sequence"));
            next();
            switch (c) {
            case 't': c = '\t'; break;
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 'u':
            case 'U': {
                int size = c == 'u' ? 4 : 8;
                std::string codepoint;
                for (int i = 0; i < size; ++i) {
                  if (current(&c) && (('0' <= c && c <= '9') || ('A' <= c && c <= 'F') || ('a' <= c && c <= 'f'))) {
                    codepoint += c;
                    next();
                  } else {
                    return Token(TokenType::ERROR_TOKEN, std::string("String has unknown escape sequence"));
                  }
                }
                s += unescape(codepoint);
                continue;
            }
            case '"': c = '"'; break;
            case '\'': c = '\''; break;
            case '\\': c = '\\'; break;
            case '\n':
                while (current(&c) && (c == ' ' || c == '\t' || c == '\r' || c == '\n')) {
                    next();
                }
                continue;
            default:
                return Token(TokenType::ERROR_TOKEN, std::string("String has unknown escape sequence"));
            }
        } else if (c == '"') {
            if (multiline) {
                if (current(&c) && c == '"') {
                    next();
                    if (current(&c) && c == '"') {
                        next();
                        return Token(TokenType::MULTILINE_STRING, s);
                    } else {
                        s += '"';
                        s += '"';
                        continue;
                    }
                } else {
                    s += '"';
                    continue;
                }
            } else {
                return Token(TokenType::STRING, s);
            }
        }

        s += c;
    }

    return Token(TokenType::ERROR_TOKEN, std::string("String didn't end"));
}

inline Token Lexer::nextStringSingleQuote()
{
    if (!consume('\''))
        return Token(TokenType::ERROR_TOKEN, std::string("String didn't start with '\''?"));

    std::string s;
    char c;

    if (current(&c) && c == '\'') {
        next();
        if (!current(&c) || c != '\'') {
            // OK. It's empty string.
            return Token(TokenType::STRING, std::string());
        }
        next();
        // raw string literal started.
        // Newline just after """ should be ignored.
        if (current(&c) && c == '\n')
            next();

        while (current(&c)) {
            if (c == '\'') {
                next();
                if (current(&c) && c == '\'') {
                    next();
                    if (current(&c) && c == '\'') {
                        next();
                        return Token(TokenType::MULTILINE_STRING, s);
                    } else {
                        s += '\'';
                        s += '\'';
                        continue;
                    }
                } else {
                    s += '\'';
                    continue;
                }
            }

            next();
            s += c;
            continue;
        }

        return Token(TokenType::ERROR_TOKEN, std::string("String didn't end with '\'\'\'' ?"));
    }

    while (current(&c)) {
        next();
        if (c == '\'') {
            return Token(TokenType::STRING, s);
        }

        s += c;
    }

    return Token(TokenType::ERROR_TOKEN, std::string("String didn't end with '\''?"));
}

inline Token Lexer::nextKey()
{
    std::string s;
    char c;
    while (current(&c) && (isalnum(c) || c == '_' || c == '-')) {
        s += c;
        next();
    }

    if (s.empty())
        return Token(TokenType::ERROR_TOKEN, std::string("Unknown key format"));

    return Token(TokenType::IDENT, s);
}

inline Token Lexer::nextValue()
{
    std::string s;
    char c;

    if (current(&c) && isalpha(c)) {
        s += c;
        next();
        while (current(&c) && isalpha(c)) {
            s += c;
            next();
        }

        if (s == "true")
            return Token(TokenType::BOOL, true);
        if (s == "false")
            return Token(TokenType::BOOL, false);
        return Token(TokenType::ERROR_TOKEN, std::string("Unknown ident: ") + s);
    }

    while (current(&c) && (('0' <= c && c <= '9') || c == '.' || c == 'e' || c == 'E' ||
                           c == 'T' || c == 'Z' || c == '_' || c == ':' || c == '-' || c == '+')) {
        next();
        s += c;
    }

    if (isInteger(s)) {
        std::stringstream ss(removeDelimiter(s));
        std::int64_t x;
        ss >> x;
        return Token(TokenType::INT, x);
    }

    if (isDouble(s)) {
        std::stringstream ss(removeDelimiter(s));
        double d;
        ss >> d;
        return Token(TokenType::DOUBLE, d);
    }

    return parseAsTime(s);
}

inline Token Lexer::parseAsTime(const std::string& str)
{
    const char* s = str.c_str();

    int n;
    int YYYY, MM, DD;
#if defined(_MSC_VER)
    if (sscanf_s(s, "%d-%d-%d%n", &YYYY, &MM, &DD, &n) != 3)
        return Token(TokenType::ERROR_TOKEN, std::string("Invalid token"));
#else
    if (sscanf(s, "%d-%d-%d%n", &YYYY, &MM, &DD, &n) != 3)
        return Token(TokenType::ERROR_TOKEN, std::string("Invalid token"));
#endif

    if (!(1 <= MM && MM <= 12)) {
        return Token(TokenType::ERROR_TOKEN, std::string("Invalid token"));
    }
    if (YYYY < 1900) {
        return Token(TokenType::ERROR_TOKEN, std::string("Invalid token"));
    }

    if (s[n] == '\0') {
        std::tm t;
        t.tm_sec = 0;
        t.tm_min = 0;
        t.tm_hour = 0;
        t.tm_mday = DD;
        t.tm_mon = MM - 1;
        t.tm_year = YYYY - 1900;
        auto tp = std::chrono::system_clock::from_time_t(timegm(&t));
        return Token(TokenType::TIME, tp);
    }

    if (s[n] != 'T')
        return Token(TokenType::ERROR_TOKEN, std::string("Invalid token"));

    s = s + n + 1;

    int hh, mm;
    double ss; // double for fraction
#if defined(_MSC_VER)
    if (sscanf_s(s, "%d:%d:%lf%n", &hh, &mm, &ss, &n) != 3)
        return Token(TokenType::ERROR_TOKEN, std::string("Invalid token"));
#else
    if (sscanf(s, "%d:%d:%lf%n", &hh, &mm, &ss, &n) != 3)
        return Token(TokenType::ERROR_TOKEN, std::string("Invalid token"));
#endif

    std::tm t;
    t.tm_sec = static_cast<int>(ss);
    t.tm_min = mm;
    t.tm_hour = hh;
    t.tm_mday = DD;
    t.tm_mon = MM - 1;
    t.tm_year = YYYY - 1900;
    auto tp = std::chrono::system_clock::from_time_t(timegm(&t));
    ss -= static_cast<int>(ss);
    // TODO(mayah): workaround GCC 4.9.3 on cygwin does not have std::round, but round().
    tp += std::chrono::microseconds(static_cast<std::int64_t>(round(ss * 1000000)));

    if (s[n] == '\0')
        return Token(TokenType::TIME, tp);

    if (s[n] == 'Z' && s[n + 1] == '\0')
        return Token(TokenType::TIME, tp);

    s = s + n;
    // offset
    // [+/-]%d:%d
    char pn;
    int oh, om;
#if defined(_MSC_VER)
    if (sscanf_s(s, "%c%d:%d", &pn, static_cast<unsigned>(sizeof(pn)), &oh, &om) != 3)
        return Token(TokenType::ERROR_TOKEN, std::string("Invalid token"));
#else
    if (sscanf(s, "%c%d:%d", &pn, &oh, &om) != 3)
        return Token(TokenType::ERROR_TOKEN, std::string("Invalid token"));
#endif

    if (pn != '+' && pn != '-')
        return Token(TokenType::ERROR_TOKEN, std::string("Invalid token"));

    if (pn == '+') {
        tp -= std::chrono::hours(oh);
        tp -= std::chrono::minutes(om);
    } else {
        tp += std::chrono::hours(oh);
        tp += std::chrono::minutes(om);
    }

    return Token(TokenType::TIME, tp);
}

inline Token Lexer::nextKeyToken()
{
    return nextToken(false);
}

inline Token Lexer::nextValueToken()
{
    return nextToken(true);
}

inline Token Lexer::nextToken(bool isValueToken)
{
    char c;
    while (current(&c)) {
        if (c == ' ' || c == '\t' || c == '\r') {
            next();
            continue;
        }

        if (c == '#') {
            skipUntilNewLine();
            continue;
        }

        switch (c) {
        case '\n':
            next();
            return Token(TokenType::END_OF_LINE);
        case '=':
            next();
            return Token(TokenType::EQUAL);
        case '{':
            next();
            return Token(TokenType::LBRACE);
        case '}':
            next();
            return Token(TokenType::RBRACE);
        case '[':
            next();
            return Token(TokenType::LBRACKET);
        case ']':
            next();
            return Token(TokenType::RBRACKET);
        case ',':
            next();
            return Token(TokenType::COMMA);
        case '.':
            next();
            return Token(TokenType::DOT);
        case '\"':
            return nextStringDoubleQuote();
        case '\'':
            return nextStringSingleQuote();
        default:
            if (isValueToken) {
                return nextValue();
            } else {
                return nextKey();
            }
        }
    }

    return Token(TokenType::END_OF_FILE);
}

} // namespace internal

// ----------------------------------------------------------------------

// static
inline const char* Value::typeToString(Value::Type type)
{
    switch (type) {
    case NULL_TYPE:   return "null";
    case BOOL_TYPE:   return "bool";
    case INT_TYPE:    return "int";
    case DOUBLE_TYPE: return "double";
    case STRING_TYPE: return "string";
    case TIME_TYPE:   return "time";
    case ARRAY_TYPE:  return "array";
    case TABLE_TYPE:  return "table";
    default:          return "unknown";
    }
}

inline Value::Value(const Value& v) :
    type_(v.type_), lineNo(v.lineNo)
{
    switch (v.type_) {
    case NULL_TYPE: null_ = v.null_; break;
    case BOOL_TYPE: bool_ = v.bool_; break;
    case INT_TYPE: int_ = v.int_; break;
    case DOUBLE_TYPE: double_ = v.double_; break;
    case STRING_TYPE: string_ = new std::string(*v.string_); break;
    case TIME_TYPE: time_ = new Time(*v.time_); break;
    case ARRAY_TYPE: array_ = new Array(*v.array_); break;
    case TABLE_TYPE: table_ = new Table(*v.table_); break;
    default:
        assert(false);
        type_ = NULL_TYPE;
        null_ = nullptr;
    }
}

inline Value::Value(Value&& v) noexcept :
    type_(v.type_), lineNo(v.lineNo)
{
    switch (v.type_) {
    case NULL_TYPE: null_ = v.null_; break;
    case BOOL_TYPE: bool_ = v.bool_; break;
    case INT_TYPE: int_ = v.int_; break;
    case DOUBLE_TYPE: double_ = v.double_; break;
    case STRING_TYPE: string_ = v.string_; break;
    case TIME_TYPE: time_ = v.time_; break;
    case ARRAY_TYPE: array_ = v.array_; break;
    case TABLE_TYPE: table_ = v.table_; break;
    default:
        assert(false);
        type_ = NULL_TYPE;
        null_ = nullptr;
    }
	
    v.type_ = NULL_TYPE;
    v.null_ = nullptr;
}

inline Value& Value::operator=(const Value& v)
{
    if (this == &v)
        return *this;

    this->~Value();

	lineNo = v.lineNo;
    type_ = v.type_;
    switch (v.type_) {
    case NULL_TYPE: null_ = v.null_; break;
    case BOOL_TYPE: bool_ = v.bool_; break;
    case INT_TYPE: int_ = v.int_; break;
    case DOUBLE_TYPE: double_ = v.double_; break;
    case STRING_TYPE: string_ = new std::string(*v.string_); break;
    case TIME_TYPE: time_ = new Time(*v.time_); break;
    case ARRAY_TYPE: array_ = new Array(*v.array_); break;
    case TABLE_TYPE: table_ = new Table(*v.table_); break;
    default:
        assert(false);
        type_ = NULL_TYPE;
        null_ = nullptr;
    }

    return *this;
}

inline Value& Value::operator=(Value&& v) noexcept
{
    if (this == &v)
        return *this;

    this->~Value();

	lineNo = v.lineNo;
    type_ = v.type_;
    switch (v.type_) {
    case NULL_TYPE: null_ = v.null_; break;
    case BOOL_TYPE: bool_ = v.bool_; break;
    case INT_TYPE: int_ = v.int_; break;
    case DOUBLE_TYPE: double_ = v.double_; break;
    case STRING_TYPE: string_ = v.string_; break;
    case TIME_TYPE: time_ = v.time_; break;
    case ARRAY_TYPE: array_ = v.array_; break;
    case TABLE_TYPE: table_ = v.table_; break;
    default:
        assert(false);
        type_ = NULL_TYPE;
        null_ = nullptr;
    }

    v.type_ = NULL_TYPE;
    v.null_ = nullptr;
    return *this;
}

inline Value::~Value()
{
    switch (type_) {
    case STRING_TYPE:
        delete string_;
        break;
    case TIME_TYPE:
        delete time_;
        break;
    case ARRAY_TYPE:
        delete array_;
        break;
    case TABLE_TYPE:
        delete table_;
        break;
    default:
        break;
    }
}

inline size_t Value::size() const
{
    switch (type_) {
    case NULL_TYPE:
        return 0;
    case ARRAY_TYPE:
        return array_->size();
    case TABLE_TYPE:
        return table_->size();
    default:
        return 1;
    }
}

inline bool Value::empty() const
{
    return size() == 0;
}

template<> struct Value::ValueConverter<bool>
{
    bool is(const Value& v) { return v.type() == Value::BOOL_TYPE; }
    bool to(const Value& v) { v.assureType<bool>(); return v.bool_; }

};
template<> struct Value::ValueConverter<int64_t>
{
    bool is(const Value& v) { return v.type() == Value::INT_TYPE; }
    int64_t to(const Value& v) { v.assureType<int64_t>(); return v.int_; }
};
template<> struct Value::ValueConverter<int>
{
    bool is(const Value& v) { return v.type() == Value::INT_TYPE; }
    int to(const Value& v) { v.assureType<int>(); return static_cast<int>(v.int_); }
};
template<> struct Value::ValueConverter<double>
{
    bool is(const Value& v) { return v.type() == Value::DOUBLE_TYPE; }
    double to(const Value& v) { v.assureType<double>(); return v.double_; }
};
template<> struct Value::ValueConverter<std::string>
{
    bool is(const Value& v) { return v.type() == Value::STRING_TYPE; }
    const std::string& to(const Value& v) { v.assureType<std::string>(); return *v.string_; }
};
template<> struct Value::ValueConverter<Time>
{
    bool is(const Value& v) { return v.type() == Value::TIME_TYPE; }
    const Time& to(const Value& v) { v.assureType<Time>(); return *v.time_; }
};
template<> struct Value::ValueConverter<Array>
{
    bool is(const Value& v) { return v.type() == Value::ARRAY_TYPE; }
    const Array& to(const Value& v) { v.assureType<Array>(); return *v.array_; }
};
template<> struct Value::ValueConverter<Table>
{
    bool is(const Value& v) { return v.type() == Value::TABLE_TYPE; }
    const Table& to(const Value& v) { v.assureType<Table>(); return *v.table_; }
};

template<typename T>
struct Value::ValueConverter<std::vector<T>>
{
    bool is(const Value& v)
    {
        if (v.type() != Value::ARRAY_TYPE)
            return false;
        const Array& array = v.as<Array>();
        if (array.empty())
            return true;
        return array.front().is<T>();
    }

    std::vector<T> to(const Value& v)
    {
        const Array& array = v.as<Array>();
        if (array.empty())
            return std::vector<T>();
        array.front().assureType<T>();

        std::vector<T> result;
        for (const auto& element : array) {
            result.push_back(element.as<T>());
        }

        return result;
    }
};

namespace internal {
template<typename T> inline const char* type_name();
template<> inline const char* type_name<bool>() { return "bool"; }
template<> inline const char* type_name<int>() { return "int"; }
template<> inline const char* type_name<int64_t>() { return "int64_t"; }
template<> inline const char* type_name<double>() { return "double"; }
template<> inline const char* type_name<std::string>() { return "string"; }
template<> inline const char* type_name<toml::Time>() { return "time"; }
template<> inline const char* type_name<toml::Array>() { return "array"; }
template<> inline const char* type_name<toml::Table>() { return "table"; }
} // namespace internal

template<typename T>
inline void Value::assureType() const
{
    if (!is<T>())
        failwith("type error: this value is ", typeToString(type_), " but ", internal::type_name<T>(), " was requested");
}

template<typename T>
inline bool Value::is() const
{
    return ValueConverter<T>().is(*this);
}

template<typename T>
inline typename call_traits<T>::return_type Value::as() const
{
    return ValueConverter<T>().to(*this);
}

inline bool Value::isNumber() const
{
    return is<int>() || is<double>();
}

inline double Value::asNumber() const
{
    if (is<int>())
        return as<int>();
    if (is<double>())
        return as<double>();

    failwith("type error: this value is ", typeToString(type_), " but number is requested");
}

inline std::time_t Value::as_time_t() const
{
    return std::chrono::system_clock::to_time_t(as<Time>());
}

inline std::string Value::spaces(int num)
{
    if (num <= 0)
        return std::string();

    return std::string(num, ' ');
}

inline std::string Value::escapeKey(const std::string& key)
{
    auto position = std::find_if(key.begin(), key.end(), [](char c) -> bool {
        if (std::isalnum(c) || c == '_' || c == '-')
            return false;
        return true;
    });

    if (position != key.end()) {
        std::string escaped = "\"";
        for (const char& c : key) {
            if (c == '\\' || c  == '"')
                escaped += '\\';
            escaped += c;
        }
        escaped += "\"";

        return escaped;
    }

    return key;
}

inline void Value::write(std::ostream* os, const std::string& keyPrefix, int indent) const
{
    switch (type_) {
    case NULL_TYPE:
        failwith("null type value is not a valid value");
        break;
    case BOOL_TYPE:
        (*os) << (bool_ ? "true" : "false");
        break;
    case INT_TYPE:
        (*os) << int_;
        break;
    case DOUBLE_TYPE: {
        (*os) << std::fixed << std::showpoint << double_;
        break;
    }
    case STRING_TYPE:
        (*os) << '"' << internal::escapeString(*string_) << '"';
        break;
    case TIME_TYPE: {
        time_t tt = std::chrono::system_clock::to_time_t(*time_);
        std::tm t;
        gmtime_r(&tt, &t);
        char buf[256];
        snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
        (*os) << buf;
        break;
    }
    case ARRAY_TYPE:
        (*os) << '[';
        for (size_t i = 0; i < array_->size(); ++i) {
            if (i)
                (*os) << ", ";
            (*array_)[i].write(os, keyPrefix, -1);
        }
        (*os) << ']';
        break;
    case TABLE_TYPE:
        for (const auto& kv : *table_) {
            if (kv.second.is<Table>())
                continue;
            if (kv.second.is<Array>() && kv.second.size() > 0 && kv.second.find(0)->is<Table>())
                continue;
            (*os) << spaces(indent) << escapeKey(kv.first) << " = ";
            kv.second.write(os, keyPrefix, indent >= 0 ? indent + 1 : indent);
            (*os) << '\n';
        }
        for (const auto& kv : *table_) {
            if (kv.second.is<Table>()) {
                std::string key(keyPrefix);
                if (!keyPrefix.empty())
                    key += ".";
                key += escapeKey(kv.first);
                (*os) << "\n" << spaces(indent) << "[" << key << "]\n";
                kv.second.write(os, key, indent >= 0 ? indent + 1 : indent);
            }
            if (kv.second.is<Array>() && kv.second.size() > 0 && kv.second.find(0)->is<Table>()) {
                std::string key(keyPrefix);
                if (!keyPrefix.empty())
                    key += ".";
                key += escapeKey(kv.first);
                for (const auto& v : kv.second.as<Array>()) {
                    (*os) << "\n" << spaces(indent) << "[[" << key << "]]\n";
                    v.write(os, key, indent >= 0 ? indent + 1 : indent);
                }
            }
        }
        break;
    default:
        failwith("writing unknown type");
        break;
    }
}

inline void Value::writeFormatted(std::ostream* os, FormatFlag flags) const
{
    int indent = flags & FORMAT_INDENT ? 0 : -1;

    write(os, std::string(), indent);
}

// static
inline FormatFlag operator|(FormatFlag lhs, FormatFlag rhs)
{
    return static_cast<FormatFlag>(static_cast<int>(lhs) | static_cast<int>(rhs));
}

// static
inline std::ostream& operator<<(std::ostream& os, const toml::Value& v)
{
    v.write(&os);
    return os;
}

// static
inline bool operator==(const Value& lhs, const Value& rhs)
{
    if (lhs.type() != rhs.type())
        return false;

    switch (lhs.type()) {
    case Value::Type::NULL_TYPE:
        return true;
    case Value::Type::BOOL_TYPE:
        return lhs.bool_ == rhs.bool_;
    case Value::Type::INT_TYPE:
        return lhs.int_ == rhs.int_;
    case Value::Type::DOUBLE_TYPE:
        return lhs.double_ == rhs.double_;
    case Value::Type::STRING_TYPE:
        return *lhs.string_ == *rhs.string_;
    case Value::Type::TIME_TYPE:
        return *lhs.time_ == *rhs.time_;
    case Value::Type::ARRAY_TYPE:
        return *lhs.array_ == *rhs.array_;
    case Value::Type::TABLE_TYPE:
        return *lhs.table_ == *rhs.table_;
    default:
        failwith("unknown type");
    }
}

template<typename T>
inline typename call_traits<T>::return_type Value::get(const std::string& key) const
{
    if (!is<Table>())
        failwith("type must be table to do get(key).");

    const Value* obj = find(key);
    if (!obj)
        failwith("key ", key, " was not found.");

    return obj->as<T>();
}

inline const Value* Value::find(const std::string& key) const
{
    if (!is<Table>())
        return nullptr;

    std::istringstream ss(key);
    internal::Lexer lexer(ss);

    const Value* current = this;
    while (true) {
        internal::Token t = lexer.nextKeyToken();
        if (!(t.type() == internal::TokenType::IDENT || t.type() == internal::TokenType::STRING))
            return nullptr;

        std::string part = t.strValue();
        t = lexer.nextKeyToken();
        if (t.type() == internal::TokenType::DOT) {
            current = current->findChild(part);
            if (!current || !current->is<Table>())
                return nullptr;
        } else if (t.type() == internal::TokenType::END_OF_FILE) {
            return current->findChild(part);
        } else {
            return nullptr;
        }
    }
}

inline Value* Value::find(const std::string& key)
{
    return const_cast<Value*>(const_cast<const Value*>(this)->find(key));
}

inline bool Value::merge(const toml::Value& v)
{
    if (this == &v)
        return true;
    if (!is<Table>() || !v.is<Table>())
        return false;

    for (const auto& kv : *v.table_) {
        if (Value* tmp = find(kv.first)) {
            // If both are table, we merge them.
            if (tmp->is<Table>() && kv.second.is<Table>()) {
                if (!tmp->merge(kv.second))
                    return false;
            } else {
                setChild(kv.first, kv.second);
            }
        } else {
            setChild(kv.first, kv.second);
        }
    }

    return true;
}

inline Value* Value::set(const std::string& key, const Value& v)
{
    Value* result = ensureValue(key);
    *result = v;
    return result;
}

inline Value* Value::setChild(const std::string& key, const Value& v)
{
    if (!valid())
        *this = Value(Table(), v.lineNo);

    if (!is<Table>())
        failwith("type must be table to do set(key, v).");

    (*table_)[key] = v;
    return &(*table_)[key];
}

inline Value* Value::setChild(const std::string& key, Value&& v)
{
    if (!valid())
        *this = Value(Table(), v.lineNo);

    if (!is<Table>())
        failwith("type must be table to do set(key, v).");

    (*table_)[key] = std::move(v);
    return &(*table_)[key];
}

inline bool Value::erase(const std::string& key)
{
    if (!is<Table>())
        return false;

    std::istringstream ss(key);
    internal::Lexer lexer(ss);

    Value* current = this;
    while (true) {
        internal::Token t = lexer.nextKeyToken();
        if (!(t.type() == internal::TokenType::IDENT || t.type() == internal::TokenType::STRING))
            return false;

        std::string part = t.strValue();
        t = lexer.nextKeyToken();
        if (t.type() == internal::TokenType::DOT) {
            current = current->findChild(part);
            if (!current || !current->is<Table>())
                return false;
        } else if (t.type() == internal::TokenType::END_OF_FILE) {
            return current->eraseChild(part);
        } else {
            return false;
        }
    }
}

inline bool Value::eraseChild(const std::string& key)
{
    if (!is<Table>())
        failwith("type must be table to do erase(key).");

    return table_->erase(key) > 0;
}

inline Value& Value::operator[](const std::string& key)
{
    if (!valid())
        *this = Value(Table(), lineNo);

    if (Value* v = findChild(key))
        return *v;

    return *setChild(key, Value());
}

template<typename T>
inline typename call_traits<T>::return_type Value::get(size_t index) const
{
    if (!is<Array>())
        failwith("type must be array to do get(index).");

    if (array_->size() <= index)
        failwith("index out of bound");

    return (*array_)[index].as<T>();
}

inline const Value* Value::find(size_t index) const
{
    if (!is<Array>())
        return nullptr;
    if (index < array_->size())
        return &(*array_)[index];
    return nullptr;
}

inline Value* Value::find(size_t index)
{
    return const_cast<Value*>(const_cast<const Value*>(this)->find(index));
}

inline Value* Value::push(const Value& v)
{
    if (!valid())
        *this = Value(Array(), lineNo);
    else if (!is<Array>())
        failwith("type must be array to do push(Value).");

    array_->push_back(v);
    return &array_->back();
}

inline Value* Value::push(Value&& v)
{
    if (!valid())
        *this = Value(Array(), lineNo);
    else if (!is<Array>())
        failwith("type must be array to do push(Value).");

    array_->push_back(std::move(v));
    return &array_->back();
}

inline Value* Value::ensureValue(const std::string& key)
{
    if (!valid())
        *this = Value(Table(), lineNo);
    if (!is<Table>()) {
        failwith("encountered non table value");
    }

    std::istringstream ss(key);
    internal::Lexer lexer(ss);

    Value* current = this;
    while (true) {
        internal::Token t = lexer.nextKeyToken();
        if (!(t.type() == internal::TokenType::IDENT || t.type() == internal::TokenType::STRING)) {
            failwith("invalid key");
        }

        std::string part = t.strValue();
        t = lexer.nextKeyToken();
        if (t.type() == internal::TokenType::DOT) {
            if (Value* candidate = current->findChild(part)) {
                if (!candidate->is<Table>())
                    failwith("encountered non table value");

                current = candidate;
            } else {
                current = current->setChild(part, Value(Table(), lineNo));
            }
        } else if (t.type() == internal::TokenType::END_OF_FILE) {
            if (Value* v = current->findChild(part))
                return v;
            return current->setChild(part, Value());
        } else {
            failwith("invalid key");
        }
    }
}

inline Value* Value::findChild(const std::string& key)
{
    assert(is<Table>());

    auto it = table_->find(key);
    if (it == table_->end())
        return nullptr;

    return &it->second;
}

inline const Value* Value::findChild(const std::string& key) const
{
    assert(is<Table>());

    auto it = table_->find(key);
    if (it == table_->end())
        return nullptr;

    return &it->second;
}

// ----------------------------------------------------------------------

namespace internal {

inline void Parser::skipForKey()
{
    while (token().type() == TokenType::END_OF_LINE)
        nextKey();
}

inline void Parser::skipForValue()
{
    while (token().type() == TokenType::END_OF_LINE)
        nextValue();
}

inline bool Parser::consumeForKey(TokenType type)
{
    if (token().type() == type) {
        nextKey();
        return true;
    }

    return false;
}

inline bool Parser::consumeForValue(TokenType type)
{
    if (token().type() == type) {
        nextValue();
        return true;
    }

    return false;
}

inline bool Parser::consumeEOLorEOFForKey()
{
    if (token().type() == TokenType::END_OF_LINE || token().type() == TokenType::END_OF_FILE) {
        nextKey();
        return true;
    }

    return false;
}

inline void Parser::addError(const std::string& reason)
{
    if (!errorReason_.empty())
        return;

    std::stringstream ss;
    ss << reason << " at line " << lexer_.lineNo();
    errorReason_ = ss.str();
}

inline const std::string& Parser::errorReason()
{
    return errorReason_;
}

inline Value Parser::parse()
{
    Value root(Table(), lexer_.lineNo());
    Value* currentValue = &root;
	currentValue->lineNo = lexer_.lineNo();

    while (true) {
        skipForKey();
        if (token().type() == TokenType::END_OF_FILE)
            break;
        if (token().type() == TokenType::LBRACKET) {
            currentValue = parseGroupKey(&root);
            if (!currentValue) {
                addError("Error when parsing group key");
                return Value();
            }
            continue;
        }

        if (!parseKeyValue(currentValue)) {
            addError("Error when parsing key Value");
            return Value();
        }
    }
    return root;
}

inline Value* Parser::parseGroupKey(Value* root)
{
    if (!consumeForKey(TokenType::LBRACKET))
        return nullptr;

    bool isArray = false;
    if (token().type() == TokenType::LBRACKET) {
        nextKey();
        isArray = true;
    }

    Value* currentValue = root;
    while (true) {
        if (token().type() != TokenType::IDENT && token().type() != TokenType::STRING)
            return nullptr;

        std::string key = token().strValue();
        nextKey();

		int lineNo = lexer_.lineNo();

        if (token().type() == TokenType::DOT) {
            nextKey();
            if (Value* candidate = currentValue->findChild(key)) {
                if (candidate->is<Array>() && candidate->size() > 0)
                    candidate = candidate->find(candidate->size() - 1);
                if (!candidate->is<Table>())
                    return nullptr;
                currentValue = candidate;
            } else {
                currentValue = currentValue->setChild(key, Value(Table(), lineNo));
				currentValue->lineNo = lineNo;
            }
            continue;
        }

        if (token().type() == TokenType::RBRACKET) {
            nextKey();
            if (Value* candidate = currentValue->findChild(key)) {
                if (isArray) {
                    if (!candidate->is<Array>())
                        return nullptr;
                    currentValue = candidate->push(Value(Table(), lineNo));
					currentValue->lineNo = lineNo;
                } else {
                    if (candidate->is<Array>() && candidate->size() > 0)
                        candidate = candidate->find(candidate->size() - 1);
                    if (!candidate->is<Table>())
                        return nullptr;
                    currentValue = candidate;
                }
            } else {
                if (isArray) 
				{
					Value arr = Value(Array(), lineNo);
					arr.lineNo = lineNo;
                    currentValue = currentValue->setChild(key, arr);
                    currentValue = currentValue->push(Value(Table(), lineNo));
					currentValue->lineNo = lineNo;
                } else 
				{
					Value tab = Value(Table(), lineNo);
					tab.lineNo = lineNo;
                    currentValue = currentValue->setChild(key, tab);
                }
            }
            break;
        }

        return nullptr;
    }

    if (isArray) {
        if (!consumeForKey(TokenType::RBRACKET))
            return nullptr;
    }

    if (!consumeEOLorEOFForKey())
        return nullptr;

    return currentValue;
}

inline bool Parser::parseKeyValue(Value* current)
{
    std::string key;
    if (!parseKey(&key)) {
        addError("Parse key failed");
        return false;
    }
    if (!consumeForValue(TokenType::EQUAL)) {
        addError("No equal?");
        return false;
    }

    Value v;
    if (!parseValue(&v))
        return false;
    if (!consumeEOLorEOFForKey())
        return false;

    if (current->has(key)) {
        addError("Multiple same key: " + key);
        return false;
    }

    current->setChild(key, std::move(v));
    return true;
}

inline bool Parser::parseKey(std::string* key)
{
    key->clear();

    if (token().type() == TokenType::IDENT || token().type() == TokenType::STRING) {
        *key = token().strValue();
        nextValue();
        return true;
    }

    return false;
}

inline bool Parser::parseValue(Value* v)
{	
    switch (token().type()) {
    case TokenType::STRING:
    case TokenType::MULTILINE_STRING:
        *v = Value(token().strValue(), lexer_.lineNo());
        nextValue();
        return true;
    case TokenType::LBRACKET:
        return parseArray(v);
    case TokenType::LBRACE:
        return parseInlineTable(v);
    case TokenType::BOOL:
        *v = Value(token().boolValue(), lexer_.lineNo());
        nextValue();
        return true;
    case TokenType::INT:
        *v = Value(token().intValue(), lexer_.lineNo());
        nextValue();
        return true;
    case TokenType::DOUBLE:
        *v = Value(token().doubleValue(), lexer_.lineNo());
        nextValue();
        return true;
    case TokenType::TIME:
        *v = Value(token().timeValue(), lexer_.lineNo());
        nextValue();
        return true;
    case TokenType::ERROR_TOKEN:
        addError(token().strValue());
        return false;
    default:
        addError("Unexpected token");
        return false;
    }
}

inline bool Parser::parseBool(Value* v)
{
	int lineNo = lexer_.lineNo();
    if (token().strValue() == "true") {
        nextValue();
        *v = Value(true, lineNo);
        return true;
    }

    if (token().strValue() == "false") {		
        nextValue();
        *v = Value(false, lineNo);
        return true;
    }

    return false;
}

inline bool Parser::parseArray(Value* v)
{
	int lineNo = lexer_.lineNo();
    if (!consumeForValue(TokenType::LBRACKET))
        return false;

    Array a;
    while (true) {
        skipForValue();

        if (token().type() == TokenType::RBRACKET)
            break;

        skipForValue();
        Value x;
        if (!parseValue(&x))
            return false;

        if (!a.empty()) {
            if (a.front().type() != x.type()) {
                addError("Type check failed");
                return false;
            }
        }

        a.push_back(std::move(x));
        skipForValue();
        if (token().type() == TokenType::RBRACKET)
            break;
        if (token().type() == TokenType::COMMA)
            nextValue();
    }

    if (!consumeForValue(TokenType::RBRACKET))
        return false;
    *v = std::move(Value(a, lineNo));
    return true;
}

inline bool Parser::parseInlineTable(Value* value)
{
    // For inline table, next is KEY, so use consumeForKey here.
    if (!consumeForKey(TokenType::LBRACE))
        return false;

    Value t(Table(), lexer_.lineNo());
    bool first = true;
    while (true) {
        if (token().type() == TokenType::RBRACE) {
            break;
        }

        if (!first) {
            if (token().type() != TokenType::COMMA) {
                addError("Inline table didn't have ',' for delimiter?");
                return false;
            }
            nextKey();
        }
        first = false;

        std::string key;
        if (!parseKey(&key))
            return false;
        if (!consumeForValue(TokenType::EQUAL))
            return false;
        Value v;
        if (!parseValue(&v))
            return false;

        if (t.has(key)) {
            addError("Inline table has multiple same keys: key=" + key);
            return false;
        }

        t.set(key, v);
    }

    if (!consumeForValue(TokenType::RBRACE))
        return false;
    *value = std::move(t);
    return true;
}

} // namespace internal
} // namespace toml

#endif // TINYTOML_H_
