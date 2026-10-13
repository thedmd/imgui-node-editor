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
# pragma once

# include <type_traits>
# include <string>
# include <vector>
# include <map>
# include <cstddef>
# include <new>
# include <sstream>

# ifndef CRUDE_ASSERT
#     include <cassert>
#     define CRUDE_ASSERT(expr) assert(expr)
# endif

# ifndef CRUDE_JSON_IO
#     define CRUDE_JSON_IO 1
# endif

// parse() rejects documents nested deeper. Values built in code may nest deeper, but
// dump(), copies and destruction recurse as well, so keep them shallow too.
# ifndef CRUDE_JSON_MAX_DEPTH
#     define CRUDE_JSON_MAX_DEPTH 512
# endif

// A program with another copy of crude_json can build this one into a namespace of its
// own, for the two to link together.
# ifndef CRUDE_JSON_NAMESPACE
#     define CRUDE_JSON_NAMESPACE crude_json
# endif

namespace CRUDE_JSON_NAMESPACE {

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
private:
    template <typename T>
    using if_integer = typename std::enable_if<std::is_integral<T>::value && !std::is_same<T, boolean>::value, int>::type;
    template <typename T>
    using if_not_char = typename std::enable_if<!std::is_same<typename std::remove_cv<T>::type, char>::value, int>::type;

public:
    value(type_t type = type_t::null): m_Type(construct(m_Storage, type)) {}
    // MSVC's std::map allocates when moved; running out of memory there terminates.
    value(value&& other) noexcept;
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
    // Integers are stored as numbers, exact up to 2^53.
    template <typename T, if_integer<T> = 0>
    value(      T        v): m_Type(construct(m_Storage, static_cast<number>(v))) {}
    template <typename T, if_not_char<T> = 0>
    value(      T*        ) = delete;
    ~value() { destruct(m_Storage, m_Type); }

    value& operator=(value&& other) noexcept { if (this != &other) { value(std::move(other)).swap(*this); } return *this; }
    value& operator=(const value& other)     { if (this != &other) { value(          other).swap(*this);  } return *this; }

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
    template <typename T, if_integer<T> = 0>
    value& operator=(      T        v) { auto other = value(          v);  swap(other); return *this; }
    template <typename T, if_not_char<T> = 0>
    value& operator=(      T*        ) = delete;

    type_t type() const { return m_Type; }

    explicit operator type_t() const { return m_Type; }

    // Mutable lookups add missing entries, turning null into an array or an object;
    // on another type they assert and return a value that isn't in the document.
    // Const lookups return null for a missing entry or another type.
          value& operator[](size_t index);
    const value& operator[](size_t index) const;
          value& operator[](const string& key);
    const value& operator[](const string& key) const;

    // Returns nullptr for a missing key, or when this isn't an object.
          value* find(const string& key);
    const value* find(const string& key) const;

    bool contains(const string& key) const;

    void push_back(const value& value);
    void push_back(value&& value);

    size_t erase(const string& key);

    bool is_primitive()  const { return is_string() || is_number() || is_boolean() || is_null(); }
    bool is_structured() const { return is_object() || is_array();   }
    bool is_null()       const { return m_Type == type_t::null;      }
    bool is_object()     const { return m_Type == type_t::object;    }
    bool is_array()      const { return m_Type == type_t::array;     }
    bool is_string()     const { return m_Type == type_t::string;    }
    bool is_boolean()    const { return m_Type == type_t::boolean;   }
    bool is_number()     const { return m_Type == type_t::number;    }
    bool is_discarded()  const { return m_Type == type_t::discarded; }

    // On another type, get() asserts and returns an empty value; writes to it are lost.
    template <typename T> const T& get() const;
    template <typename T>       T& get();

    template <typename T> const T* get_ptr() const;
    template <typename T>       T* get_ptr();

    string dump(const int indent = -1, const char indent_char = ' ') const;

