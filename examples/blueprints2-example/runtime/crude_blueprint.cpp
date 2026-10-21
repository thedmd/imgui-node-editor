# include "crude_blueprint.h"
# include <crude_json.h>
# include <algorithm>
# include <charconv>
# include <cmath>
# include <limits>


//
// -------[ Errors ]-------
//

crude_blueprint::Error::Error(ErrorCategory category, uint16_t code, std::string_view message)
    : m_Category(category)
    , m_Code(code)
{
    if (!message.empty())
        m_Message = MakeRef<const StringBox>(std::string(message));
}


//
// -------[ Types ]-------
//

const crude_blueprint::Type* crude_blueprint::TypeTable::Get(const ValueType& head, std::span<const Type* const> args)
{
    CRUDE_BP_ASSERT(static_cast<int>(args.size()) == head.m_Arity);
    if (head.m_Arity == 0)
        return &head.AsType();

    std::vector<const Type*> key(args.begin(), args.end());
    auto& type = m_Types[{ &head, key }];
    if (!type)
    {
        auto name = head.m_Name + "<";
        for (size_t i = 0; i < key.size(); ++i)
            name += (i ? "," : "") + key[i]->m_Name;
        name += ">";
        type = std::make_unique<Type>(Type{ &head, std::move(key), std::move(name) });
    }
    return type.get();
}


//
// -------[ Values ]-------
//

namespace crude_blueprint { namespace {

struct ArrayBox final : Box
{
    CRUDE_BP_RTTI(ArrayBox, Box)

    explicit ArrayBox(std::vector<Value> items): m_Items(std::move(items)) {}

    const std::vector<Value> m_Items;
};

struct MapBox final : Box
{
    CRUDE_BP_RTTI(MapBox, Box)

    explicit MapBox(std::vector<std::pair<Value, Value>> entries): m_Entries(std::move(entries)) {}

    const std::vector<std::pair<Value, Value>> m_Entries;
};

struct StructBox final : Box
{
    CRUDE_BP_RTTI(StructBox, Box)

    explicit StructBox(std::vector<Value> fields): m_Fields(std::move(fields)) {}

    const std::vector<Value> m_Fields;
};

template <typename T>
const T* BoxOf(const std::variant<bool, int32_t, float, Ref<const Box>>& data)
{
    auto box = std::get_if<Ref<const Box>>(&data);
    return box ? Cast<T>(box->Get()) : nullptr;
}

} } // namespace crude_blueprint { namespace {

crude_blueprint::Value crude_blueprint::Value::Bool(bool value)
{
    return Value(BoolType(), value);
}

crude_blueprint::Value crude_blueprint::Value::Int32(int32_t value)
{
    return Value(Int32Type(), value);
}

crude_blueprint::Value crude_blueprint::Value::Float(float value)
{
    return Value(FloatType(), value);
}

crude_blueprint::Value crude_blueprint::Value::String(std::string text)
{
    return Value(StringType(), MakeRef<const StringBox>(std::move(text)));
}

crude_blueprint::Value crude_blueprint::Value::Array(const Type& type, std::vector<Value> items)
{
    CRUDE_BP_ASSERT(type.m_Head == &ArrayValueType());
    return Value(type, MakeRef<const ArrayBox>(std::move(items)));
}

crude_blueprint::Value crude_blueprint::Value::Map(const Type& type, std::vector<std::pair<Value, Value>> entries)
{
    CRUDE_BP_ASSERT(type.m_Head == &MapValueType() && type.m_Args[0]->m_Head->IsKey());
    auto& keyType = *type.m_Args[0]->m_Head;
    std::stable_sort(entries.begin(), entries.end(), [&keyType](const auto& a, const auto& b) { return keyType.Compare(a.first, b.first) < 0; });

    // Of equal keys, which are next to each other now, the last one stays.
    std::vector<std::pair<Value, Value>> unique;
    unique.reserve(entries.size());
    for (auto& entry : entries)
    {
        if (!unique.empty() && keyType.Compare(unique.back().first, entry.first) == 0)
            unique.back() = std::move(entry);
        else
            unique.push_back(std::move(entry));
    }
    return Value(type, MakeRef<const MapBox>(std::move(unique)));
}

