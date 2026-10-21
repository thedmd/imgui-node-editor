//------------------------------------------------------------------------------
// crude_blueprint: blueprints2-example's runtime.
//
// Value types, node types and graphs whose pin types follow their links,
// compiled into programs that contexts run, step and inspect.
//------------------------------------------------------------------------------
# pragma once
# include <concepts>
# include <cstddef>
# include <cstdint>
# include <optional>
# include <string>
# include <string_view>
# include <type_traits>
# include <utility>
# include <variant>

# ifndef CRUDE_BP_ASSERT
#     include <cassert>
#     define CRUDE_BP_ASSERT(_EXPR) assert(_EXPR)
# endif

namespace crude_blueprint {

// Graphs, nodes, pins and links of a blueprint share one space of IDs.
using Id = uint32_t;


//
// -------[ RTTI ]-------
//

// Identifies a class at run time. Never saved: class names aren't stable.
struct TypeInfo
{
    std::string_view m_Name;
    const TypeInfo*  m_Base = nullptr;

    bool IsA(const TypeInfo& other) const
    {
        for (auto info = this; info; info = info->m_Base)
            if (info == &other)
                return true;
        return false;
    }
};

# define CRUDE_BP_RTTI_ROOT(Class) \
    static const ::crude_blueprint::TypeInfo& StaticTypeInfo() \
    { \
        static const ::crude_blueprint::TypeInfo info{ #Class, nullptr }; \
        return info; \
    } \
    virtual const ::crude_blueprint::TypeInfo& GetTypeInfo() const { return StaticTypeInfo(); }

# define CRUDE_BP_RTTI(Class, Base) \
    static const ::crude_blueprint::TypeInfo& StaticTypeInfo() \
    { \
        static const ::crude_blueprint::TypeInfo info{ #Class, &Base::StaticTypeInfo() }; \
        return info; \
    } \
    const ::crude_blueprint::TypeInfo& GetTypeInfo() const override { return StaticTypeInfo(); }

template <typename T, typename U>
bool IsA(const U* object)
{
    return object && object->GetTypeInfo().IsA(T::StaticTypeInfo());
}

template <typename T, typename U>
auto Cast(U* object) -> std::conditional_t<std::is_const_v<U>, const T*, T*>
{
    using Pointer = std::conditional_t<std::is_const_v<U>, const T*, T*>;
    return IsA<T>(object) ? static_cast<Pointer>(object) : nullptr;
}


//
// -------[ Boxes ]-------
//

// Shared and immutable: strings, arrays, maps and structs held by values, and messages held by errors.
struct Box
{
    CRUDE_BP_RTTI_ROOT(Box)

    Box() = default;
    Box(const Box&) = delete;
    Box& operator=(const Box&) = delete;
    virtual ~Box() = default;

private:
    template <typename T> friend class Ref;

    mutable size_t m_References = 0;
};

// A counted reference to a box. Not thread-safe.
template <typename T>
class Ref
{
public:
    Ref() = default;
    Ref(std::nullptr_t) {}
    explicit Ref(T* box): m_Box(box) { Acquire(); }
    Ref(const Ref& other): m_Box(other.m_Box) { Acquire(); }
    Ref(Ref&& other) noexcept: m_Box(std::exchange(other.m_Box, nullptr)) {}
    template <typename U> requires std::is_convertible_v<U*, T*>
    Ref(const Ref<U>& other): m_Box(other.Get()) { Acquire(); }
    ~Ref() { Release(); }

    Ref& operator=(Ref other) noexcept
    {
        std::swap(m_Box, other.m_Box);
        return *this;
    }

    T* Get() const        { return m_Box; }
    T* operator->() const { return m_Box; }
    T& operator*() const  { return *m_Box; }
    explicit operator bool() const { return m_Box != nullptr; }

    size_t References() const { return m_Box ? Base()->m_References : 0; }

    bool operator==(const Ref& other) const { return m_Box == other.m_Box; }

private:
    const Box* Base() const { return m_Box; }

    void Acquire()
    {
        if (m_Box)
            ++Base()->m_References;
    }

    void Release()
    {
        if (m_Box && --Base()->m_References == 0)
            delete Base();
    }

    T* m_Box = nullptr;
};

template <typename T, typename... Args>
Ref<T> MakeRef(Args&&... args)
{
    return Ref<T>(new T(std::forward<Args>(args)...));
}

struct StringBox final : Box
{
    CRUDE_BP_RTTI(StringBox, Box)

    explicit StringBox(std::string text): m_Text(std::move(text)) {}

    const std::string m_Text;
};


//
// -------[ Errors ]-------
//

// Open: categories of the application's errors start at 1000.
struct ErrorCategory
{
    uint16_t m_Value = 0;

    bool operator==(const ErrorCategory& other) const = default;

    static const ErrorCategory Blueprint;
    static const ErrorCategory Library;
};

inline constexpr ErrorCategory ErrorCategory::Blueprint{ 0 };
inline constexpr ErrorCategory ErrorCategory::Library{ 1 };

enum class BlueprintError : uint16_t
{
    Threw = 1,      // node or extension code threw
    WrongType,      // a value, or a value in a file, of another type
    OutOfRange,     // a number outside its type's range
    NotFinite,      // NaN or an infinity, which files can't hold
    Duplicate,      // a name registered twice, a key twice in a file
    UnknownType,    // a type name nothing registered
    BadTypeText,    // type text that doesn't parse
    CastFailed,     // a cast that may fail, failed
};

constexpr ErrorCategory CategoryOf(BlueprintError) { return ErrorCategory::Blueprint; }

// An enum whose category a CategoryOf() overload gives, found by ADL.
template <typename Code>
concept ErrorCode = std::is_enum_v<Code> && requires (Code code) { { CategoryOf(code) } -> std::same_as<ErrorCategory>; };

// The message is out of line, so an error without one allocates nothing.
struct Error
{
    ErrorCategory        m_Category;
    uint16_t             m_Code = 0;
    Ref<const StringBox> m_Message;

    Error() = default;
    Error(ErrorCategory category, uint16_t code, std::string_view message = {});

    template <ErrorCode Code>
    Error(Code code, std::string_view message = {})
        : Error(CategoryOf(code), static_cast<uint16_t>(code), message)
    {
    }

    std::string_view Message() const { return m_Message ? std::string_view(m_Message->m_Text) : std::string_view(); }

    template <ErrorCode Code>
    bool Is(Code code) const { return m_Category == CategoryOf(code) && m_Code == static_cast<uint16_t>(code); }

    // The message doesn't count.
    bool operator==(const Error& other) const { return m_Category == other.m_Category && m_Code == other.m_Code; }
};

template <typename T>
class [[nodiscard]] Result
{
    static_assert(!std::is_same_v<std::remove_cv_t<T>, Error>, "a Result holds a value or an Error");

public:
    Result(const T& value): m_Value(std::in_place_index<0>, value) {}
    Result(T&& value): m_Value(std::in_place_index<0>, std::move(value)) {}
    Result(Error error): m_Value(std::in_place_index<1>, std::move(error)) {}
    template <ErrorCode Code>
    Result(Code code): m_Value(std::in_place_index<1>, code) {}

    explicit operator bool() const { return IsOk(); }
    bool IsOk() const    { return m_Value.index() == 0; }
    bool IsError() const { return m_Value.index() == 1; }

    T&       GetValue() &       { CRUDE_BP_ASSERT(IsOk()); return std::get<0>(m_Value); }
    const T& GetValue() const & { CRUDE_BP_ASSERT(IsOk()); return std::get<0>(m_Value); }
    T&&      GetValue() &&      { CRUDE_BP_ASSERT(IsOk()); return std::get<0>(std::move(m_Value)); }

    const Error& GetError() const { CRUDE_BP_ASSERT(IsError()); return std::get<1>(m_Value); }

    template <typename U>
    T ValueOr(U&& fallback) const & { return IsOk() ? std::get<0>(m_Value) : static_cast<T>(std::forward<U>(fallback)); }

private:
    std::variant<T, Error> m_Value;
};

template <>
class [[nodiscard]] Result<void>
{
public:
    Result() = default;
    Result(Error error): m_Error(std::move(error)) {}
    template <ErrorCode Code>
    Result(Code code): m_Error(Error(code)) {}

    explicit operator bool() const { return IsOk(); }
    bool IsOk() const    { return !m_Error.has_value(); }
    bool IsError() const { return m_Error.has_value(); }

    const Error& GetError() const { CRUDE_BP_ASSERT(IsError()); return *m_Error; }

private:
    std::optional<Error> m_Error;
};

enum class Severity : uint8_t { Error, Warning, Info };

// An error, how bad it is, and where it is; an ID that doesn't apply is 0.
struct Diagnostic
{
    Error    m_Error;
    Severity m_Severity = Severity::Error;
    Id       m_Graph = 0;
    Id       m_Node  = 0;
    Id       m_Pin   = 0;
    Id       m_Link  = 0;
};

} // namespace crude_blueprint
