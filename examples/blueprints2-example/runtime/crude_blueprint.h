//------------------------------------------------------------------------------
// crude_blueprint: blueprints2-example's runtime.
//
// Value types, node types and graphs whose pin types follow their links,
// compiled into programs that contexts run, step and inspect.
//------------------------------------------------------------------------------
# pragma once
# include <any>
# include <chrono>
# include <concepts>
# include <cstddef>
# include <cstdint>
# include <functional>
# include <map>
# include <memory>
# include <optional>
# include <span>
# include <string>
# include <string_view>
# include <type_traits>
# include <unordered_map>
# include <utility>
# include <variant>
# include <vector>

# ifndef CRUDE_BP_ASSERT
#     include <cassert>
#     define CRUDE_BP_ASSERT(_EXPR) assert(_EXPR)
# endif

namespace crude_json {
struct value;
} // namespace crude_json

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
    OutOfRange,     // a number outside its range: a type's, a group's
    NotFinite,      // NaN or an infinity, which files can't hold
    Duplicate,      // a name registered twice, a key twice in a file
    UnknownType,    // a type name nothing registered
    BadTypeText,    // type text that doesn't parse
    CastFailed,     // a cast that may fail, failed
    NotFound,       // an ID or a name that nothing has
    BadLink,        // a link that can't be, such as from an input
    TypeMismatch,   // types that don't match, or a type that a type variable doesn't take
    Undecided,      // a type that nothing decided
    NoCast,         // a cast that the registry doesn't have
    NoPin,          // a link to a pin that isn't on its node now
    Unsettled,      // types that keep changing a node's pins
    NeedsLink,      // a required or recommended pin without a link
    Loop,           // a link that closes a loop of pure nodes
    Unsupported,    // what a node or a fiber can't do now, such as a node type that doesn't run
    BadFile,        // a file that isn't a blueprint this runtime reads
    TooDeep,        // pure nodes computed in the inputs of others, nested deeper than the runtime goes
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


//
// -------[ Types ]-------
//

struct ValueType;
class Registry;

// A value type, with its arguments when it's a type constructor: Int32, Array<Int32>,
// Map<String,Int32>. There is one of each, so types compare by address.
struct Type
{
    const ValueType*         m_Head = nullptr;
    std::vector<const Type*> m_Args;
    std::string              m_Name;
};

// Interns constructed types. A value type that isn't a type constructor is its own type.
class TypeTable
{
public:
    const Type* Get(const ValueType& head, std::span<const Type* const> args = {});

    // Type text, as files hold it: "Int32", "Map<String,Array<Int32>>".
    Result<const Type*> Parse(const Registry& registry, std::string_view text);

private:
    std::map<std::pair<const ValueType*, std::vector<const Type*>>, std::unique_ptr<Type>> m_Types;
};


//
// -------[ Values ]-------
//

// 24 bytes. Strings, arrays, maps, structs and the application's values are boxed.
class Value
{
public:
    // No type: a placeholder, which nothing hands out as a value.
    Value() = default;

    static Value Bool(bool value);
    static Value Int32(int32_t value);
    static Value Float(float value);
    static Value String(std::string text);
    static Value Array(const Type& type, std::vector<Value> items);
    // Sorted by key; of equal keys, the last one stays.
    static Value Map(const Type& type, std::vector<std::pair<Value, Value>> entries);
    // In the struct's order of fields.
    static Value Struct(const Type& type, std::vector<Value> fields);
    static Value Boxed(const Type& type, Ref<const Box> box);

    const Type* GetType() const { return m_Type; }
    bool        IsValid() const { return m_Type != nullptr; }

    bool                                     AsBool() const;
    int32_t                                  AsInt32() const;
    float                                    AsFloat() const;
    std::string_view                         AsString() const;
    std::span<const Value>                   AsArray() const;
    std::span<const std::pair<Value, Value>> AsMap() const;
    std::span<const Value>                   AsStruct() const;
    const Box*                               AsBox() const;

    // A map's value for a key, or null.
    const Value* Find(const Value& key) const;

    bool operator==(const Value& other) const;

private:
    using Data = std::variant<bool, int32_t, float, Ref<const Box>>;