crude_blueprint::Value crude_blueprint::Value::Struct(const Type& type, std::vector<Value> fields)
{
    CRUDE_BP_ASSERT(IsA<StructType>(type.m_Head) && fields.size() == static_cast<const StructType*>(type.m_Head)->Fields().size());
    return Value(type, MakeRef<const StructBox>(std::move(fields)));
}

crude_blueprint::Value crude_blueprint::Value::Boxed(const Type& type, Ref<const Box> box)
{
    return Value(type, std::move(box));
}

bool crude_blueprint::Value::AsBool() const
{
    auto value = std::get_if<bool>(&m_Data);
    CRUDE_BP_ASSERT(value && m_Type == &BoolType());
    return value ? *value : false;
}

int32_t crude_blueprint::Value::AsInt32() const
{
    auto value = std::get_if<int32_t>(&m_Data);
    CRUDE_BP_ASSERT(value);
    return value ? *value : 0;
}

float crude_blueprint::Value::AsFloat() const
{
    auto value = std::get_if<float>(&m_Data);
    CRUDE_BP_ASSERT(value);
    return value ? *value : 0.0f;
}

std::string_view crude_blueprint::Value::AsString() const
{
    auto box = BoxOf<StringBox>(m_Data);
    CRUDE_BP_ASSERT(box);
    return box ? std::string_view(box->m_Text) : std::string_view();
}

std::span<const crude_blueprint::Value> crude_blueprint::Value::AsArray() const
{
    auto box = BoxOf<ArrayBox>(m_Data);
    CRUDE_BP_ASSERT(box);
    return box ? std::span<const Value>(box->m_Items) : std::span<const Value>();
}

std::span<const std::pair<crude_blueprint::Value, crude_blueprint::Value>> crude_blueprint::Value::AsMap() const
{
    auto box = BoxOf<MapBox>(m_Data);
    CRUDE_BP_ASSERT(box);
    return box ? std::span<const std::pair<Value, Value>>(box->m_Entries) : std::span<const std::pair<Value, Value>>();
}

std::span<const crude_blueprint::Value> crude_blueprint::Value::AsStruct() const
{
    auto box = BoxOf<StructBox>(m_Data);
    CRUDE_BP_ASSERT(box);
    return box ? std::span<const Value>(box->m_Fields) : std::span<const Value>();
}

const crude_blueprint::Box* crude_blueprint::Value::AsBox() const
{
    auto box = std::get_if<Ref<const Box>>(&m_Data);
    return box ? box->Get() : nullptr;
}

const crude_blueprint::Value* crude_blueprint::Value::Find(const Value& key) const
{
    auto entries = AsMap();
    if (entries.empty())
        return nullptr;
    auto& keyType = *m_Type->m_Args[0]->m_Head;
    auto it = std::lower_bound(entries.begin(), entries.end(), key, [&keyType](const auto& entry, const Value& k) { return keyType.Compare(entry.first, k) < 0; });
    if (it == entries.end() || keyType.Compare(it->first, key) != 0)
        return nullptr;
    return &it->second;
}

bool crude_blueprint::Value::operator==(const Value& other) const
{
    if (m_Type != other.m_Type)
        return false;
    if (!m_Type)
        return true;
    return m_Type->m_Head->Equal(*this, other);
}


//
// -------[ Value types ]-------
//

namespace crude_blueprint { namespace {

Error Within(const std::string& where, const Error& error)
{
    return Error(error.m_Category, error.m_Code, where + ": " + std::string(error.Message()));
}

std::string Quote(std::string_view text)
{
    std::string result = "\"";
    for (auto c : text)
    {
        if (c == '"' || c == '\\')
            result += '\\';
        result += c;
    }
    return result + "\"";
}

// A value inside another one: strings are quoted there.
std::string FormatItem(const Value& value)
{
    if (!value.IsValid())
        return "?";
    if (value.GetType() == &StringType())
        return Quote(value.AsString());
    return value.GetType()->m_Head->Format(value);
}

struct BoolTypeImpl final : ValueType
{
    BoolTypeImpl(): ValueType("Bool") {}