    void swap(value& other) noexcept;

    inline friend void swap(value& lhs, value& rhs) noexcept { lhs.swap(rhs); }

    // Returns discarded value for invalid inputs.
    static value parse(const string& data);

# if CRUDE_JSON_IO
    // Paths are UTF-8, on Windows as well.
    // load() returns false when the file can't be read; a file that isn't JSON loads as a discarded value.
    // save() writes path + ".tmp", and renames it over the file.
    static std::pair<value, bool> load(const string& path);
    bool save(const string& path, const int indent = -1, const char indent_char = ' ') const;
# endif

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
    struct storage_t { alignas(max_align) unsigned char data[max_size]; };

    // A value lives in the storage, made there by placement new, and from C++17 a
    // pointer to it made from the storage's address needs std::launder().
# if defined(__cpp_lib_launder)
    template <typename T> static T* launder(T* pointer) { return std::launder(pointer); }
# else
    template <typename T> static T* launder(T* pointer) { return pointer; }
# endif

    static       object*   object_ptr(      storage_t& storage) { return launder(reinterpret_cast<       object*>(storage.data)); }
    static const object*   object_ptr(const storage_t& storage) { return launder(reinterpret_cast<const  object*>(storage.data)); }
    static       array*     array_ptr(      storage_t& storage) { return launder(reinterpret_cast<        array*>(storage.data)); }
    static const array*     array_ptr(const storage_t& storage) { return launder(reinterpret_cast<const   array*>(storage.data)); }
    static       string*   string_ptr(      storage_t& storage) { return launder(reinterpret_cast<       string*>(storage.data)); }
    static const string*   string_ptr(const storage_t& storage) { return launder(reinterpret_cast<const  string*>(storage.data)); }
    static       boolean* boolean_ptr(      storage_t& storage) { return launder(reinterpret_cast<      boolean*>(storage.data)); }
    static const boolean* boolean_ptr(const storage_t& storage) { return launder(reinterpret_cast<const boolean*>(storage.data)); }
    static       number*   number_ptr(      storage_t& storage) { return launder(reinterpret_cast<       number*>(storage.data)); }
    static const number*   number_ptr(const storage_t& storage) { return launder(reinterpret_cast<const  number*>(storage.data)); }

    template <typename T> static const T& empty()   { static const T s_Empty{}; return s_Empty; }
    template <typename T> static       T& scratch() { static thread_local T s_Scratch; s_Scratch = T(); return s_Scratch; }

    static type_t construct(storage_t& storage, type_t type)
    {
        switch (type)
        {
            case type_t::object:    new (storage.data) object();  break;
            case type_t::array:     new (storage.data) array();   break;
            case type_t::string:    new (storage.data) string();  break;
            case type_t::boolean:   new (storage.data) boolean(); break;
            case type_t::number:    new (storage.data) number();  break;
            default: break;
        }

        return type;
    }

