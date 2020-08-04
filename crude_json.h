// Crude implementation of JSON value object and parser.
//
// LICENSE
//   This software is dual-licensed to the public domain and under the following
//   license: you are granted a perpetual, irrevocable license to copy, modify,
//   publish, and distribute this file as you see fit.
//
// CREDITS
//   Written by Michal Cichon
# ifndef __CRUDE_JSON_H__
# define __CRUDE_JSON_H__
# pragma once

# include <type_traits>
# include <string>
# include <vector>
# include <map>
# include <cstddef>
# include <algorithm>
# include <sstream>

# ifndef CRUDE_ASSERT
#     include <cassert>
#     define CRUDE_ASSERT(expr) assert(expr)
# endif

namespace crude_json {

struct value;

using string  = std::string;
using object  = std::map<string, value>;
using array   = std::vector<value>;
using number  = double;
using boolean = bool;
using null    = std::nullptr_t;

enum class type_t
{
    null,
    object,
    array,
    string,
    boolean,
    number,
    discarded
};

struct value
{
    value(type_t type = type_t::null): m_Type(construct(m_Storage, type)) {}
    value(value&& other);
    value(const value& other);

    value(      null)      : m_Type(construct(m_Storage,      null()))  {}
    value(      object&& v): m_Type(construct(m_Storage, std::move(v))) {}
    value(const object&  v): m_Type(construct(m_Storage,           v))  {}
    value(      array&&  v): m_Type(construct(m_Storage, std::move(v))) {}
    value(const array&   v): m_Type(construct(m_Storage,           v))  {}
    value(      string&& v): m_Type(construct(m_Storage, std::move(v))) {}
    value(const string&  v): m_Type(construct(m_Storage,           v))  {}
    value(const char*    v): m_Type(construct(m_Storage,           v))  {}
    value(      boolean  v): m_Type(construct(m_Storage,           v))  {}
    value(      number   v): m_Type(construct(m_Storage,           v))  {}
    ~value() { destruct(m_Storage, m_Type); }

    value& operator=(value&& other)      { if (this != &other) { value(std::move(other)).swap(*this); } return *this; }
    value& operator=(const value& other) { if (this != &other) { value(          other).swap(*this);  } return *this; }

    value& operator=(      null)       { auto other = value(           );  swap(other); return *this; }
    value& operator=(      object&& v) { auto other = value(std::move(v)); swap(other); return *this; }
    value& operator=(const object&  v) { auto other = value(          v);  swap(other); return *this; }
    value& operator=(      array&&  v) { auto other = value(std::move(v)); swap(other); return *this; }
    value& operator=(const array&   v) { auto other = value(          v);  swap(other); return *this; }
    value& operator=(      string&& v) { auto other = value(std::move(v)); swap(other); return *this; }
    value& operator=(const string&  v) { auto other = value(          v);  swap(other); return *this; }
    value& operator=(const char*    v) { auto other = value(          v);  swap(other); return *this; }
    value& operator=(      boolean  v) { auto other = value(          v);  swap(other); return *this; }
    value& operator=(      number   v) { auto other = value(          v);  swap(other); return *this; }

    type_t type() const { return m_Type; }

    operator type_t() const { return m_Type; }

          value& operator[](size_t index);
    const value& operator[](size_t index) const;
          value& operator[](const string& key);
    const value& operator[](const string& key) const;

    bool contains(const string& key) const;

    void push_back(const value& value);
    void push_back(value&& value);

    bool is_primitive()  const { return is_string() || is_number() || is_boolean() || is_null(); }
    bool is_structured() const { return is_object() || is_array();   }
    bool is_null()       const { return m_Type == type_t::null;      }
    bool is_object()     const { return m_Type == type_t::object;    }
    bool is_array()      const { return m_Type == type_t::array;     }
    bool is_string()     const { return m_Type == type_t::string;    }
    bool is_boolean()    const { return m_Type == type_t::boolean;   }
    bool is_number()     const { return m_Type == type_t::number;    }
    bool is_discarded()  const { return m_Type == type_t::discarded; }

    template <typename T> const T& get() const;
    template <typename T>       T& get();

    string dump(const int indent = -1, const char indent_char = ' ') const;

    void swap(value& other);

    inline friend void swap(value& lhs, value& rhs) { lhs.swap(rhs); }

    // Returns discarded value for invalid inputs.
    static value parse(const string& data);

private:
    struct parser;

    // VS2015: std::max() is not constexpr yet.
# define CRUDE_MAX2(a, b)           ((a) < (b) ? (b) : (a))
# define CRUDE_MAX3(a, b, c)        CRUDE_MAX2(CRUDE_MAX2(a, b), c)
# define CRUDE_MAX4(a, b, c, d)     CRUDE_MAX2(CRUDE_MAX3(a, b, c), d)
# define CRUDE_MAX5(a, b, c, d, e)  CRUDE_MAX2(CRUDE_MAX4(a, b, c, d), e)
    enum
    {
        max_size  = CRUDE_MAX5( sizeof(string),  sizeof(object),  sizeof(array),  sizeof(number),  sizeof(boolean)),
        max_align = CRUDE_MAX5(alignof(string), alignof(object), alignof(array), alignof(number), alignof(boolean))
    };
# undef CRUDE_MAX5
# undef CRUDE_MAX4
# undef CRUDE_MAX3
# undef CRUDE_MAX2
    using storage_t = std::aligned_storage<max_size, max_align>::type;