    Value Default(const Type&) const override { return Value::Bool(false); }

    Result<Value> Load(const Type&, const crude_json::value& json) const override
    {
        if (!json.is_boolean())
            return Error(BlueprintError::WrongType, "not a Bool");
        return Value::Bool(json.get<crude_json::boolean>());
    }

    crude_json::value Save(const Value& value) const override { return value.AsBool(); }
    std::string Format(const Value& value) const override { return value.AsBool() ? "true" : "false"; }
    bool Equal(const Value& a, const Value& b) const override { return a.AsBool() == b.AsBool(); }
};

struct Int32TypeImpl final : ValueType
{
    Int32TypeImpl(): ValueType("Int32") {}

    Value Default(const Type&) const override { return Value::Int32(0); }

    Result<Value> Load(const Type&, const crude_json::value& json) const override
    {
        if (!json.is_number())
            return Error(BlueprintError::WrongType, "not an Int32");
        auto number = json.get<crude_json::number>();
        if (!(number >= std::numeric_limits<int32_t>::min() && number <= std::numeric_limits<int32_t>::max()))
            return Error(BlueprintError::OutOfRange, "outside Int32's range");
        if (number != std::trunc(number))
            return Error(BlueprintError::WrongType, "not a whole number");
        return Value::Int32(static_cast<int32_t>(number));
    }

    crude_json::value Save(const Value& value) const override { return value.AsInt32(); }
    std::string Format(const Value& value) const override { return std::to_string(value.AsInt32()); }
    bool Equal(const Value& a, const Value& b) const override { return a.AsInt32() == b.AsInt32(); }

    bool IsKey() const override { return true; }
    int Compare(const Value& a, const Value& b) const override { return (a.AsInt32() > b.AsInt32()) - (a.AsInt32() < b.AsInt32()); }
};

struct FloatTypeImpl final : ValueType
{
    FloatTypeImpl(): ValueType("Float") {}

    Value Default(const Type&) const override { return Value::Float(0.0f); }

    Result<Value> Load(const Type&, const crude_json::value& json) const override
    {
        if (!json.is_number())
            return Error(BlueprintError::WrongType, "not a Float");
        auto number = json.get<crude_json::number>();
        if (!std::isfinite(number))
            return Error(BlueprintError::NotFinite, "not finite");
        if (std::abs(number) > std::numeric_limits<float>::max())
            return Error(BlueprintError::OutOfRange, "outside Float's range");
        return Value::Float(static_cast<float>(number));
    }

    // A file can't hold NaN or an infinity; Load() refuses the null they become.
    crude_json::value Save(const Value& value) const override
    {
        if (!std::isfinite(value.AsFloat()))
            return crude_json::value();
        return static_cast<crude_json::number>(value.AsFloat());
    }

    std::string Format(const Value& value) const override
    {
        char buffer[32];
        auto result = std::to_chars(buffer, buffer + sizeof(buffer), value.AsFloat());
        return std::string(buffer, result.ptr);
    }

    bool Equal(const Value& a, const Value& b) const override { return a.AsFloat() == b.AsFloat(); }
};

struct StringTypeImpl final : ValueType
{
    StringTypeImpl(): ValueType("String") {}

    Value Default(const Type&) const override { return Value::String({}); }

    Result<Value> Load(const Type&, const crude_json::value& json) const override
    {
        if (!json.is_string())
            return Error(BlueprintError::WrongType, "not a String");
        return Value::String(json.get<crude_json::string>());
    }

    crude_json::value Save(const Value& value) const override { return std::string(value.AsString()); }
    std::string Format(const Value& value) const override { return std::string(value.AsString()); }
    bool Equal(const Value& a, const Value& b) const override { return a.AsString() == b.AsString(); }

    bool IsKey() const override { return true; }
    int Compare(const Value& a, const Value& b) const override { return a.AsString().compare(b.AsString()); }
};

struct ArrayTypeImpl final : ValueType
{
    ArrayTypeImpl(): ValueType("Array", 1) {}