    Value(const Type& type, Data data): m_Type(&type), m_Data(std::move(data)) {}

    const Type* m_Type = nullptr;
    Data        m_Data;
};


//
// -------[ Value types ]-------
//

struct ValueType
{
    CRUDE_BP_RTTI_ROOT(ValueType)

    explicit ValueType(std::string name, int arity = 0);
    ValueType(const ValueType&) = delete;
    ValueType& operator=(const ValueType&) = delete;
    virtual ~ValueType() = default;

    const std::string m_Name;   // saved
    const int         m_Arity;  // > 0: a type constructor, such as Array with 1

    const Type& AsType() const { return m_Self; }

    virtual Value             Default(const Type& type) const = 0;
    virtual Result<Value>     Load(const Type& type, const crude_json::value& json) const = 0;
    virtual crude_json::value Save(const Value& value) const = 0;
    virtual std::string       Format(const Value& value) const = 0;
    virtual bool              Equal(const Value& a, const Value& b) const = 0;

    // A type constructor's arguments, which it may refuse.
    virtual Result<void> CheckArgs(std::span<const Type* const> args) const;

    // Map keys: values with an order.
    virtual bool IsKey() const { return false; }
    virtual int  Compare(const Value& a, const Value& b) const;

private:
    const Type m_Self;
};

const ValueType& BoolValueType();
const ValueType& Int32ValueType();
const ValueType& FloatValueType();
const ValueType& StringValueType();
const ValueType& ArrayValueType();  // Array<T>
const ValueType& MapValueType();    // Map<K, V>, with K a key

inline const Type& BoolType()   { return BoolValueType().AsType(); }
inline const Type& Int32Type()  { return Int32ValueType().AsType(); }
inline const Type& FloatType()  { return FloatValueType().AsType(); }
inline const Type& StringType() { return StringValueType().AsType(); }

struct StructField
{
    std::string m_Name;
    const Type* m_Type = nullptr;
};

// In files, a struct is an object: fields it lacks take their defaults, and fields it has
// that the struct doesn't are ignored, so documents survive changes to the struct.
struct StructType final : ValueType
{
    CRUDE_BP_RTTI(StructType, ValueType)

    StructType(std::string name, std::vector<StructField> fields);

    std::span<const StructField> Fields() const { return m_Fields; }
    int FindField(std::string_view name) const; // -1 for none

    Value             Default(const Type& type) const override;
    Result<Value>     Load(const Type& type, const crude_json::value& json) const override;
    crude_json::value Save(const Value& value) const override;
    std::string       Format(const Value& value) const override;
    bool              Equal(const Value& a, const Value& b) const override;

private:
    const std::vector<StructField> m_Fields;
};


//
// -------[ Casts ]-------
//

// Converts a value of one type into another. Applied only where the user asks for it.
struct CastInfo
{
    const Type*                                m_From    = nullptr;
    const Type*                                m_To      = nullptr;
    bool                                       m_MayFail = false;
    std::function<Result<Value>(const Value&)> m_Function;

    // As files name it: "Int32->Float".
    std::string Name() const { return m_From->m_Name + "->" + m_To->m_Name; }
};


//
// -------[ Node types ]-------
//

enum class Direction : uint8_t { Input, Output };

// Without a link, an input uses its default, and an output goes unread.
enum class Requirement : uint8_t
{
    Optional,
    Recommended,    // a warning without a link
    Required,       // an error without a link, so the blueprint can't run
};

// One of a node's type variables, as its declaration made it.
struct TypeVariable
{
    int m_Index = -1;
};

// A pin's type as a node declares it: a type, a type variable, or a type constructor applied to
// these, such as Array<T>.
struct TypeTerm
{
    const ValueType*      m_Head     = nullptr; // null: the variable
    int                   m_Variable = -1;
    std::vector<TypeTerm> m_Args;

    TypeTerm() = default;
    TypeTerm(const Type& type);
    TypeTerm(TypeVariable variable): m_Variable(variable.m_Index) {}
    TypeTerm(const ValueType& head, std::vector<TypeTerm> args);
};

