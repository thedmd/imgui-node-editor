// Crude implementation of JSON value object and parser.
//
// VERSION 0.1
//
// LICENSE
//   This software is dual-licensed to the public domain and under the following
//   license: you are granted a perpetual, irrevocable license to copy, modify,
//   publish, and distribute this file as you see fit.
//
// CREDITS
//   Written by Michal Cichon
# include "crude_json.h"
# include <iomanip>
# include <limits>
# include <locale>
# include <cstdlib>
# include <cmath>
# include <cstring>
# if __cplusplus >= 201703L || (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L)
#     include <charconv>
# endif
# if CRUDE_JSON_IO
#     include <stdio.h>
#     include <memory>
#     ifdef _WIN32
#         ifndef WIN32_LEAN_AND_MEAN
#             define WIN32_LEAN_AND_MEAN
#         endif
#         ifndef NOMINMAX
#             define NOMINMAX
#         endif
#         include <windows.h>
#     endif
# endif

namespace crude_json {

// Numbers are converted independently of the global C and C++ locales.
static bool parse_number(const string& text, number& result)
{
# if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
    const auto end = text.data() + text.size();
    const auto converted = std::from_chars(text.data(), end, result);
    return converted.ec == std::errc() && converted.ptr == end;
# else
    std::istringstream stream(text);
    stream.imbue(std::locale::classic());
    stream >> result;
    // libc++ fails the extraction of a subnormal, on the range error from strtod(),
    // and stores the converted value.
    const auto subnormal = result != 0 && std::fabs(result) < std::numeric_limits<number>::min();
    return (!stream.fail() || subnormal) && stream.eof();
# endif
}

value::value(value&& other) noexcept
    : m_Type(other.m_Type)
{
    switch (m_Type)
    {
        case type_t::object:    construct(m_Storage, std::move( *object_ptr(other.m_Storage))); break;
        case type_t::array:     construct(m_Storage, std::move(  *array_ptr(other.m_Storage))); break;
        case type_t::string:    construct(m_Storage, std::move( *string_ptr(other.m_Storage))); break;
        case type_t::boolean:   construct(m_Storage, std::move(*boolean_ptr(other.m_Storage))); break;
        case type_t::number:    construct(m_Storage, std::move( *number_ptr(other.m_Storage))); break;
        default: break;
    }
    destruct(other.m_Storage, other.m_Type);
    other.m_Type = type_t::null;
}

value::value(const value& other)
    : m_Type(other.m_Type)
{
    switch (m_Type)
    {
        case type_t::object:    construct(m_Storage,  *object_ptr(other.m_Storage)); break;
        case type_t::array:     construct(m_Storage,   *array_ptr(other.m_Storage)); break;
        case type_t::string:    construct(m_Storage,  *string_ptr(other.m_Storage)); break;
        case type_t::boolean:   construct(m_Storage, *boolean_ptr(other.m_Storage)); break;
        case type_t::number:    construct(m_Storage,  *number_ptr(other.m_Storage)); break;
        default: break;
    }
}

// Const lookups that find nothing return this.
static const value& null_value()
{
    static const value s_Null;
    return s_Null;
}

// Mutable lookups that can't add to the document return this. Writes to it are lost.
static value& scratch_value()
{
    static thread_local value s_Scratch;
    s_Scratch = nullptr;
    return s_Scratch;
}

value& value::operator[](size_t index)
{
    if (is_null())
        m_Type = construct(m_Storage, type_t::array);

    if (is_array())
    {
        auto& v = *array_ptr(m_Storage);
        if (index < v.max_size())
        {
            if (index >= v.size())
                v.insert(v.end(), index - v.size() + 1, value());

            return v[index];
        }
    }

    CRUDE_ASSERT(false && "operator[] on unsupported type or index");
    return scratch_value();
}

const value& value::operator[](size_t index) const
{
    if (is_array())
    {
        auto& v = *array_ptr(m_Storage);
        if (index < v.size())
            return v[index];
    }

    return null_value();
}

value& value::operator[](const string& key)
{
    if (is_null())
        m_Type = construct(m_Storage, type_t::object);

    if (is_object())
        return (*object_ptr(m_Storage))[key];

    CRUDE_ASSERT(false && "operator[] on unsupported type");
    return scratch_value();
}

const value& value::operator[](const string& key) const
{
    if (auto entry = find(key))
        return *entry;

    return null_value();
}

value* value::find(const string& key)
{
    return const_cast<value*>(static_cast<const value*>(this)->find(key));
}

const value* value::find(const string& key) const
{
    if (!is_object())
        return nullptr;

    auto& o = *object_ptr(m_Storage);
    auto it = o.find(key);
    return it != o.end() ? &it->second : nullptr;
}

bool value::contains(const string& key) const
{
    if (is_object())
    {
        auto& o = *object_ptr(m_Storage);
        auto it = o.find(key);
        return it != o.end();
    }

    return false;
}

void value::push_back(const value& value)
{
    if (is_null())
        m_Type = construct(m_Storage, type_t::array);

    if (is_array())
    {
        auto& v = *array_ptr(m_Storage);
        v.push_back(value);
    }
    else
    {
        CRUDE_ASSERT(false && "operator[] on unsupported type");
        std::terminate();
    }
}

void value::push_back(value&& value)
{
    if (is_null())
        m_Type = construct(m_Storage, type_t::array);

    if (is_array())
    {
        auto& v = *array_ptr(m_Storage);
        v.push_back(std::move(value));
    }
    else
    {
        CRUDE_ASSERT(false && "operator[] on unsupported type");
        std::terminate();
    }
}

size_t value::erase(const string& key)
{
    if (!is_object())
        return 0;

    auto& o = *object_ptr(m_Storage);
    auto it = o.find(key);

    if (it == o.end())
        return 0;

    o.erase(it);

    return 1;
}

void value::swap(value& other) noexcept
{
    using std::swap;

    if (m_Type == other.m_Type)
    {
        switch (m_Type)
        {
            case type_t::object:    swap(*object_ptr(m_Storage),  *object_ptr(other.m_Storage));  break;
            case type_t::array:     swap(*array_ptr(m_Storage),   *array_ptr(other.m_Storage));   break;
            case type_t::string:    swap(*string_ptr(m_Storage),  *string_ptr(other.m_Storage));  break;
            case type_t::boolean:   swap(*boolean_ptr(m_Storage), *boolean_ptr(other.m_Storage)); break;
            case type_t::number:    swap(*number_ptr(m_Storage),  *number_ptr(other.m_Storage));  break;
            default: break;
        }
    }
    else
    {
        value tmp(std::move(other));
        other.~value();
        new (&other) value(std::move(*this));
        this->~value();
        new (this) value(std::move(tmp));
    }
}

string value::dump(const int indent, const char indent_char) const
{
    dump_context_t context(indent, indent_char);

    context.out.imbue(std::locale::classic());
    context.out.precision(std::numeric_limits<double>::max_digits10 + 1);
    context.out << std::defaultfloat;

    dump(context, 0);
    return context.out.str();
}

void value::dump_context_t::write_indent(int level)
{
    if (indent <= 0 || level == 0)
        return;

    out.fill(indent_char);
    out.width(indent * level);
    out << indent_char;
    out.width(0);
}

void value::dump_context_t::write_separator()
{
    if (indent < 0)
        return;

    out.put(' ');
}

void value::dump_context_t::write_newline()
{
    if (indent < 0)
        return;

    out.put('\n');
}

static void write_string(std::ostream& out, const string& text)
{
    static const char hex_digits[] = "0123456789abcdef";

    out << '\"';
    for (const auto c : text)
    {
        switch (c)
        {
            case '\"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b";  break;
            case '\f': out << "\\f";  break;
            case '\n': out << "\\n";  break;
            case '\r': out << "\\r";  break;
            case '\t': out << "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                    out << "\\u00" << hex_digits[c >> 4] << hex_digits[c & 0xF];
                else
                    out << c;
                break;
        }
    }
    out << '\"';
}

void value::dump(dump_context_t& context, int level) const
{
    context.write_indent(level);

    switch (m_Type)
    {
        case type_t::null:
            context.out << "null";
            break;

        case type_t::object:
            context.out << '{';
            {
                context.write_newline();
                bool first = true;
                for (auto& entry : *object_ptr(m_Storage))
                {
                    if (!first) { context.out << ','; context.write_newline(); } else first = false;
                    context.write_indent(level + 1);
                    write_string(context.out, entry.first);
                    context.out << ':';
                    if (!entry.second.is_structured())
                    {
                        context.write_separator();
                        entry.second.dump(context, 0);
                    }
                    else
                    {
                        context.write_newline();
                        entry.second.dump(context, level + 1);
                    }
                }
                if (!first)
                    context.write_newline();
            }
            context.write_indent(level);
            context.out << '}';
            break;

        case type_t::array:
            context.out << '[';
            {
                context.write_newline();
                bool first = true;
                for (auto& entry : *array_ptr(m_Storage))
                {
                    if (!first) { context.out << ','; context.write_newline(); } else first = false;
                    if (!entry.is_structured())
                    {
                        context.write_indent(level + 1);
                        entry.dump(context, 0);
                    }
                    else
                    {
                        entry.dump(context, level + 1);
                    }
                }
                if (!first)
                    context.write_newline();
            }
            context.write_indent(level);
            context.out << ']';
            break;

        case type_t::string:
            write_string(context.out, *string_ptr(m_Storage));
            break;


        case type_t::boolean:
            if (*boolean_ptr(m_Storage))
                context.out << "true";
            else
                context.out << "false";
            break;

        case type_t::number:
            // JSON has no NaN and no infinity.
            if (std::isfinite(*number_ptr(m_Storage)))
                context.out << *number_ptr(m_Storage);
            else
                context.out << "null";
            break;

        default:
            break;
    }
}

static void append_utf8(string& result, unsigned long code_point)
{
    if (code_point < 0x80)
    {
        result.push_back(static_cast<char>(code_point));
    }
    else if (code_point < 0x800)
    {
        result.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
        result.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
    else if (code_point < 0x10000)
    {
        result.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
        result.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
    else
    {
        result.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
        result.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
}

struct value::parser
{
    parser(const char* begin, const char* end)
        : m_Cursor(begin)
        , m_End(end)
    {
    }

    value parse()
    {
        value v;

        // Accept single value only when end of the stream is reached.
        if (!accept_element(v) || !eof())
            v = value(type_t::discarded);

        return v;
    }

private:
    struct cursor_state
    {
        cursor_state(parser* p)
            : m_Owner(p)
            , m_LastCursor(p->m_Cursor)
        {
        }

        void reset()
        {
            m_Owner->m_Cursor = m_LastCursor;
        }

        bool operator()(bool accept)
        {
            if (!accept)
                reset();
            else
                m_LastCursor = m_Owner->m_Cursor;
            return accept;
        }

    private:
        parser*     m_Owner;
        const char* m_LastCursor;
    };

    cursor_state state()
    {
        return cursor_state(this);
    }

    bool accept_value(value& result)
    {
        if (peek() == '{' || peek() == '[')
        {
            if (m_Depth >= CRUDE_JSON_MAX_DEPTH)
                return false;

            ++m_Depth;
            const auto accepted = accept_object(result) || accept_array(result);
            --m_Depth;
            return accepted;
        }

        return accept_string(result)
            || accept_number(result)
            || accept_boolean(result)
            || accept_null(result);
    }

    bool accept_object(value& result)
    {
        auto s = state();

        object o;
        if (s(accept('{') && accept_ws() && accept('}')))
        {
            result = o;
            return true;
        }
        else if (s(accept('{') && accept_members(o) && accept('}')))
        {
            result = std::move(o);
            return true;
        }

        return false;
    }

    bool accept_members(object& o)
    {
        if (!accept_member(o))
            return false;

        while (true)
        {
            auto s = state();
            if (!s(accept(',') && accept_member(o)))
                break;
        }

        return true;
    }

    bool accept_member(object& o)
    {
        auto s = state();

        value key;
        value v;
        if (s(accept_ws() && accept_string(key) && accept_ws() && accept(':') && accept_element(v)))
        {
            o.emplace(std::move(key.get<string>()), std::move(v));
            return true;
        }

        return false;
    }

    bool accept_array(value& result)
    {
        auto s = state();

        if (s(accept('[') && accept_ws() && accept(']')))
        {
            result = array();
            return true;
        }

        array a;
        if (s(accept('[') && accept_elements(a) && accept(']')))
        {
            result = std::move(a);
            return true;
        }

        return false;
    }

    bool accept_elements(array& a)
    {
        value v;
        if (!accept_element(v))
            return false;

        a.emplace_back(std::move(v));
        while (true)
        {
            auto s = state();
            v = nullptr;
            if (!s(accept(',') && accept_element(v)))
                break;
            a.emplace_back(std::move(v));
        }

        return true;
    }

    bool accept_element(value& result)
    {
        auto s = state();
        return s(accept_ws() && accept_value(result) && accept_ws());
    }

    bool accept_string(value& result)
    {
        auto s = state();

        string v;
        if (s(accept('\"') && accept_characters(v) && accept('\"')))
        {
            result = std::move(v);
            return true;
        }
        else
            return false;
    }

    // Bytes other than escapes are taken as they are, so UTF-8 text stays UTF-8.
    bool accept_characters(string& result)
    {
        while (!eof() && !expect('\"'))
        {
            if (accept('\\'))
            {
                if (!accept_escape(result))
                    return false;
            }
            else
            {
                result.push_back(static_cast<char>(peek()));
                advance();
            }
        }

        return true;
    }

    bool accept_escape(string& result)
    {
        if (accept('\"')) { result.push_back('\"'); return true; }
        if (accept('\\')) { result.push_back('\\'); return true; }
        if (accept('/'))  { result.push_back('/');  return true; }
        if (accept('b'))  { result.push_back('\b'); return true; }
        if (accept('f'))  { result.push_back('\f'); return true; }
        if (accept('n'))  { result.push_back('\n'); return true; }
        if (accept('r'))  { result.push_back('\r'); return true; }
        if (accept('t'))  { result.push_back('\t'); return true; }

        unsigned long code_point = 0;
        if (!accept('u') || !accept_code_unit(code_point))
            return false;

        // A UTF-16 surrogate pair escapes a code point above 0xFFFF.
        if (code_point >= 0xDC00 && code_point <= 0xDFFF)
            return false;
        if (code_point >= 0xD800 && code_point <= 0xDBFF)
        {
            unsigned long low = 0;
            if (!accept('\\') || !accept('u') || !accept_code_unit(low) || low < 0xDC00 || low > 0xDFFF)
                return false;
            code_point = 0x10000 + ((code_point - 0xD800) << 10) + (low - 0xDC00);
        }

        append_utf8(result, code_point);
        return true;
    }

    bool accept_code_unit(unsigned long& result)
    {
        string hex;
        hex.reserve(4);
        if (!(accept_hex(hex) && accept_hex(hex) && accept_hex(hex) && accept_hex(hex)))
            return false;

        result = std::strtoul(hex.c_str(), nullptr, 16);
        return true;
    }

    bool accept_hex(string& result)
    {
        if (accept_digit(result))
            return true;

        auto c = peek();
        if ((c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))
        {
            advance();
            result.push_back(static_cast<char>(c));
            return true;
        }

        return false;
    }

    bool accept_number(value& result)
    {
        auto s = state();

        string n;
        number v = 0;
        if (!(accept_int(n) && accept_frac(n) && accept_exp(n)) || !parse_number(n, v) || !std::isfinite(v))
        {
            s.reset();
            return false;
        }

        result = v;
        return true;
    }

    bool accept_int(string& result)
    {
        auto s = state();

        string part;
        if (s(accept_onenine(part) && accept_digits(part)))
        {
            result += std::move(part);
            return true;
        }

        part.resize(0);
        if (accept_digit(part))
        {
            result += std::move(part);
            return true;
        }

        part.resize(0);
        if (s(accept('-') && accept_onenine(part) && accept_digits(part)))
        {
            result += '-';
            result += std::move(part);
            return true;
        }

        part.resize(0);
        if (s(accept('-') && accept_digit(part)))
        {
            result += '-';
            result += std::move(part);
            return true;
        }

        return false;
    }

    bool accept_digits(string& result)
    {
        string part;
        if (!accept_digit(part))
            return false;

        while (accept_digit(part))
            ;

        result += std::move(part);

        return true;
    }

    bool accept_digit(string& result)
    {
        if (accept('0'))
        {
            result.push_back('0');
            return true;
        }
        else if (accept_onenine(result))
            return true;

        return false;
    }

    bool accept_onenine(string& result)
    {
        auto c = peek();
        if (c >= '1' && c <= '9')
        {
            result.push_back(static_cast<char>(c));
            return advance();
        }

        return false;
    }

    bool accept_frac(string& result)
    {
        auto s = state();

        string part;
        if (s(accept('.') && accept_digits(part)))
        {
            result += '.';
            result += std::move(part);
        }

        return true;
    }

    bool accept_exp(string& result)
    {
        auto s = state();

        string part;
        if (s(accept('e') && accept_sign(part) && accept_digits(part)))
        {
            result += 'e';
            result += std::move(part);
            return true;
        }
        part.resize(0);
        if (s(accept('E') && accept_sign(part) && accept_digits(part)))
        {
            result += 'E';
            result += std::move(part);
        }

        return true;
    }

    bool accept_sign(string& result)
    {
        if (accept('+'))
            result.push_back('+');
        else if (accept('-'))
            result.push_back('-');

        return true;
    }

    bool accept_ws()
    {
        while (expect('\x09') || expect('\x0A') || expect('\x0D') || expect('\x20'))
            advance();
        return true;
    }

    bool accept_boolean(value& result)
    {
        if (accept("true"))
        {
            result = true;
            return true;
        }
        else if (accept("false"))
        {
            result = false;
            return true;
        }

        return false;
    }

    bool accept_null(value& result)
    {
        if (accept("null"))
        {
            result = nullptr;
            return true;
        }

        return false;
    }

    bool accept(char c)
    {
        if (expect(c))
            return advance();
        else
            return false;
    }

    bool accept(const char* str)
    {
        auto last = m_Cursor;

        while (*str)
        {
            if (eof() || *str != *m_Cursor)
            {
                m_Cursor = last;
                return false;
            }

            advance();
            ++str;
        }

        return true;
    }

    int peek() const
    {
        if (!eof())
            return static_cast<unsigned char>(*m_Cursor);
        else
            return -1;
    }

    bool expect(char c)
    {
        return peek() == c;
    }

    bool advance(int count = 1)
    {
        if (m_Cursor + count > m_End)
        {
            m_Cursor = m_End;
            return false;
        }

        m_Cursor += count;

        return true;
    }

    bool eof() const
    {
        return m_Cursor == m_End;
    }

    const char* m_Cursor;
    const char* m_End;
    int         m_Depth = 0;
};

value value::parse(const string& data)
{
    auto p = parser(data.c_str(), data.c_str() + data.size());

    auto v = p.parse();

    return v;
}

# if CRUDE_JSON_IO
std::pair<value, bool> value::load(const string& path)
{
    // Modern C++, so beautiful...
    std::unique_ptr<FILE, void(*)(FILE*)> file{nullptr, [](FILE* file) { if (file) fclose(file); }};
# if defined(_MSC_VER) || (defined(__STDC_LIB_EXT1__) && __STDC_WANT_LIB_EXT1__)
    FILE* handle = nullptr;
    if (fopen_s(&handle, path.c_str(), "rb") != 0)
        return {value{}, false};
    file.reset(handle);
# else
    file.reset(fopen(path.c_str(), "rb"));
# endif

    if (!file)
        return {value{}, false};

    string data;
    char buffer[4096];
    while (const auto size = fread(buffer, 1, sizeof(buffer), file.get()))
        data.append(buffer, size);
    if (ferror(file.get()))
        return {value{}, false};

    const char byte_order_mark[] = "\xEF\xBB\xBF";
    const auto start = data.compare(0, 3, byte_order_mark) == 0 ? 3 : 0;
    auto p = parser(data.c_str() + start, data.c_str() + data.size());
    return {p.parse(), true};
}

// Replaces 'to' when it exists.
static bool replace_file(const string& from, const string& to)
{
# if defined(_WIN32)
    return MoveFileExA(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
# else
    return rename(from.c_str(), to.c_str()) == 0;
# endif
}

bool value::save(const string& path, const int indent, const char indent_char) const
{
    const auto data      = dump(indent, indent_char);
    const auto temp_path = path + ".tmp";

    FILE* file = nullptr;
# if defined(_MSC_VER) || (defined(__STDC_LIB_EXT1__) && __STDC_WANT_LIB_EXT1__)
    if (fopen_s(&file, temp_path.c_str(), "wb") != 0)
        return false;
# else
    file = fopen(temp_path.c_str(), "wb");
# endif
    if (!file)
        return false;

    const auto written = fwrite(data.data(), 1, data.size(), file) == data.size();
    const auto closed  = fclose(file) == 0;
    if (!written || !closed || !replace_file(temp_path, path))
    {
        remove(temp_path.c_str());
        return false;
    }

    return true;
}

# endif

} // namespace crude_json