    Value Default(const Type& type) const override { return Value::Array(type, {}); }

    Result<Value> Load(const Type& type, const crude_json::value& json) const override
    {
        if (!json.is_array())
            return Error(BlueprintError::WrongType, "not an array");
        auto& itemType = *type.m_Args[0];
        std::vector<Value> items;
        for (auto& itemJson : json.get<crude_json::array>())
        {
            auto item = itemType.m_Head->Load(itemType, itemJson);
            if (!item)
                return Within("item " + std::to_string(items.size()), item.GetError());
            items.push_back(std::move(item).GetValue());
        }
        return Value::Array(type, std::move(items));
    }

    crude_json::value Save(const Value& value) const override
    {
        crude_json::array result;
        for (auto& item : value.AsArray())
            result.push_back(item.GetType()->m_Head->Save(item));
        return result;
    }

    std::string Format(const Value& value) const override
    {
        std::string result = "[";
        for (auto& item : value.AsArray())
            result += (result.size() > 1 ? ", " : "") + FormatItem(item);
        return result + "]";
    }

    bool Equal(const Value& a, const Value& b) const override
    {
        auto x = a.AsArray(), y = b.AsArray();
        return std::equal(x.begin(), x.end(), y.begin(), y.end());
    }
};

// In files, a map is an array of [key, value] pairs: keys needn't be strings.
struct MapTypeImpl final : ValueType
{
    MapTypeImpl(): ValueType("Map", 2) {}

    Value Default(const Type& type) const override { return Value::Map(type, {}); }

    Result<void> CheckArgs(std::span<const Type* const> args) const override
    {
        if (!args[0]->m_Head->IsKey())
            return Error(BlueprintError::WrongType, args[0]->m_Name + " can't be a key");
        return {};
    }

    Result<Value> Load(const Type& type, const crude_json::value& json) const override
    {
        auto& keyType   = *type.m_Args[0];
        auto& valueType = *type.m_Args[1];
        if (auto check = CheckArgs(type.m_Args); !check)
            return check.GetError();
        if (!json.is_array())
            return Error(BlueprintError::WrongType, "not an array of pairs");
        std::vector<std::pair<Value, Value>> entries;
        for (auto& entryJson : json.get<crude_json::array>())
        {
            const auto where = "entry " + std::to_string(entries.size());
            auto pair = entryJson.get_ptr<crude_json::array>();
            if (!pair || pair->size() != 2)
                return Error(BlueprintError::WrongType, where + ": not a pair");
            auto key = keyType.m_Head->Load(keyType, (*pair)[0]);
            if (!key)
                return Within(where + " key", key.GetError());
            auto value = valueType.m_Head->Load(valueType, (*pair)[1]);
            if (!value)
                return Within(where + " value", value.GetError());
            entries.emplace_back(std::move(key).GetValue(), std::move(value).GetValue());
        }

        auto map = Value::Map(type, entries);
        if (map.AsMap().size() != entries.size())
            return Error(BlueprintError::Duplicate, "a key appears twice");
        return map;
    }

    crude_json::value Save(const Value& value) const override
    {
        crude_json::array result;
        for (auto& [key, item] : value.AsMap())
            result.push_back(crude_json::array{ key.GetType()->m_Head->Save(key), item.GetType()->m_Head->Save(item) });
        return result;
    }

    std::string Format(const Value& value) const override
    {
        std::string result = "{";
        for (auto& [key, item] : value.AsMap())
            result += (result.size() > 1 ? ", " : "") + FormatItem(key) + ": " + FormatItem(item);
        return result + "}";
    }

    bool Equal(const Value& a, const Value& b) const override
    {
        auto x = a.AsMap(), y = b.AsMap();
        return std::equal(x.begin(), x.end(), y.begin(), y.end());
    }
};

} } // namespace crude_blueprint { namespace {

crude_blueprint::ValueType::ValueType(std::string name, int arity)
    : m_Name(std::move(name))
    , m_Arity(arity)
    , m_Self{ this, {}, m_Name }
{
}