struct VariableDeclaration
{
    std::string                   m_Name;
    std::vector<const ValueType*> m_Allowed; // the heads it may take, Array for any Array<T>; empty: any
};

struct PinDeclaration
{
    std::string m_Key;                       // stable and saved: "a", "in.2", "pos.x"
    std::string m_Name;                      // shown on the node
    Direction   m_Direction   = Direction::Input;
    bool        m_Flow        = false;
    TypeTerm    m_Type;                      // a data pin's
    Value       m_Default;                   // none: its type's
    Requirement m_Requirement = Requirement::Optional;
};

struct PinOptions
{
    Value       m_Default;
    Requirement m_Requirement = Requirement::Optional;
};

// Pins the user adds and removes. Each index has a pin per prefix, keyed "<prefix>.<index>".
struct GroupDeclaration
{
    std::string              m_Name;
    std::vector<std::string> m_Prefixes;
    int                      m_Min = 0;
    int                      m_Max = 0;
};

struct NodeConfig;

// The config's count of indices, within the group's bounds; without one, the minimum.
int GroupCount(const GroupDeclaration& group, const NodeConfig& config);
// The group's index a key is of: 2 for "in.2", and for "in.2.x", a field of it split; -1 for none.
int GroupIndex(const GroupDeclaration& group, std::string_view key);

// What a node instance's pins depend on, besides its type variables: saved, and changed by edits.
struct NodeConfig
{
    std::map<std::string, int, std::less<>>         m_Groups; // indices per group
    std::map<std::string, const Type*, std::less<>> m_Types;  // the type variables the user chose
    std::vector<std::string>                        m_Split;  // struct pins split into their fields, by key
    std::map<std::string, Value, std::less<>>       m_Data;   // the node type's own, such as a Constant's value

    bool operator==(const NodeConfig& other) const = default;
};

// What a node's type variables resolved to in the graph's previous round. Undecided ones are absent.
struct Bindings
{
    std::map<std::string, const Type*, std::less<>> m_Types;

    const Type* Get(std::string_view variable) const;

    bool operator==(const Bindings& other) const = default;
};

class NodeDeclaration
{
public:
    explicit NodeDeclaration(const NodeConfig& config): m_Config(config) {}

    TypeVariable Variable(std::string name, std::vector<const ValueType*> allowed = {});

    void Input(std::string key, std::string name, TypeTerm type, PinOptions options = {});
    void Output(std::string key, std::string name, TypeTerm type, PinOptions options = {});
    void FlowInput(std::string key, std::string name = {}, PinOptions options = {});
    void FlowOutput(std::string key, std::string name = {}, PinOptions options = {});

    // How many indices the config gives the group, within min and max. The node declares their pins.
    // The prefixes are the group's name when none are given.
    int Group(std::string name, int min, int max, std::vector<std::string> prefixes = {});

    std::span<const VariableDeclaration> Variables() const { return m_Variables; }
    std::span<const PinDeclaration>      Pins() const      { return m_Pins; }
    std::span<const GroupDeclaration>    Groups() const    { return m_Groups; }

private:
    friend class Graph;

    void AddPin(PinDeclaration pin);

    const NodeConfig&                m_Config;
    std::vector<VariableDeclaration> m_Variables;
    std::vector<PinDeclaration>      m_Pins;
    std::vector<GroupDeclaration>    m_Groups;
};

class EvalContext;
class ExecContext;

// Where a fiber goes once a node ran.
struct Exit
{
    enum class Kind : uint8_t
    {
        None, // the branch ends
        To,   // on at a flow output
        Loop, // the branch from a flow output, then this node again, at the same flow input
    };

    Kind        m_Kind = Kind::None;
    std::string m_Pin;

    static Exit None()                { return {}; }
    static Exit To(std::string pin)   { return { Kind::To, std::move(pin) }; }
    static Exit Loop(std::string pin) { return { Kind::Loop, std::move(pin) }; }
};

// One registered type can be many node types: Make for each struct, a call for each function.
struct NodeType
{
    CRUDE_BP_RTTI_ROOT(NodeType)

