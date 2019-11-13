// Crude implementation of JSON value object and parser.
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
# include <cstdlib>
# include <clocale>
# include <cmath>
# include <cstring>


namespace crude_json {

value::value(value&& other)
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

value& value::operator[](size_t index)
{
    if (is_null())
        m_Type = construct(m_Storage, type_t::array);

    if (is_array())
    {
        auto& v = *array_ptr(m_Storage);
        if (index >= v.size())
            v.insert(v.end(), index - v.size() + 1, value());

        return v[index];
    }

    CRUDE_ASSERT(false && "operator[] on unsupported type");
    std::terminate();
}

const value& value::operator[](size_t index) const
{
    if (is_array())
        return (*array_ptr(m_Storage))[index];

    CRUDE_ASSERT(false && "operator[] on unsupported type");
    std::terminate();
}

value& value::operator[](const string& key)
{
    if (is_null())
        m_Type = construct(m_Storage, type_t::object);

    if (is_object())
        return (*object_ptr(m_Storage))[key];

    CRUDE_ASSERT(false && "operator[] on unsupported type");
    std::terminate();
}

const value& value::operator[](const string& key) const
{
    if (is_object())
    {
        auto& o = *object_ptr(m_Storage);
        auto it = o.find(key);
        CRUDE_ASSERT(it != o.end());
        return it->second;
    }

    CRUDE_ASSERT(false && "operator[] on unsupported type");
    std::terminate();
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

void value::swap(value& other)
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
                    context.out << '\"' << entry.first << "\":";
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
            context.out << '\"';

            if (string_ptr(m_Storage)->find_first_of("\"\\/\b\f\n\r") != string::npos || string_ptr(m_Storage)->find('\0') != string::npos)
            {
                for (auto c : *string_ptr(m_Storage))
                {
                         if (c == '\"')  context.out << "\\\"";
                    else if (c == '\\')  context.out << "\\\\";
                    else if (c == '/')   context.out << "\\/";
                    else if (c == '\b')  context.out << "\\b";
                    else if (c == '\f')  context.out << "\\f";
                    else if (c == '\n')  context.out << "\\n";
                    else if (c == '\r')  context.out << "\\r";
                    else if (c == '\t')  context.out << "\\t";
                    else if (c == 0)     context.out << "\\u0000";
                    else                 context.out << c;
                }
            }
            else
                context.out << *string_ptr(m_Storage);
            context.out << '\"';
            break;


        case type_t::boolean:
            if (*boolean_ptr(m_Storage))
                context.out << "true";
            else
                context.out << "false";
            break;

        case type_t::number:
            context.out << *number_ptr(m_Storage);
            break;

        default:
            break;
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

        // Switch to C locale to make strtod and strtol work as expected
        auto previous_locale = std::setlocale(LC_NUMERIC, "C");

        // Accept single value only when end of the stream is reached.
        if (!accept_element(v) || !eof())
            v = value(type_t::discarded);

        if (previous_locale && strcmp(previous_locale, "C") != 0)
            std::setlocale(LC_NUMERIC, previous_locale);

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
        return accept_object(result)
            || accept_array(result)
            || accept_string(result)
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

    bool accept_characters(string& result)
    {
        int c;
        while (accept_character(c))
        {
            CRUDE_ASSERT(c < 128); // #todo: convert characters > 127 to UTF-8
            result.push_back(static_cast<char>(c));
        }

        return true;
    }

    bool accept_character(int& c)
    {
        auto s = state();

        if (accept('\\'))
        {
            return accept_escape(c);
        }
        else if (expect('\"'))
            return false;

        // #todo: Handle UTF-8 sequences.
        return s((c = peek()) >= 0) && advance();
    }

    bool accept_escape(int& c)
    {
        if (accept('\"')) { c = '\"'; return true; }
        if (accept('\\')) { c = '\\'; return true; }
        if (accept('/'))  { c = '/';  return true; }
        if (accept('b'))  { c = '\b'; return true; }
        if (accept('f'))  { c = '\f'; return true; }
        if (accept('n'))  { c = '\n'; return true; }
        if (accept('r'))  { c = '\r'; return true; }
        if (accept('t'))  { c = '\t'; return true; }

        auto s = state();

        string hex;
        hex.reserve(4);
        if (s(accept('u') && accept_hex(hex) && accept_hex(hex) && accept_hex(hex) && accept_hex(hex)))
        {
            char* end = nullptr;
            auto v = std::strtol(hex.c_str(), &end, 16);
            if (end != hex.c_str() + hex.size())
                return false;

            c = v;
            return true;
        }

        return false;
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
        if (s(accept_int(n) && accept_frac(n) && accept_exp(n)))
        {
            char* end = nullptr;
            auto v = std::strtod(n.c_str(), &end);
            if (end != n.c_str() + n.size())
                return false;

            if (v != 0 && !std::isnormal(v))
                return false;

            result = v;
            return true;
        }

        return false;
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
            return *m_Cursor;
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
};

value value::parse(const string& data)
{
    auto p = parser(data.c_str(), data.c_str() + data.size());

    auto v = p.parse();

    return v;
}

} // namespace crude_json