crude_blueprint::Result<void> crude_blueprint::ValueType::CheckArgs(std::span<const Type* const>) const
{
    return {};
}

int crude_blueprint::ValueType::Compare(const Value&, const Value&) const
{
    CRUDE_BP_ASSERT(false && "not a key type");
    return 0;
}

const crude_blueprint::ValueType& crude_blueprint::BoolValueType()   { static const BoolTypeImpl   type; return type; }
const crude_blueprint::ValueType& crude_blueprint::Int32ValueType()  { static const Int32TypeImpl  type; return type; }
const crude_blueprint::ValueType& crude_blueprint::FloatValueType()  { static const FloatTypeImpl  type; return type; }
const crude_blueprint::ValueType& crude_blueprint::StringValueType() { static const StringTypeImpl type; return type; }
const crude_blueprint::ValueType& crude_blueprint::ArrayValueType()  { static const ArrayTypeImpl  type; return type; }
const crude_blueprint::ValueType& crude_blueprint::MapValueType()    { static const MapTypeImpl    type; return type; }

crude_blueprint::StructType::StructType(std::string name, std::vector<StructField> fields)
    : ValueType(std::move(name))
    , m_Fields(std::move(fields))
{
}

int crude_blueprint::StructType::FindField(std::string_view name) const
{
    for (size_t i = 0; i < m_Fields.size(); ++i)
        if (m_Fields[i].m_Name == name)
            return static_cast<int>(i);
    return -1;
}

crude_blueprint::Value crude_blueprint::StructType::Default(const Type& type) const
{
    std::vector<Value> fields;
    for (auto& field : m_Fields)
        fields.push_back(field.m_Type->m_Head->Default(*field.m_Type));
    return Value::Struct(type, std::move(fields));
}

crude_blueprint::Result<crude_blueprint::Value> crude_blueprint::StructType::Load(const Type& type, const crude_json::value& json) const
{
    if (!json.is_object())
        return Error(BlueprintError::WrongType, "not an object");
    std::vector<Value> fields;
    for (auto& field : m_Fields)
    {
        if (!json.contains(field.m_Name))
        {
            fields.push_back(field.m_Type->m_Head->Default(*field.m_Type));
            continue;
        }
        auto value = field.m_Type->m_Head->Load(*field.m_Type, json[field.m_Name]);
        if (!value)
            return Within(field.m_Name, value.GetError());
        fields.push_back(std::move(value).GetValue());
    }
    return Value::Struct(type, std::move(fields));
}

crude_json::value crude_blueprint::StructType::Save(const Value& value) const
{
    crude_json::object result;
    auto fields = value.AsStruct();
    for (size_t i = 0; i < m_Fields.size() && i < fields.size(); ++i)
        result[m_Fields[i].m_Name] = fields[i].GetType()->m_Head->Save(fields[i]);
    return result;
}

std::string crude_blueprint::StructType::Format(const Value& value) const
{
    std::string result = "{";
    auto fields = value.AsStruct();
    for (size_t i = 0; i < m_Fields.size() && i < fields.size(); ++i)
        result += (i ? ", " : "") + m_Fields[i].m_Name + ": " + FormatItem(fields[i]);
    return result + "}";
}

bool crude_blueprint::StructType::Equal(const Value& a, const Value& b) const
{
    auto x = a.AsStruct(), y = b.AsStruct();
    return std::equal(x.begin(), x.end(), y.begin(), y.end());
}


//
// -------[ Node types ]-------
//

namespace crude_blueprint { namespace {

bool UsesOnly(const TypeTerm& term, size_t variables)
{
    if (!term.m_Head)
        return term.m_Variable >= 0 && static_cast<size_t>(term.m_Variable) < variables;
    return std::all_of(term.m_Args.begin(), term.m_Args.end(), [variables](const TypeTerm& arg) { return UsesOnly(arg, variables); });
}

} } // namespace crude_blueprint { namespace {

crude_blueprint::TypeTerm::TypeTerm(const Type& type)
    : m_Head(type.m_Head)
{
    for (auto arg : type.m_Args)
        m_Args.emplace_back(*arg);
}