    NodeType(std::string name, std::string title, std::string category = {}, uint32_t version = 1);
    NodeType(const NodeType&) = delete;
    NodeType& operator=(const NodeType&) = delete;
    virtual ~NodeType() = default;

    const std::string m_Name;     // registered and saved: "crude.Add"
    const std::string m_Title;    // shown: "Add"
    const std::string m_Category; // "Math"
    const uint32_t    m_Version;  // of what files save of its nodes; see Upgrade()

    virtual void Declare(NodeDeclaration& declaration, const NodeConfig& config, const Bindings& bindings) const = 0;

    // A node with flow pins runs from the flow input it was entered at, and a pure one computes its
    // outputs from its inputs. Both give Unsupported unless the node type has them.
    virtual Result<Exit> Execute(ExecContext& context, std::string_view entry) const;
    virtual Result<void> Evaluate(EvalContext& context) const;

    // An event starts fibers, by its name: "Start". Other nodes have none.
    virtual std::string EventName(const NodeConfig& config) const;

    // Makes a node that an older version of this node type saved, as a file holds it, one this
    // version reads. Unsupported unless the node type has it.
    virtual Result<void> Upgrade(crude_json::value& node, uint32_t version) const;
};


//
// -------[ Registry ]-------
//

// Value types, casts and node types. One per application, shared by its blueprints: everything
// is registered at start-up, and nothing is ever unregistered.
class Registry
{
public:
    // A type the caller keeps, as the built-in ones are kept.
    Result<void> AddValueType(const ValueType& type);
    Result<void> AddValueType(std::unique_ptr<ValueType> type);
    Result<void> AddCast(CastInfo cast);
    Result<void> AddNodeType(const NodeType& type);
    Result<void> AddNodeType(std::unique_ptr<NodeType> type);

    const ValueType*                 FindValueType(std::string_view name) const;
    std::vector<const ValueType*>    ValueTypes() const; // in the order of their names
    const CastInfo*                  FindCast(const Type* from, const Type* to) const;
    std::span<const CastInfo* const> CastsFrom(const Type* from) const;
    const NodeType*                  FindNodeType(std::string_view name) const;
    std::vector<const NodeType*>     NodeTypes() const; // in the order of their names

private:
    std::map<std::string, const ValueType*, std::less<>> m_ValueTypes;
    std::vector<std::unique_ptr<ValueType>>              m_OwnedValueTypes;
    std::vector<std::unique_ptr<CastInfo>>               m_Casts;
    std::map<const Type*, std::vector<const CastInfo*>>  m_CastsFrom;
    std::map<std::string, const NodeType*, std::less<>>  m_NodeTypes;
    std::vector<std::unique_ptr<NodeType>>               m_OwnedNodeTypes;
};

// The built-in value types, and the casts between them.
Result<void> RegisterBuiltins(Registry& registry);


//
// -------[ Graphs ]-------
//

class Blueprint;
class TypeResolution;
struct FileLoader;

enum class LinkMode : uint8_t
{
    Plain, // made only when it fits, and disables no other link
    Force, // made, and the links it conflicts with are disabled
    Cast,  // joins two decided types through the registry's cast between them, and changes neither
    Keep,  // made as a file lists it: disabled when it doesn't fit
};

struct Pin : PinDeclaration
{
    Id          m_Id          = 0;
    Id          m_Node        = 0;
    const Type* m_DecidedType = nullptr; // null for flow pins, and while undecided
    std::string m_Wildcard;              // an undecided type, as shown: "?{Int32|Float}", "Array<?>"
};

// A data pin declared with a struct type, which can split into its fields; a pin of a type
// variable can't.
const StructType* SplittableStruct(const PinDeclaration& pin);

// A struct pin split into its fields, which the node's code still reads, or writes, as one.
struct SplitPin
{
    std::string m_Key;
    const Type* m_Type      = nullptr; // the struct's
    Direction   m_Direction = Direction::Input;
};

struct Node
{
    Id              m_Id   = 0;
    const NodeType* m_Type = nullptr;
    NodeConfig      m_Config;

    // Every key the node has had: a pin that goes and comes back keeps its ID, and so its links.
    std::map<std::string, Id, std::less<>> m_KeyIds;