    static       object*   object_ptr(      storage_t& storage) { return reinterpret_cast<       object*>(&storage); }
    static const object*   object_ptr(const storage_t& storage) { return reinterpret_cast<const  object*>(&storage); }
    static       array*     array_ptr(      storage_t& storage) { return reinterpret_cast<        array*>(&storage); }
    static const array*     array_ptr(const storage_t& storage) { return reinterpret_cast<const   array*>(&storage); }
    static       string*   string_ptr(      storage_t& storage) { return reinterpret_cast<       string*>(&storage); }
    static const string*   string_ptr(const storage_t& storage) { return reinterpret_cast<const  string*>(&storage); }
    static       boolean* boolean_ptr(      storage_t& storage) { return reinterpret_cast<      boolean*>(&storage); }
    static const boolean* boolean_ptr(const storage_t& storage) { return reinterpret_cast<const boolean*>(&storage); }
    static       number*   number_ptr(      storage_t& storage) { return reinterpret_cast<       number*>(&storage); }
    static const number*   number_ptr(const storage_t& storage) { return reinterpret_cast<const  number*>(&storage); }

    static type_t construct(storage_t& storage, type_t type)
    {
        switch (type)
        {
            case type_t::object:    new (&storage) object();  break;
            case type_t::array:     new (&storage) array();   break;
            case type_t::string:    new (&storage) string();  break;
            case type_t::boolean:   new (&storage) boolean(); break;
            case type_t::number:    new (&storage) number();  break;
            default: break;
        }

        return type;
    }

    static type_t construct(storage_t& storage,       null)           { (void)storage;                                        return type_t::null;    }
    static type_t construct(storage_t& storage,       object&& value) { new (&storage)  object(std::forward<object>(value));  return type_t::object;  }
    static type_t construct(storage_t& storage, const object&  value) { new (&storage)  object(value);                        return type_t::object;  }
    static type_t construct(storage_t& storage,       array&&  value) { new (&storage)   array(std::forward<array>(value));   return type_t::array;   }
    static type_t construct(storage_t& storage, const array&   value) { new (&storage)   array(value);                        return type_t::array;   }
    static type_t construct(storage_t& storage,       string&& value) { new (&storage)  string(std::forward<string>(value));  return type_t::string;  }
    static type_t construct(storage_t& storage, const string&  value) { new (&storage)  string(value);                        return type_t::string;  }
    static type_t construct(storage_t& storage, const char*    value) { new (&storage)  string(value);                        return type_t::string;  }
    static type_t construct(storage_t& storage,       boolean  value) { new (&storage) boolean(value);                        return type_t::boolean; }
    static type_t construct(storage_t& storage,       number   value) { new (&storage)  number(value);                        return type_t::number;  }

    static void destruct(storage_t& storage, type_t type)
    {
        switch (type)
        {
            case type_t::object: object_ptr(storage)->~object(); break;
            case type_t::array:   array_ptr(storage)->~array();  break;
            case type_t::string: string_ptr(storage)->~string(); break;
            default: break;
        }
    }

    struct dump_context_t
    {
        std::ostringstream out;
        const int  indent = -1;
        const char indent_char = ' ';

        // VS2015: Aggregate initialization isn't a thing yet.
        dump_context_t(const int indent, const char indent_char)
            : indent(indent)
            , indent_char(indent_char)
        {
        }

        void write_indent(int level);
        void write_separator();
        void write_newline();
    };

    void dump(dump_context_t& context, int level) const;

    storage_t m_Storage;
    type_t    m_Type;
};

template <> inline const object&  value::get<object>()  const { CRUDE_ASSERT(m_Type == type_t::object);  return *object_ptr(m_Storage);  }
template <> inline const array&   value::get<array>()   const { CRUDE_ASSERT(m_Type == type_t::array);   return *array_ptr(m_Storage);   }
template <> inline const string&  value::get<string>()  const { CRUDE_ASSERT(m_Type == type_t::string);  return *string_ptr(m_Storage);  }
template <> inline const boolean& value::get<boolean>() const { CRUDE_ASSERT(m_Type == type_t::boolean); return *boolean_ptr(m_Storage); }
template <> inline const number&  value::get<number>()  const { CRUDE_ASSERT(m_Type == type_t::number);  return *number_ptr(m_Storage);  }

template <> inline       object&  value::get<object>()        { CRUDE_ASSERT(m_Type == type_t::object);  return *object_ptr(m_Storage);  }
template <> inline       array&   value::get<array>()         { CRUDE_ASSERT(m_Type == type_t::array);   return *array_ptr(m_Storage);   }
template <> inline       string&  value::get<string>()        { CRUDE_ASSERT(m_Type == type_t::string);  return *string_ptr(m_Storage);  }
template <> inline       boolean& value::get<boolean>()       { CRUDE_ASSERT(m_Type == type_t::boolean); return *boolean_ptr(m_Storage); }
template <> inline       number&  value::get<number>()        { CRUDE_ASSERT(m_Type == type_t::number);  return *number_ptr(m_Storage);  }


} // namespace crude_json

# endif // __CRUDE_JSON_H__