crude_blueprint::TypeTerm::TypeTerm(const ValueType& head, std::vector<TypeTerm> args)
    : m_Head(&head)
    , m_Args(std::move(args))
{
    CRUDE_BP_ASSERT(static_cast<int>(m_Args.size()) == head.m_Arity);
}

const crude_blueprint::Type* crude_blueprint::Bindings::Get(std::string_view variable) const
{
    auto it = m_Types.find(variable);
    return it != m_Types.end() ? it->second : nullptr;
}

crude_blueprint::TypeVariable crude_blueprint::NodeDeclaration::Variable(std::string name, std::vector<const ValueType*> allowed)
{
    CRUDE_BP_ASSERT(std::none_of(m_Variables.begin(), m_Variables.end(), [&name](const VariableDeclaration& variable) { return variable.m_Name == name; }));
    m_Variables.push_back({ std::move(name), std::move(allowed) });
    return { static_cast<int>(m_Variables.size()) - 1 };
}

void crude_blueprint::NodeDeclaration::Input(std::string key, std::string name, TypeTerm type, PinOptions options)
{
    AddPin({ std::move(key), std::move(name), Direction::Input, false, std::move(type), std::move(options.m_Default), options.m_Requirement });
}

void crude_blueprint::NodeDeclaration::Output(std::string key, std::string name, TypeTerm type, PinOptions options)
{
    AddPin({ std::move(key), std::move(name), Direction::Output, false, std::move(type), std::move(options.m_Default), options.m_Requirement });
}

void crude_blueprint::NodeDeclaration::FlowInput(std::string key, std::string name, PinOptions options)
{
    AddPin({ std::move(key), std::move(name), Direction::Input, true, {}, {}, options.m_Requirement });
}

void crude_blueprint::NodeDeclaration::FlowOutput(std::string key, std::string name, PinOptions options)
{
    AddPin({ std::move(key), std::move(name), Direction::Output, true, {}, {}, options.m_Requirement });
}

int crude_blueprint::NodeDeclaration::Group(std::string name, int min, int max, std::vector<std::string> prefixes)
{
    CRUDE_BP_ASSERT(0 <= min && min <= max);
    if (prefixes.empty())
        prefixes.push_back(name);
    auto it = m_Config.m_Groups.find(name);
    auto count = it != m_Config.m_Groups.end() ? std::clamp(it->second, min, max) : min;
    m_Groups.push_back({ std::move(name), std::move(prefixes), min, max });
    return count;
}

void crude_blueprint::NodeDeclaration::AddPin(PinDeclaration pin)
{
    CRUDE_BP_ASSERT(!pin.m_Key.empty() && std::none_of(m_Pins.begin(), m_Pins.end(), [&pin](const PinDeclaration& other) { return other.m_Key == pin.m_Key; }));
    CRUDE_BP_ASSERT(pin.m_Flow || UsesOnly(pin.m_Type, m_Variables.size()));
    m_Pins.push_back(std::move(pin));
}

crude_blueprint::NodeType::NodeType(std::string name, std::string title, std::string category)
    : m_Name(std::move(name))
    , m_Title(std::move(title))
    , m_Category(std::move(category))
{
}


//
// -------[ Registry ]-------
//

namespace crude_blueprint { namespace {

bool IsNameCharacter(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == ':';
}

} } // namespace crude_blueprint { namespace {

crude_blueprint::Result<void> crude_blueprint::Registry::AddValueType(const ValueType& type)
{
    if (type.m_Name.empty() || !std::all_of(type.m_Name.begin(), type.m_Name.end(), IsNameCharacter))
        return Error(BlueprintError::BadTypeText, Quote(type.m_Name) + " can't name a type");
    if (!m_ValueTypes.emplace(type.m_Name, &type).second)
        return Error(BlueprintError::Duplicate, "a value type named " + type.m_Name + " is already registered");
    return {};
}