    // What the user typed into inputs, by pin. A value stays while its pin's type is undecided,
    // and is reset once the type is decided as another.
    std::map<Id, Value> m_Values;

    // From the last update.
    std::vector<VariableDeclaration> m_Variables;
    std::vector<GroupDeclaration>    m_Groups;
    std::vector<Pin>                 m_Pins;
    std::vector<SplitPin>            m_Splits;
    Bindings                         m_Bindings;

    const Pin* FindPin(std::string_view key) const;

    // As messages name it: "Add#7".
    std::string Name() const;
};

struct Link
{
    Id          m_Id     = 0;
    Id          m_From   = 0; // an output
    Id          m_To     = 0; // an input
    int         m_Forced = 0; // > 0: forced; the latest forced link wins
    std::string m_Cast;       // a cast link's cast, as files name it: "Int32->Float"

    // From the last update. A disabled link takes no part in typing, compiling or running.
    bool  m_Active = true;
    Error m_Reason;
};

// A node type whose new node a link with a pin of the graph would join, and through which pin.
struct NewNodeFit
{
    const NodeType* m_Type = nullptr;
    std::string     m_Pin;  // the new node's
    std::string     m_Cast; // the cast the link needs, as files name it; empty when it needs none
};

// Nodes, and links between their pins. After every edit, nodes declare their pins and links
// decide their types, until the pins no longer change. The pointers and spans it gives out last
// until its next edit.
class Graph
{
public:
    Graph(Blueprint& blueprint, Id id, std::string name);

    Id                 GetId() const   { return m_Id; }
    const std::string& GetName() const { return m_Name; }

    Result<Id>   AddNode(std::string_view type, NodeConfig config = {});
    Result<void> RemoveNode(Id node);
    // A data input takes one link, as a flow output does; a new link replaces the one it has.
    Result<Id>   Connect(Id from, Id to, LinkMode mode = LinkMode::Plain);
    Result<void> CanConnect(Id from, Id to, LinkMode mode = LinkMode::Plain) const;
    // The active links that forcing a link from 'from' to 'to' would disable, or why it can't be.
    Result<std::vector<Id>> ForceDisables(Id from, Id to) const;
    // Of the node types, in their order, those whose new node a link with 'pin' fits by types: through
    // its first pin a link fits as it is, or else its first a cast fits. The graph is resolved once,
    // so the link can still be refused when made, as when deciding the pin's type changes pins elsewhere.
    Result<std::vector<NewNodeFit>> FitNewNodes(Id pin, std::span<const NodeType* const> types) const;
    Result<void> Disconnect(Id link);
    // A forced config may disable links, but a type the user chose must fit its variable even then.
    Result<void> SetConfig(Id node, NodeConfig config, bool force = false);
    Result<void> AddGroupPin(Id node, std::string_view group);
    // The pins at the index go with their links; later indices move down, keeping their IDs.
    Result<void> RemoveGroupPin(Id node, std::string_view group, int index);
    // A struct pin gives way to a pin per field, "pos" to "pos.x" and "pos.y", and its links are
    // disabled until it's recombined. Recombining a pin recombines its split fields too.
    Result<void> SplitPin(Id node, std::string_view key, bool split);
    // For a data input of a decided type, a value of that type; no value clears it. Values in pins
    // and in configs are those a file can hold, such as finite Floats.
    Result<void> SetValue(Id pin, Value value);

    std::span<const Node>       Nodes() const       { return m_Nodes; }
    std::span<const Link>       Links() const       { return m_Links; }
    std::span<const Diagnostic> Diagnostics() const { return m_Diagnostics; }

    // With an error diagnostic, the graph can be edited and saved, but not run.
    bool IsBroken() const;

    const Node* FindNode(Id node) const;
    const Pin*  FindPin(Id pin) const;
    const Node* NodeOf(Id pin) const;
    const Link* FindLink(Id link) const;
    const Value* FindValue(Id pin) const;

    // "Int32", or an undecided type: "?{Int32|Float|String}", "Array<?>".
    std::string TypeText(Id pin) const;

private:
    friend struct FileLoader;