    static type_t construct(storage_t& storage,       null)           { (void)storage;                                        return type_t::null;    }
    static type_t construct(storage_t& storage,       object&& value) { new (storage.data)  object(std::forward<object>(value));  return type_t::object;  }
    static type_t construct(storage_t& storage, const object&  value) { new (storage.data)  object(value);                        return type_t::object;  }
    static type_t construct(storage_t& storage,       array&&  value) { new (storage.data)   array(std::forward<array>(value));   return type_t::array;   }
    static type_t construct(storage_t& storage, const array&   value) { new (storage.data)   array(value);                        return type_t::array;   }
    static type_t construct(storage_t& storage,       string&& value) { new (storage.data)  string(std::forward<string>(value));  return type_t::string;  }
    static type_t construct(storage_t& storage, const string&  value) { new (storage.data)  string(value);                        return type_t::string;  }
    static type_t construct(storage_t& storage, const char*    value) { new (storage.data)  string(value);                        return type_t::string;  }
    static type_t construct(storage_t& storage,       boolean  value) { new (storage.data) boolean(value);                        return type_t::boolean; }
    static type_t construct(storage_t& storage,       number   value) { new (storage.data)  number(value);                        return type_t::number;  }

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

template <> inline const object&  value::get<object>()  const { CRUDE_ASSERT(m_Type == type_t::object);  return m_Type == type_t::object  ? *object_ptr(m_Storage)  : empty<object>();    }
template <> inline const array&   value::get<array>()   const { CRUDE_ASSERT(m_Type == type_t::array);   return m_Type == type_t::array   ? *array_ptr(m_Storage)   : empty<array>();     }
template <> inline const string&  value::get<string>()  const { CRUDE_ASSERT(m_Type == type_t::string);  return m_Type == type_t::string  ? *string_ptr(m_Storage)  : empty<string>();    }
template <> inline const boolean& value::get<boolean>() const { CRUDE_ASSERT(m_Type == type_t::boolean); return m_Type == type_t::boolean ? *boolean_ptr(m_Storage) : empty<boolean>();   }
template <> inline const number&  value::get<number>()  const { CRUDE_ASSERT(m_Type == type_t::number);  return m_Type == type_t::number  ? *number_ptr(m_Storage)  : empty<number>();    }

template <> inline       object&  value::get<object>()        { CRUDE_ASSERT(m_Type == type_t::object);  return m_Type == type_t::object  ? *object_ptr(m_Storage)  : scratch<object>();  }
template <> inline       array&   value::get<array>()         { CRUDE_ASSERT(m_Type == type_t::array);   return m_Type == type_t::array   ? *array_ptr(m_Storage)   : scratch<array>();   }
template <> inline       string&  value::get<string>()        { CRUDE_ASSERT(m_Type == type_t::string);  return m_Type == type_t::string  ? *string_ptr(m_Storage)  : scratch<string>();  }
template <> inline       boolean& value::get<boolean>()       { CRUDE_ASSERT(m_Type == type_t::boolean); return m_Type == type_t::boolean ? *boolean_ptr(m_Storage) : scratch<boolean>(); }
template <> inline       number&  value::get<number>()        { CRUDE_ASSERT(m_Type == type_t::number);  return m_Type == type_t::number  ? *number_ptr(m_Storage)  : scratch<number>();  }

template <> inline const object*  value::get_ptr<object>()  const { if (m_Type == type_t::object)  return object_ptr(m_Storage);  else return nullptr; }
template <> inline const array*   value::get_ptr<array>()   const { if (m_Type == type_t::array)   return array_ptr(m_Storage);   else return nullptr; }
template <> inline const string*  value::get_ptr<string>()  const { if (m_Type == type_t::string)  return string_ptr(m_Storage);  else return nullptr; }
template <> inline const boolean* value::get_ptr<boolean>() const { if (m_Type == type_t::boolean) return boolean_ptr(m_Storage); else return nullptr; }
template <> inline const number*  value::get_ptr<number>()  const { if (m_Type == type_t::number)  return number_ptr(m_Storage);  else return nullptr; }

template <> inline       object*  value::get_ptr<object>()        { if (m_Type == type_t::object)  return object_ptr(m_Storage);  else return nullptr; }
template <> inline       array*   value::get_ptr<array>()         { if (m_Type == type_t::array)   return array_ptr(m_Storage);   else return nullptr; }
template <> inline       string*  value::get_ptr<string>()        { if (m_Type == type_t::string)  return string_ptr(m_Storage);  else return nullptr; }
template <> inline       boolean* value::get_ptr<boolean>()       { if (m_Type == type_t::boolean) return boolean_ptr(m_Storage); else return nullptr; }
template <> inline       number*  value::get_ptr<number>()        { if (m_Type == type_t::number)  return number_ptr(m_Storage);  else return nullptr; }

} // namespace CRUDE_JSON_NAMESPACE