crude_blueprint::Result<void> crude_blueprint::Registry::AddValueType(std::unique_ptr<ValueType> type)
{
    CRUDE_BP_ASSERT(type);
    if (auto result = AddValueType(*type); !result)
        return result;
    m_OwnedValueTypes.push_back(std::move(type));
    return {};
}

crude_blueprint::Result<void> crude_blueprint::Registry::AddCast(CastInfo cast)
{
    CRUDE_BP_ASSERT(cast.m_From && cast.m_To && cast.m_From != cast.m_To && cast.m_Function);
    if (FindCast(cast.m_From, cast.m_To))
        return Error(BlueprintError::Duplicate, "a cast " + cast.Name() + " is already registered");
    auto& owned = m_Casts.emplace_back(std::make_unique<CastInfo>(std::move(cast)));
    m_CastsFrom[owned->m_From].push_back(owned.get());
    return {};
}

const crude_blueprint::ValueType* crude_blueprint::Registry::FindValueType(std::string_view name) const
{
    auto it = m_ValueTypes.find(name);
    return it != m_ValueTypes.end() ? it->second : nullptr;
}

const crude_blueprint::CastInfo* crude_blueprint::Registry::FindCast(const Type* from, const Type* to) const
{
    for (auto cast : CastsFrom(from))
        if (cast->m_To == to)
            return cast;
    return nullptr;
}

std::span<const crude_blueprint::CastInfo* const> crude_blueprint::Registry::CastsFrom(const Type* from) const
{
    auto it = m_CastsFrom.find(from);
    if (it == m_CastsFrom.end())
        return {};
    return it->second;
}

crude_blueprint::Result<void> crude_blueprint::Registry::AddNodeType(const NodeType& type)
{
    CRUDE_BP_ASSERT(!type.m_Name.empty());
    if (!m_NodeTypes.emplace(type.m_Name, &type).second)
        return Error(BlueprintError::Duplicate, "a node type named " + type.m_Name + " is already registered");
    return {};
}

crude_blueprint::Result<void> crude_blueprint::Registry::AddNodeType(std::unique_ptr<NodeType> type)
{
    CRUDE_BP_ASSERT(type);
    if (auto result = AddNodeType(*type); !result)
        return result;
    m_OwnedNodeTypes.push_back(std::move(type));
    return {};
}

const crude_blueprint::NodeType* crude_blueprint::Registry::FindNodeType(std::string_view name) const
{
    auto it = m_NodeTypes.find(name);
    return it != m_NodeTypes.end() ? it->second : nullptr;
}

crude_blueprint::Result<void> crude_blueprint::RegisterBuiltins(Registry& registry)
{
    for (auto type : { &BoolValueType(), &Int32ValueType(), &FloatValueType(), &StringValueType(), &ArrayValueType(), &MapValueType() })
        if (auto result = registry.AddValueType(*type); !result)
            return result;

    auto toString = [](const Value& value) -> Result<Value> { return Value::String(value.GetType()->m_Head->Format(value)); };

    const CastInfo casts[] =
    {
        { &Int32Type(), &FloatType(), false, [](const Value& value) -> Result<Value> { return Value::Float(static_cast<float>(value.AsInt32())); } },
        // Toward zero, as C++ converts.
        { &FloatType(), &Int32Type(), true, [](const Value& value) -> Result<Value>
        {
            auto number = value.AsFloat();
            if (!(number >= -2147483648.0f && number < 2147483648.0f))
                return Error(BlueprintError::CastFailed, FloatValueType().Format(value) + " doesn't fit Int32");
            return Value::Int32(static_cast<int32_t>(number));
        } },
        { &BoolType(),  &Int32Type(),  false, [](const Value& value) -> Result<Value> { return Value::Int32(value.AsBool() ? 1 : 0); } },
        { &Int32Type(), &BoolType(),   false, [](const Value& value) -> Result<Value> { return Value::Bool(value.AsInt32() != 0); } },
        { &BoolType(),  &StringType(), false, toString },
        { &Int32Type(), &StringType(), false, toString },
        { &FloatType(), &StringType(), false, toString },
        { &StringType(), &Int32Type(), true, [](const Value& value) -> Result<Value>
        {
            auto text = value.AsString();
            int32_t number = 0;
            auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
            if (error == std::errc::result_out_of_range)
                return Error(BlueprintError::CastFailed, Quote(text) + " doesn't fit Int32");
            if (error != std::errc() || end != text.data() + text.size())
                return Error(BlueprintError::CastFailed, Quote(text) + " isn't a whole number");
            return Value::Int32(number);
        } },
        // As files hold numbers: no locale, no spaces, no NaN.
        { &StringType(), &FloatType(), true, [](const Value& value) -> Result<Value>
        {
            auto text = value.AsString();
            auto json = text.find_first_not_of("+-.0123456789Ee") == std::string_view::npos ? crude_json::value::parse(std::string(text)) : crude_json::value();
            if (!json.is_number())
                return Error(BlueprintError::CastFailed, Quote(text) + " isn't a number");
            auto number = json.get<crude_json::number>();
            if (std::abs(number) > std::numeric_limits<float>::max())
                return Error(BlueprintError::CastFailed, Quote(text) + " doesn't fit Float");
            return Value::Float(static_cast<float>(number));
        } },
    };
    for (auto& cast : casts)
        if (auto result = registry.AddCast(cast); !result)
            return result;
    return {};
}