    struct Problem
    {
        Id    m_Node = 0;
        Error m_Error;
    };

    void             Update();
    void             Declare();
    std::vector<int> Resolve(TypeResolution& resolution);
    void             Diagnose();
    Result<void>     Disruption(const Graph& before, bool forced) const;
    Result<void>     CheckLink(const Pin& from, const Pin& to) const;
    Result<void>     Editable(const Node& node) const;
    bool             ClosesPureLoop(const Pin& from, const Pin& to) const;
    Node*            MutableNode(Id node);
    std::string      PinName(const Pin& pin) const;
    std::string      LinkName(const Link& link) const; // of an active link, whose pins are on their nodes
    std::string      LinkName(const Pin& from, const Pin& to) const;

    Blueprint*                                         m_Blueprint;
    Id                                                 m_Id;
    std::string                                        m_Name;
    std::vector<Node>                                  m_Nodes;
    std::vector<Link>                                  m_Links;
    int                                                m_LastForced = 0;
    std::unordered_map<Id, size_t>                     m_NodeIndex;
    std::unordered_map<Id, std::pair<size_t, size_t>>  m_PinIndex;
    std::vector<Problem>                               m_Problems;
    std::vector<Diagnostic>                            m_Diagnostics;
};


//
// -------[ Blueprints ]-------
//

// A document: graphs that share one space of IDs, and the application's registry and type table.
class Blueprint
{
public:
    Blueprint(std::shared_ptr<const Registry> registry, std::shared_ptr<TypeTable> types);
    Blueprint(const Blueprint&) = delete;
    Blueprint& operator=(const Blueprint&) = delete;

    const Registry& GetRegistry() const { return *m_Registry; }
    TypeTable&      GetTypes() const    { return *m_Types; }

    Graph&       AddGraph(std::string name);
    Graph*       FindGraph(Id graph);
    const Graph* FindGraph(Id graph) const;

    std::span<const std::unique_ptr<Graph>> Graphs() const { return m_Graphs; }

    const NodeType* FindNodeType(std::string_view name) const;

    Id NewId() { return m_NextId++; }

private:
    friend class Graph;
    friend struct FileLoader;
    friend crude_json::value Save(const Blueprint& blueprint);
    friend Result<void>      Load(Blueprint& blueprint, const crude_json::value& file);

    std::shared_ptr<const Registry>        m_Registry;
    std::shared_ptr<TypeTable>             m_Types;
    Id                                     m_NextId = 1;
    std::vector<std::unique_ptr<Graph>>    m_Graphs;
    std::vector<std::unique_ptr<NodeType>> m_Placeholders; // for the nodes of types the registry lacks
};


//
// -------[ Files ]-------
//

// Format 2: "format": "crude_blueprint", "version": 2. Values are saved with their types, and of
// the values in inputs, only those the user typed. Types are text: "Array<Int32>".
crude_json::value Save(const Blueprint& blueprint);

// Into an empty blueprint, which a file it can't read leaves empty. Links that don't fit load
// disabled. A node it can't understand, such as one of a node type the registry lacks, loads as a
// placeholder: its links disabled, an error on it, and saved back unchanged.
Result<void> Load(Blueprint& blueprint, const crude_json::value& file);


//
// -------[ Programs ]-------
//

class Context;
struct CompileResult;

// A pin as a program runs it.
struct ProgramPin
{
    Id          m_Id        = 0;
    std::string m_Key;
    Direction   m_Direction = Direction::Input;
    bool        m_Flow      = false;
    const Type* m_Type      = nullptr;

    // A data input reads the output of slot m_Source through m_Cast, and without a link, m_Constant:
    // the value the user typed, else the declared default, else its type's.
    int             m_Source = -1;
    const CastInfo* m_Cast   = nullptr;
    Id              m_Link   = 0; // a data input's, or a flow output's
    Value           m_Constant;

    int m_Slot      = -1; // a data output's
    int m_Target    = -1; // a flow output's node, by index, and the flow input there; -1 when unlinked
    int m_TargetPin = -1;
};

struct ProgramNode
{
    Id                      m_Id    = 0;
    Id                      m_Graph = 0;
    const NodeType*         m_Type  = nullptr;
    NodeConfig              m_Config;
    std::string             m_Name;          // as messages name it: "Add#7"
    bool                    m_Pure  = false; // without flow pins
    std::vector<ProgramPin> m_Pins;
    std::vector<SplitPin>   m_Splits;
};

// A blueprint as it was compiled. Contexts share it, so edits never reach a run in progress.
class Program
{
public:
    std::span<const ProgramNode> Nodes() const { return m_Nodes; }

    int FindNode(Id node) const; // its index, or -1
    int FindEvent(std::string_view name) const;

private:
    friend class Context;
    friend class NodeContext;
    friend CompileResult Compile(const Blueprint& blueprint);

    std::vector<ProgramNode>                    m_Nodes;
    std::vector<std::pair<int, int>>            m_Slots; // each slot's node and pin, by index
    std::map<std::string, int, std::less<>>     m_Events;
    std::unordered_map<Id, std::pair<int, int>> m_Pins;  // each pin's node and pin, by index
};

struct CompileResult
{
    std::shared_ptr<const Program> m_Program;     // null while any diagnostic is an error
    std::vector<Diagnostic>        m_Diagnostics; // warnings too
};

CompileResult Compile(const Blueprint& blueprint);


//
// -------[ Contexts ]-------
//

using FiberId = uint32_t;

enum class FiberState : uint8_t
{
    Ready,   // with a node to run next
    Done,
    Failed,  // a run-time error stopped it
    Stopped,
};

// A fiber is one line of execution: an event's run.
struct FiberInfo
{
    FiberId    m_Id           = 0;
    FiberState m_State        = FiberState::Ready;
    Id         m_Node         = 0;     // the node it runs next
    Id         m_Ran          = 0;     // the node its last step ran
    Id         m_Link         = 0;     // the flow link its last step took; 0 for none, as into a loop's node again
    bool       m_AtBreakpoint = false; // Run() stopped it before its next node, which has a breakpoint
};

// Why Run() returned.
enum class RunState : uint8_t
{
    Idle,       // no fiber is ready
    Running,    // the time is up, and fibers are ready
    Breakpoint, // a fiber is before a node with a breakpoint
};

// A value as the debugger shows it.
struct Watch
{
    Value    m_Value;
    uint64_t m_Step = 0; // the step that stored or computed it; 0 when it's a default nothing wrote
};

// What a run tells its host.
struct ContextMonitor
{
    virtual ~ContextMonitor() = default;
    virtual void OnLog(const Context& context, Id node, std::string_view text) = 0;
};

class NodeContext
{
public:
    // An input's value: what its link reads, through the link's cast, or else its constant.
    Result<Value> Get(std::string_view input);
    template <typename T>
    Result<T>     Get(std::string_view input); // bool, int32_t, float or std::string
    // The inputs of a group's prefix, by index: "in" for in.0, in.1 and on.
    Result<std::vector<Value>> Inputs(std::string_view prefix);
    // Of the output's decided type.
    void Set(std::string_view output, Value value);

    const Type*       TypeOf(std::string_view pin) const;
    bool              HasPin(std::string_view pin) const; // as a group's, which the config decides
    const NodeConfig& Config() const;
    void              Log(std::string_view text);

    // The node's own, in this context: made on first use, and kept until the context goes.
    template <typename T>
    T& State();

protected:
    NodeContext(Context& context, int node): m_Context(context), m_Node(node) {}

    Context& m_Context;
    int      m_Node;

private:
    std::any& StateSlot();
    Error     NotA(std::string_view input, const Type& type) const;
};

class EvalContext final : public NodeContext
{
public:
    EvalContext(Context& context, int node): NodeContext(context, node) {}
};

class ExecContext final : public NodeContext
{
public:
    ExecContext(Context& context, int node, bool again): NodeContext(context, node), m_Again(again) {}

    // Entered again after the branch of its Exit::Loop.
    bool IsAgain() const { return m_Again; }

private:
    bool m_Again;
};

// One run of a program: its fibers, the values of its slots, the state of its nodes, and the errors
// that stopped fibers. An error stops only its fiber.
class Context
{
public:
    explicit Context(std::shared_ptr<const Program> program);