//
// -------[ Type text ]-------
//

namespace crude_blueprint { namespace {

// type := name [ '<' type { ',' type } '>' ]
struct TypeParser
{
    static const int c_MaxDepth = 32;

    TypeTable&       m_Table;
    const Registry&  m_Registry;
    std::string_view m_Text;
    size_t           m_At    = 0;
    int              m_Depth = 0;

    void SkipSpaces()
    {
        while (m_At < m_Text.size() && (m_Text[m_At] == ' ' || m_Text[m_At] == '\t'))
            ++m_At;
    }

    bool Take(char c)
    {
        SkipSpaces();
        if (m_At < m_Text.size() && m_Text[m_At] == c)
        {
            ++m_At;
            return true;
        }
        return false;
    }

    Error Unexpected() const
    {
        if (m_At >= m_Text.size())
            return Error(BlueprintError::BadTypeText, Quote(m_Text) + " ends too soon");
        return Error(BlueprintError::BadTypeText, Quote(m_Text) + " has '" + m_Text[m_At] + "' at " + std::to_string(m_At));
    }

    Result<const Type*> Parse()
    {
        SkipSpaces();
        const auto start = m_At;
        while (m_At < m_Text.size() && IsNameCharacter(m_Text[m_At]))
            ++m_At;
        if (m_At == start)
            return Unexpected();

        const auto name = m_Text.substr(start, m_At - start);
        auto head = m_Registry.FindValueType(name);
        if (!head)
            return Error(BlueprintError::UnknownType, "no value type is named " + std::string(name));

        std::vector<const Type*> args;
        if (Take('<'))
        {
            if (++m_Depth > c_MaxDepth)
                return Error(BlueprintError::BadTypeText, "types nest deeper than " + std::to_string(c_MaxDepth));
            do
            {
                auto arg = Parse();
                if (!arg)
                    return arg;
                args.push_back(arg.GetValue());
            }
            while (Take(','));
            if (!Take('>'))
                return Unexpected();
            --m_Depth;
        }

        if (static_cast<int>(args.size()) != head->m_Arity)
            return Error(BlueprintError::BadTypeText, std::string(name) + " takes " + std::to_string(head->m_Arity) + (head->m_Arity == 1 ? " type" : " types") + ", not " + std::to_string(args.size()));
        if (auto check = head->CheckArgs(args); !check)
            return check.GetError();
        return m_Table.Get(*head, args);
    }
};

} } // namespace crude_blueprint { namespace {

crude_blueprint::Result<const crude_blueprint::Type*> crude_blueprint::TypeTable::Parse(const Registry& registry, std::string_view text)
{
    TypeParser parser{ *this, registry, text };
    auto type = parser.Parse();
    if (!type)
        return type;
    parser.SkipSpaces();
    if (parser.m_At != text.size())
        return parser.Unexpected();
    return type;
}