    Result<FiberId>    Start(std::string_view event);
    // Runs the fiber's next node, breakpoint or not.
    Result<FiberState> Step(FiberId fiber);
    // Steps the ready fibers in turn until none is ready, the time is up, or a fiber is before a
    // node with a breakpoint. Run again, that fiber runs the node.
    RunState           Run(std::chrono::microseconds budget);
    void               Stop(FiberId fiber);
    // Of a node with flow pins; a pure node's never stops a run.
    void               SetBreakpoint(Id node, bool enabled);

    std::vector<FiberInfo>      Fibers() const;
    uint64_t                    StepCount() const   { return m_Step; }
    std::span<const Diagnostic> Diagnostics() const { return m_Diagnostics; }
    const Program&              GetProgram() const  { return *m_Program; }
    void                        SetMonitor(ContextMonitor* monitor) { m_Monitor = monitor; }

    // A pin's value now. A pure output that this step hasn't computed is computed, which changes
    // nothing else; an input gives what it reads.
    Result<Watch> Evaluate(Id pin);

private:
    friend class NodeContext;

    struct Fiber
    {
        FiberInfo                        m_Info;
        int                              m_Node  = -1;
        int                              m_Entry = -1;
        bool                             m_Again = false;
        std::vector<std::pair<int, int>> m_Loops; // nodes to enter again once their branch ends, and where
    };

    Result<Value> Read(int node, int pin);
    Result<Value> ReadSlot(int slot);
    Result<void>  EvaluateNode(int node);
    void          Write(int node, int pin, Value value);
    void          Settle(Fiber& fiber);
    FiberState    Fail(Fiber& fiber, Error error, int node);
    Fiber*        FindFiber(FiberId fiber);

    std::shared_ptr<const Program> m_Program;
    std::vector<Value>             m_Slots;
    std::vector<uint64_t>          m_SlotSteps;   // the step that wrote each slot
    std::vector<uint64_t>          m_Computed;    // per node: the generation of a pure node's outputs
    std::vector<std::any>          m_States;      // per node
    std::vector<bool>              m_Breakpoints; // per node
    std::vector<Fiber>             m_Fibers;
    std::vector<Diagnostic>        m_Diagnostics;
    ContextMonitor*                m_Monitor    = nullptr;
    uint64_t                       m_Step       = 0;
    // Up when a step starts and when a node with flow pins writes an output, so a pure output
    // is computed at most once for what it reads.
    uint64_t                       m_Generation = 1;
    FiberId                        m_NextFiber  = 1;
    int                            m_Depth      = 0; // of pure nodes computed in each other's inputs
    // Where the error on its way out arose: a pure node or a cast deep in an input.
    int                            m_ErrorNode  = -1;
    Id                             m_ErrorLink  = 0;
};

template <typename T>
Result<T> NodeContext::Get(std::string_view input)
{
    auto read = Get(input);
    if (!read)
        return read.GetError();
    auto& value = read.GetValue();
    if constexpr (std::is_same_v<T, bool>)
        return value.GetType() == &BoolType() ? Result<T>(value.AsBool()) : Result<T>(NotA(input, BoolType()));
    else if constexpr (std::is_same_v<T, int32_t>)
        return value.GetType() == &Int32Type() ? Result<T>(value.AsInt32()) : Result<T>(NotA(input, Int32Type()));
    else if constexpr (std::is_same_v<T, float>)
        return value.GetType() == &FloatType() ? Result<T>(value.AsFloat()) : Result<T>(NotA(input, FloatType()));
    else if constexpr (std::is_same_v<T, std::string>)
        return value.GetType() == &StringType() ? Result<T>(std::string(value.AsString())) : Result<T>(NotA(input, StringType()));
    else
        static_assert(sizeof(T) == 0, "Get<T>() takes bool, int32_t, float or std::string");
}

template <typename T>
T& NodeContext::State()
{
    auto& state = StateSlot();
    if (auto existing = std::any_cast<T>(&state))
        return *existing;
    return state.emplace<T>();
}

} // namespace crude_blueprint
