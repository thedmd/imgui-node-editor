# include "crude_blueprint.h"
# include <crude_json.h>
# include <algorithm>
# include <charconv>
# include <cmath>
# include <exception>
# include <iterator>
# include <limits>
# include <tuple>
# include <unordered_set>


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

int crude_blueprint::GroupCount(const GroupDeclaration& group, const NodeConfig& config)
{
    auto it = config.m_Groups.find(group.m_Name);
    return it != config.m_Groups.end() ? std::clamp(it->second, group.m_Min, group.m_Max) : group.m_Min;
}

int crude_blueprint::NodeDeclaration::Group(std::string name, int min, int max, std::vector<std::string> prefixes)
{
    CRUDE_BP_ASSERT(0 <= min && min <= max);
    if (prefixes.empty())
        prefixes.push_back(name);
    m_Groups.push_back({ std::move(name), std::move(prefixes), min, max });
    return GroupCount(m_Groups.back(), m_Config);
}

void crude_blueprint::NodeDeclaration::AddPin(PinDeclaration pin)
{
    CRUDE_BP_ASSERT(!pin.m_Key.empty() && std::none_of(m_Pins.begin(), m_Pins.end(), [&pin](const PinDeclaration& other) { return other.m_Key == pin.m_Key; }));
    CRUDE_BP_ASSERT(pin.m_Flow || UsesOnly(pin.m_Type, m_Variables.size()));
    m_Pins.push_back(std::move(pin));
}

crude_blueprint::NodeType::NodeType(std::string name, std::string title, std::string category, uint32_t version)
    : m_Name(std::move(name))
    , m_Title(std::move(title))
    , m_Category(std::move(category))
    , m_Version(version)
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

std::vector<const crude_blueprint::ValueType*> crude_blueprint::Registry::ValueTypes() const
{
    std::vector<const ValueType*> types;
    for (auto& [name, type] : m_ValueTypes)
        types.push_back(type);
    return types;
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

std::vector<const crude_blueprint::NodeType*> crude_blueprint::Registry::NodeTypes() const
{
    std::vector<const NodeType*> types;
    for (auto& [name, type] : m_NodeTypes)
        types.push_back(type);
    return types;
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


//
// -------[ Type resolution ]-------
//

namespace crude_blueprint {

// Union-find over a graph's type variables, where a link makes two pins' types equal. A unification
// that fails is rolled back, so a link that doesn't fit leaves no trace.
class TypeResolution
{
public:
    explicit TypeResolution(TypeTable& types): m_Types(types) {}

    int Size() const { return static_cast<int>(m_Variables.size()); }

    void AddVariable(std::string name, std::vector<const ValueType*> allowed)
    {
        Variable variable;
        variable.m_Parent    = Size();
        variable.m_AllowedBy = allowed.empty() ? std::string() : name;
        variable.m_Allowed   = std::move(allowed);
        variable.m_Name      = std::move(name);
        m_Variables.push_back(std::move(variable));
    }

    void Begin()
    {
        m_Trail.clear();
        m_Recording = true;
    }

    void Commit()
    {
        m_Trail.clear();
        m_Recording = false;
    }

    void Rollback()
    {
        for (auto it = m_Trail.rbegin(); it != m_Trail.rend(); ++it)
            m_Variables[it->first] = std::move(it->second);
        Commit();
    }

    // 'why' names what makes them equal, for the messages of later conflicts.
    Result<void> Unify(const TypeTerm& lhs, const TypeTerm& rhs, const std::string& why)
    {
        int fromA, fromB;
        auto a = Shallow(lhs, fromA);
        auto b = Shallow(rhs, fromB);
        if (!a.m_Head && !b.m_Head)
            return Merge(Find(a.m_Variable), Find(b.m_Variable));
        if (!a.m_Head)
            return Bind(Find(a.m_Variable), b, why);
        if (!b.m_Head)
            return Bind(Find(b.m_Variable), a, why);
        if (a.m_Head != b.m_Head)
            return Error(BlueprintError::TypeMismatch, Text(a) + Because(fromA) + " doesn't match " + Text(b) + Because(fromB));
        for (size_t i = 0; i < a.m_Args.size(); ++i)
            if (auto result = Unify(a.m_Args[i], b.m_Args[i], why); !result)
                return result;
        return {};
    }

    // Null while any part is undecided.
    const Type* TypeOf(const TypeTerm& term) const
    {
        int from;
        auto shallow = Shallow(term, from);
        if (!shallow.m_Head)
            return nullptr;
        std::vector<const Type*> args;
        for (auto& arg : shallow.m_Args)
        {
            auto type = TypeOf(arg);
            if (!type)
                return nullptr;
            args.push_back(type);
        }
        return m_Types.Get(*shallow.m_Head, args);
    }

    std::string Text(const TypeTerm& term) const
    {
        int from;
        auto shallow = Shallow(term, from);
        if (!shallow.m_Head)
            return "?" + (shallow.m_Variable < 0 ? std::string() : AllowedText(m_Variables[Find(shallow.m_Variable)].m_Allowed));
        auto text = shallow.m_Head->m_Name;
        if (!shallow.m_Args.empty())
        {
            text += "<";
            for (size_t i = 0; i < shallow.m_Args.size(); ++i)
                text += (i ? "," : "") + Text(shallow.m_Args[i]);
            text += ">";
        }
        return text;
    }

private:
    struct Variable
    {
        int                           m_Parent = 0;
        int                           m_Size   = 1;
        std::optional<TypeTerm>       m_Bound;     // by representative
        std::vector<const ValueType*> m_Allowed;   // by representative; empty: any
        std::string                   m_AllowedBy; // by representative: whose allowed set applies
        std::string                   m_BoundBy;   // by representative: what bound it
        std::string                   m_Name;      // "Add#7.T"
    };

    static std::string AllowedText(const std::vector<const ValueType*>& allowed)
    {
        if (allowed.empty())
            return {};
        std::string text = "{";
        for (size_t i = 0; i < allowed.size(); ++i)
            text += (i ? "|" : "") + allowed[i]->m_Name;
        return text + "}";
    }

    // No path compression, which would have to be rolled back too; union by size keeps paths short.
    int Find(int variable) const
    {
        while (m_Variables[variable].m_Parent != variable)
            variable = m_Variables[variable].m_Parent;
        return variable;
    }

    // The term with its top variable replaced by what it's bound to; 'from' is that variable.
    TypeTerm Shallow(const TypeTerm& term, int& from) const
    {
        from = -1;
        if (term.m_Head || term.m_Variable < 0)
            return term;
        auto representative = Find(term.m_Variable);
        if (!m_Variables[representative].m_Bound)
            return TypeVariable{ representative };
        from = representative;
        return *m_Variables[representative].m_Bound;
    }

    bool Occurs(int variable, const TypeTerm& term) const
    {
        int from;
        auto shallow = Shallow(term, from);
        if (!shallow.m_Head)
            return Find(shallow.m_Variable) == variable;
        return std::any_of(shallow.m_Args.begin(), shallow.m_Args.end(), [this, variable](const TypeTerm& arg) { return Occurs(variable, arg); });
    }

    std::string Because(int from) const
    {
        if (from < 0 || m_Variables[from].m_BoundBy.empty())
            return {};
        return " (from " + m_Variables[from].m_BoundBy + ")";
    }

    Result<void> Bind(int variable, const TypeTerm& term, const std::string& why)
    {
        auto& bound = m_Variables[variable];
        if (Occurs(variable, term))
            return Error(BlueprintError::TypeMismatch, bound.m_Name + " can't be " + Text(term) + ", which contains it");
        if (!bound.m_Allowed.empty() && std::find(bound.m_Allowed.begin(), bound.m_Allowed.end(), term.m_Head) == bound.m_Allowed.end())
            return Error(BlueprintError::TypeMismatch, bound.m_AllowedBy + " takes only " + AllowedText(bound.m_Allowed) + ", not " + Text(term));
        Save(variable);
        bound.m_Bound   = term;
        bound.m_BoundBy = why;
        return {};
    }

    Result<void> Merge(int a, int b)
    {
        if (a == b)
            return {};
        auto& allowedA = m_Variables[a].m_Allowed;
        auto& allowedB = m_Variables[b].m_Allowed;
        std::vector<const ValueType*> allowed;
        std::string                   allowedBy;
        if (allowedA.empty() || allowedB.empty())
        {
            allowed   = allowedA.empty() ? allowedB : allowedA;
            allowedBy = allowedA.empty() ? m_Variables[b].m_AllowedBy : m_Variables[a].m_AllowedBy;
        }
        else
        {
            for (auto head : allowedA)
                if (std::find(allowedB.begin(), allowedB.end(), head) != allowedB.end())
                    allowed.push_back(head);
            if (allowed.empty())
                return Error(BlueprintError::TypeMismatch, m_Variables[a].m_AllowedBy + " takes " + AllowedText(allowedA) + " and " + m_Variables[b].m_AllowedBy + " takes " + AllowedText(allowedB) + ": no type is both");
            allowedBy = m_Variables[a].m_AllowedBy + " and " + m_Variables[b].m_AllowedBy;
        }

        if (m_Variables[a].m_Size < m_Variables[b].m_Size)
            std::swap(a, b);
        Save(a);
        Save(b);
        m_Variables[b].m_Parent    = a;
        m_Variables[a].m_Size     += m_Variables[b].m_Size;
        m_Variables[a].m_Allowed   = std::move(allowed);
        m_Variables[a].m_AllowedBy = std::move(allowedBy);
        return {};
    }

    void Save(int variable)
    {
        if (m_Recording)
            m_Trail.emplace_back(variable, m_Variables[variable]);
    }

    TypeTable&                            m_Types;
    std::vector<Variable>                 m_Variables;
    std::vector<std::pair<int, Variable>> m_Trail;
    bool                                  m_Recording = false;
};

} // namespace crude_blueprint


//
// -------[ Graphs ]-------
//

namespace crude_blueprint { namespace {

// A node a file holds that this runtime can't use: its pins as the file lists them, of types of
// their own, and the node as the file has it, which is what's saved.
struct PlaceholderNodeType final : NodeType
{
    CRUDE_BP_RTTI(PlaceholderNodeType, NodeType)

    PlaceholderNodeType(std::string name, crude_json::value saved, Error reason)
        : NodeType(name, name)
        , m_Saved(std::move(saved))
        , m_Reason(std::move(reason))
    {
    }

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        auto pins = m_Saved.find("pins");
        if (!pins || !pins->is_object())
            return;
        for (auto& [key, pin] : pins->get<crude_json::object>())
        {
            auto direction = pin.find("dir");
            if (!direction || !direction->is_string())
                continue;
            auto input = direction->get<crude_json::string>() == "in";
            auto flow  = pin["flow"].is_boolean() && pin["flow"].get<crude_json::boolean>();
            if (flow && input)
                declaration.FlowInput(key, key);
            else if (flow)
                declaration.FlowOutput(key, key);
            else if (input)
                declaration.Input(key, key, declaration.Variable(key));
            else
                declaration.Output(key, key, declaration.Variable(key));
        }
    }

    const crude_json::value m_Saved;
    const Error             m_Reason;
};

// A node's term in the graph: its variables numbered from the node's first.
TypeTerm Offset(const TypeTerm& term, int base)
{
    TypeTerm result = term;
    if (!result.m_Head && result.m_Variable >= 0)
        result.m_Variable += base;
    for (auto& arg : result.m_Args)
        arg = Offset(arg, base);
    return result;
}

// As a file would give it back.
Result<void> Savable(const Value& value, const std::string& what)
{
    auto type   = value.GetType();
    auto loaded = type->m_Head->Load(*type, type->m_Head->Save(value));
    if (!loaded)
        return Error(loaded.GetError().m_Category, loaded.GetError().m_Code, what + " can't hold " + type->m_Head->Format(value) + ", which no file holds: " + std::string(loaded.GetError().Message()));
    return {};
}

Result<void> Savable(const NodeConfig& config, const std::string& node)
{
    for (auto& [key, value] : config.m_Data)
        if (auto savable = Savable(value, node + "'s " + key); !savable)
            return savable;
    return {};
}

// The index of a group's pin key: 2 for "in.2", and for "in.2.x" of a split pin; -1 for others.
int GroupIndex(std::string_view key, std::string_view prefix, std::string_view* rest = nullptr)
{
    if (key.size() <= prefix.size() + 1 || key.substr(0, prefix.size()) != prefix || key[prefix.size()] != '.')
        return -1;
    auto digits = key.substr(prefix.size() + 1);
    int index = 0;
    auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), index);
    if (error != std::errc() || end == digits.data() || (end != digits.data() + digits.size() && *end != '.'))
        return -1;
    if (rest)
        *rest = digits.substr(static_cast<size_t>(end - digits.data()));
    return index;
}

// A key once an index of the group goes: none for that index's keys, one index down for later ones.
std::optional<std::string> AfterRemoval(const std::string& key, const GroupDeclaration& group, int index)
{
    for (auto& prefix : group.m_Prefixes)
    {
        std::string_view rest;
        auto at = GroupIndex(key, prefix, &rest);
        if (at < 0)
            continue;
        if (at == index)
            return std::nullopt;
        if (at > index)
            return prefix + "." + std::to_string(at - 1) + std::string(rest);
        break;
    }
    return key;
}

// Fields of split fields split too.
void Split(std::vector<PinDeclaration>& pins, std::span<const std::string> split, std::vector<SplitPin>& splits)
{
    for (size_t i = 0; i < pins.size();)
    {
        auto structType = SplittableStruct(pins[i]);
        if (!structType || std::find(split.begin(), split.end(), pins[i].m_Key) == split.end())
        {
            ++i;
            continue;
        }

        auto& pin      = pins[i];
        auto  defaults = pin.m_Default.IsValid() ? pin.m_Default.AsStruct() : std::span<const Value>();
        std::vector<PinDeclaration> fields;
        for (size_t f = 0; f < structType->Fields().size(); ++f)
        {
            auto& field = structType->Fields()[f];
            PinDeclaration fieldPin;
            fieldPin.m_Key         = pin.m_Key + "." + field.m_Name;
            fieldPin.m_Name        = pin.m_Name.empty() ? field.m_Name : pin.m_Name + " " + field.m_Name;
            fieldPin.m_Direction   = pin.m_Direction;
            fieldPin.m_Type        = *field.m_Type;
            fieldPin.m_Default     = f < defaults.size() ? defaults[f] : Value();
            fieldPin.m_Requirement = pin.m_Requirement;
            fields.push_back(std::move(fieldPin));
        }
        auto taken = std::any_of(fields.begin(), fields.end(), [&pins](const PinDeclaration& field)
        {
            return std::any_of(pins.begin(), pins.end(), [&field](const PinDeclaration& other) { return other.m_Key == field.m_Key; });
        });
        CRUDE_BP_ASSERT(!taken && "a split pin's field has the key of another pin");
        if (taken)
        {
            ++i;
            continue;
        }
        splits.push_back({ pin.m_Key, &structType->AsType(), pin.m_Direction });
        pins.erase(pins.begin() + static_cast<std::ptrdiff_t>(i));
        pins.insert(pins.begin() + static_cast<std::ptrdiff_t>(i), std::make_move_iterator(fields.begin()), std::make_move_iterator(fields.end()));
    }
}

} } // namespace crude_blueprint { namespace {

const crude_blueprint::StructType* crude_blueprint::SplittableStruct(const PinDeclaration& pin)
{
    return pin.m_Flow ? nullptr : Cast<StructType>(pin.m_Type.m_Head);
}

int crude_blueprint::GroupIndex(const GroupDeclaration& group, std::string_view key)
{
    for (auto& prefix : group.m_Prefixes)
        if (auto index = GroupIndex(key, prefix); index >= 0)
            return index;
    return -1;
}

const crude_blueprint::Pin* crude_blueprint::Node::FindPin(std::string_view key) const
{
    for (auto& pin : m_Pins)
        if (pin.m_Key == key)
            return &pin;
    return nullptr;
}

std::string crude_blueprint::Node::Name() const
{
    return (m_Type->m_Title.empty() ? m_Type->m_Name : m_Type->m_Title) + "#" + std::to_string(m_Id);
}

crude_blueprint::Graph::Graph(Blueprint& blueprint, Id id, std::string name)
    : m_Blueprint(&blueprint)
    , m_Id(id)
    , m_Name(std::move(name))
{
}

crude_blueprint::Result<crude_blueprint::Id> crude_blueprint::Graph::AddNode(std::string_view type, NodeConfig config)
{
    auto nodeType = m_Blueprint->FindNodeType(type);
    if (!nodeType)
        return Error(BlueprintError::UnknownType, "no node type is named " + std::string(type));

    if (auto savable = Savable(config, nodeType->m_Title); !savable)
        return savable.GetError();

    Graph trial = *this;
    auto& node = trial.m_Nodes.emplace_back();
    node.m_Id     = m_Blueprint->NewId();
    node.m_Type   = nodeType;
    node.m_Config = std::move(config);
    const auto id = node.m_Id;
    trial.Update();
    if (auto disruption = trial.Disruption(*this, false); !disruption)
        return disruption.GetError();
    *this = std::move(trial);
    return id;
}

crude_blueprint::Result<void> crude_blueprint::Graph::RemoveNode(Id node)
{
    auto removed = MutableNode(node);
    if (!removed)
        return Error(BlueprintError::NotFound, "no node " + std::to_string(node) + " in " + m_Name);
    // Links to pins the node doesn't have now, too.
    auto isNodesPin = [removed](Id pin) { return std::any_of(removed->m_KeyIds.begin(), removed->m_KeyIds.end(), [pin](const auto& key) { return key.second == pin; }); };
    std::erase_if(m_Links, [&isNodesPin](const Link& link) { return isNodesPin(link.m_From) || isNodesPin(link.m_To); });
    std::erase_if(m_Nodes, [node](const Node& other) { return other.m_Id == node; });
    Update();
    return {};
}

crude_blueprint::Result<crude_blueprint::Id> crude_blueprint::Graph::Connect(Id from, Id to, LinkMode mode)
{
    auto fromPin = FindPin(from);
    auto toPin   = FindPin(to);
    if (!fromPin || !toPin)
        return Error(BlueprintError::NotFound, "no pin " + std::to_string(fromPin ? to : from) + " in " + m_Name);
    if (auto check = CheckLink(*fromPin, *toPin); !check)
        return check.GetError();
    if (mode != LinkMode::Keep && ClosesPureLoop(*fromPin, *toPin))
        return Error(BlueprintError::Loop, LinkName(*fromPin, *toPin) + " closes a loop of pure nodes");

    Link link;
    link.m_Id   = m_Blueprint->NewId();
    link.m_From = from;
    link.m_To   = to;
    if (mode == LinkMode::Cast)
    {
        if (fromPin->m_Flow)
            return Error(BlueprintError::BadLink, "a flow link can't cast");
        if (!fromPin->m_DecidedType || !toPin->m_DecidedType)
            return Error(BlueprintError::Undecided, "a cast needs both types decided");
        auto cast = m_Blueprint->GetRegistry().FindCast(fromPin->m_DecidedType, toPin->m_DecidedType);
        if (!cast)
            return Error(BlueprintError::NoCast, "no cast from " + fromPin->m_DecidedType->m_Name + " to " + toPin->m_DecidedType->m_Name);
        link.m_Cast = cast->Name();
    }

    Graph trial = *this;
    if (mode == LinkMode::Force)
        link.m_Forced = ++trial.m_LastForced;
    const auto flow = fromPin->m_Flow;
    std::erase_if(trial.m_Links, [&link, flow](const Link& other) { return flow ? other.m_From == link.m_From : other.m_To == link.m_To; });
    trial.m_Links.push_back(link);
    trial.Update();

    if (mode != LinkMode::Keep)
    {
        auto made = trial.FindLink(link.m_Id);
        if (!made->m_Active)
            return made->m_Reason;
        if (auto disruption = trial.Disruption(*this, mode == LinkMode::Force); !disruption)
            return disruption.GetError();
    }
    *this = std::move(trial);
    return link.m_Id;
}

crude_blueprint::Result<void> crude_blueprint::Graph::CanConnect(Id from, Id to, LinkMode mode) const
{
    const auto next = m_Blueprint->m_NextId;
    Graph trial = *this;
    auto result = trial.Connect(from, to, mode);
    m_Blueprint->m_NextId = next;
    if (!result)
        return result.GetError();
    return {};
}

crude_blueprint::Result<std::vector<crude_blueprint::Id>> crude_blueprint::Graph::ForceDisables(Id from, Id to) const
{
    const auto next = m_Blueprint->m_NextId;
    Graph trial = *this;
    auto result = trial.Connect(from, to, LinkMode::Force);
    m_Blueprint->m_NextId = next;
    if (!result)
        return result.GetError();
    std::vector<Id> disabled;
    for (auto& link : m_Links)
        if (auto after = trial.FindLink(link.m_Id); link.m_Active && after && !after->m_Active)
            disabled.push_back(link.m_Id);
    return disabled;
}

crude_blueprint::Result<std::vector<crude_blueprint::NewNodeFit>> crude_blueprint::Graph::FitNewNodes(Id pin, std::span<const NodeType* const> types) const
{
    auto dragged = FindPin(pin);
    if (!dragged)
        return Error(BlueprintError::NotFound, "no pin " + std::to_string(pin) + " in " + m_Name);
    std::vector<NewNodeFit> fits;
    if (IsA<PlaceholderNodeType>(NodeOf(pin)->m_Type))
        return fits;

    // Without the link the new one would replace, as Connect() leaves the graph.
    const auto output = dragged->m_Direction == Direction::Output;
    const auto flow   = dragged->m_Flow;
    const auto next   = m_Blueprint->m_NextId;
    Graph scratch = *this;
    if (std::erase_if(scratch.m_Links, [pin, output, flow](const Link& link) { return flow ? output && link.m_From == pin : !output && link.m_To == pin; }))
        scratch.Update();
    TypeResolution resolution(m_Blueprint->GetTypes());
    const auto bases = scratch.Resolve(resolution);
    m_Blueprint->m_NextId = next;

    // Its node can lose it with that link.
    auto at = scratch.m_PinIndex.find(pin);
    if (at == scratch.m_PinIndex.end())
        return fits;
    auto& [nodeIndex, pinIndex] = at->second;
    const auto& resolved = scratch.m_Nodes[nodeIndex].m_Pins[pinIndex];
    const auto  term     = Offset(resolved.m_Type, bases[nodeIndex]);
    const auto  decided  = resolved.m_DecidedType;

    const std::string why = "a link to a new node";
    const NodeConfig  config;
    const auto&       registry = m_Blueprint->GetRegistry();
    for (auto type : types)
    {
        NodeDeclaration declaration(config);
        type->Declare(declaration, config, {});
        const auto base = resolution.Size();
        for (auto& variable : declaration.Variables())
            resolution.AddVariable(type->m_Title + "." + variable.m_Name, variable.m_Allowed);

        std::optional<NewNodeFit> plain, cast;
        for (auto& candidate : declaration.Pins())
        {
            if ((candidate.m_Direction == Direction::Output) == output || candidate.m_Flow != flow)
                continue;
            if (flow)
            {
                plain = NewNodeFit{ type, candidate.m_Key, {} };
                break;
            }
            auto candidateTerm = Offset(candidate.m_Type, base);
            resolution.Begin();
            auto unified = resolution.Unify(term, candidateTerm, why);
            resolution.Rollback();
            if (unified)
            {
                plain = NewNodeFit{ type, candidate.m_Key, {} };
                break;
            }
            auto candidateType = cast || !decided ? nullptr : resolution.TypeOf(candidateTerm);
            if (!candidateType)
                continue;
            if (auto info = output ? registry.FindCast(decided, candidateType) : registry.FindCast(candidateType, decided))
                cast = NewNodeFit{ type, candidate.m_Key, info->Name() };
        }
        if (plain || cast)
            fits.push_back(plain ? std::move(*plain) : std::move(*cast));
    }
    return fits;
}

crude_blueprint::Result<void> crude_blueprint::Graph::Disconnect(Id link)
{
    if (!FindLink(link))
        return Error(BlueprintError::NotFound, "no link " + std::to_string(link) + " in " + m_Name);
    std::erase_if(m_Links, [link](const Link& other) { return other.m_Id == link; });
    Update();
    return {};
}

crude_blueprint::Result<void> crude_blueprint::Graph::SetConfig(Id node, NodeConfig config, bool force)
{
    if (!FindNode(node))
        return Error(BlueprintError::NotFound, "no node " + std::to_string(node) + " in " + m_Name);
    if (auto editable = Editable(*FindNode(node)); !editable)
        return editable;
    if (auto savable = Savable(config, FindNode(node)->Name()); !savable)
        return savable;
    Graph trial = *this;
    trial.MutableNode(node)->m_Config = std::move(config);
    trial.Update();
    if (auto disruption = trial.Disruption(*this, force); !disruption)
        return disruption;
    *this = std::move(trial);
    return {};
}

crude_blueprint::Result<void> crude_blueprint::Graph::AddGroupPin(Id node, std::string_view group)
{
    auto changed = MutableNode(node);
    if (!changed)
        return Error(BlueprintError::NotFound, "no node " + std::to_string(node) + " in " + m_Name);
    if (auto editable = Editable(*changed); !editable)
        return editable;
    auto declared = std::find_if(changed->m_Groups.begin(), changed->m_Groups.end(), [group](const GroupDeclaration& other) { return other.m_Name == group; });
    if (declared == changed->m_Groups.end())
        return Error(BlueprintError::NotFound, changed->Name() + " has no group " + std::string(group));
    auto count = GroupCount(*declared, changed->m_Config);
    if (count >= declared->m_Max)
        return Error(BlueprintError::OutOfRange, changed->Name() + "." + declared->m_Name + " has its most pins, " + std::to_string(declared->m_Max));
    changed->m_Config.m_Groups[declared->m_Name] = count + 1;
    Update();
    return {};
}

crude_blueprint::Result<void> crude_blueprint::Graph::RemoveGroupPin(Id node, std::string_view group, int index)
{
    auto changed = MutableNode(node);
    if (!changed)
        return Error(BlueprintError::NotFound, "no node " + std::to_string(node) + " in " + m_Name);
    if (auto editable = Editable(*changed); !editable)
        return editable;
    auto declared = std::find_if(changed->m_Groups.begin(), changed->m_Groups.end(), [group](const GroupDeclaration& other) { return other.m_Name == group; });
    if (declared == changed->m_Groups.end())
        return Error(BlueprintError::NotFound, changed->Name() + " has no group " + std::string(group));
    auto count = GroupCount(*declared, changed->m_Config);
    if (index < 0 || index >= count)
        return Error(BlueprintError::OutOfRange, changed->Name() + "." + declared->m_Name + " has no index " + std::to_string(index));
    if (count <= declared->m_Min)
        return Error(BlueprintError::OutOfRange, changed->Name() + "." + declared->m_Name + " has its fewest pins, " + std::to_string(declared->m_Min));

    std::vector<Id>                        removed;
    std::map<std::string, Id, std::less<>> keyIds;
    for (auto& [key, id] : changed->m_KeyIds)
    {
        if (auto moved = AfterRemoval(key, *declared, index))
            keyIds.emplace(std::move(*moved), id);
        else
            removed.push_back(id);
    }
    std::vector<std::string> split;
    for (auto& key : changed->m_Config.m_Split)
        if (auto moved = AfterRemoval(key, *declared, index))
            split.push_back(std::move(*moved));
    changed->m_KeyIds = std::move(keyIds);
    changed->m_Config.m_Split = std::move(split);
    changed->m_Config.m_Groups[declared->m_Name] = count - 1;
    std::erase_if(m_Links, [&removed](const Link& link) { return std::find(removed.begin(), removed.end(), link.m_From) != removed.end() || std::find(removed.begin(), removed.end(), link.m_To) != removed.end(); });
    for (auto id : removed)
        changed->m_Values.erase(id);
    Update();
    return {};
}

crude_blueprint::Result<void> crude_blueprint::Graph::SplitPin(Id node, std::string_view key, bool split)
{
    auto changed = MutableNode(node);
    if (!changed)
        return Error(BlueprintError::NotFound, "no node " + std::to_string(node) + " in " + m_Name);
    if (auto editable = Editable(*changed); !editable)
        return editable;
    auto& list   = changed->m_Config.m_Split;
    auto  listed = std::find(list.begin(), list.end(), key) != list.end();
    if (split)
    {
        if (listed)
            return {};
        auto pin = changed->FindPin(key);
        if (!pin)
            return Error(BlueprintError::NotFound, changed->Name() + " has no pin " + std::string(key));
        if (!SplittableStruct(*pin))
            return Error(BlueprintError::WrongType, changed->Name() + "." + std::string(key) + " isn't a struct pin");
        list.emplace_back(key);
    }
    else
    {
        if (!listed)
            return Error(BlueprintError::NotFound, changed->Name() + "." + std::string(key) + " isn't split");
        auto prefix = std::string(key) + ".";
        std::erase_if(list, [key, &prefix](const std::string& other) { return other == key || other.starts_with(prefix); });
    }
    Update();
    return {};
}

crude_blueprint::Result<void> crude_blueprint::Graph::SetValue(Id pin, Value value)
{
    auto found = FindPin(pin);
    if (!found)
        return Error(BlueprintError::NotFound, "no pin " + std::to_string(pin) + " in " + m_Name);
    if (auto editable = Editable(*NodeOf(pin)); !editable)
        return editable;
    if (found->m_Flow || found->m_Direction != Direction::Input)
        return Error(BlueprintError::WrongType, PinName(*found) + " takes no value");
    auto& values = MutableNode(found->m_Node)->m_Values;
    if (!value.IsValid())
    {
        values.erase(pin);
        return {};
    }
    if (!found->m_DecidedType)
        return Error(BlueprintError::Undecided, PinName(*found) + " has no type decided for its value");
    if (value.GetType() != found->m_DecidedType)
        return Error(BlueprintError::WrongType, PinName(*found) + " takes " + found->m_DecidedType->m_Name + ", not " + value.GetType()->m_Name);
    if (auto savable = Savable(value, PinName(*found)); !savable)
        return savable;
    values[pin] = std::move(value);
    return {};
}

bool crude_blueprint::Graph::IsBroken() const
{
    return std::any_of(m_Diagnostics.begin(), m_Diagnostics.end(), [](const Diagnostic& diagnostic) { return diagnostic.m_Severity == Severity::Error; });
}

const crude_blueprint::Node* crude_blueprint::Graph::FindNode(Id node) const
{
    auto it = m_NodeIndex.find(node);
    return it != m_NodeIndex.end() ? &m_Nodes[it->second] : nullptr;
}

crude_blueprint::Node* crude_blueprint::Graph::MutableNode(Id node)
{
    auto it = m_NodeIndex.find(node);
    return it != m_NodeIndex.end() ? &m_Nodes[it->second] : nullptr;
}

const crude_blueprint::Pin* crude_blueprint::Graph::FindPin(Id pin) const
{
    auto it = m_PinIndex.find(pin);
    return it != m_PinIndex.end() ? &m_Nodes[it->second.first].m_Pins[it->second.second] : nullptr;
}

const crude_blueprint::Node* crude_blueprint::Graph::NodeOf(Id pin) const
{
    auto it = m_PinIndex.find(pin);
    return it != m_PinIndex.end() ? &m_Nodes[it->second.first] : nullptr;
}

const crude_blueprint::Link* crude_blueprint::Graph::FindLink(Id link) const
{
    for (auto& other : m_Links)
        if (other.m_Id == link)
            return &other;
    return nullptr;
}

const crude_blueprint::Value* crude_blueprint::Graph::FindValue(Id pin) const
{
    auto node = NodeOf(pin);
    if (!node)
        return nullptr;
    auto it = node->m_Values.find(pin);
    return it != node->m_Values.end() ? &it->second : nullptr;
}

std::string crude_blueprint::Graph::TypeText(Id pin) const
{
    auto found = FindPin(pin);
    if (!found || found->m_Flow)
        return {};
    return found->m_DecidedType ? found->m_DecidedType->m_Name : found->m_Wildcard;
}

std::string crude_blueprint::Graph::PinName(const Pin& pin) const
{
    return NodeOf(pin.m_Id)->Name() + "." + pin.m_Key;
}

std::string crude_blueprint::Graph::LinkName(const Link& link) const
{
    return LinkName(*FindPin(link.m_From), *FindPin(link.m_To));
}

std::string crude_blueprint::Graph::LinkName(const Pin& from, const Pin& to) const
{
    return PinName(from) + " -> " + PinName(to);
}

crude_blueprint::Result<void> crude_blueprint::Graph::CheckLink(const Pin& from, const Pin& to) const
{
    if (from.m_Direction != Direction::Output || to.m_Direction != Direction::Input)
        return Error(BlueprintError::BadLink, "a link goes from an output to an input");
    if (from.m_Flow != to.m_Flow)
        return Error(BlueprintError::BadLink, "a flow pin links only to a flow pin");
    if (from.m_Node == to.m_Node)
        return Error(BlueprintError::BadLink, "a link joins two nodes");
    return {};
}

crude_blueprint::Result<void> crude_blueprint::Graph::Editable(const Node& node) const
{
    if (IsA<PlaceholderNodeType>(node.m_Type))
        return Error(BlueprintError::Unsupported, node.Name() + " stands in for a node this blueprint can't use, and stays as its file had it");
    return {};
}

// When the input's node already feeds the output's, through active data links of pure nodes.
bool crude_blueprint::Graph::ClosesPureLoop(const Pin& from, const Pin& to) const
{
    auto isPure = [](const Node& node) { return std::none_of(node.m_Pins.begin(), node.m_Pins.end(), [](const Pin& pin) { return pin.m_Flow; }); };
    auto source = NodeOf(from.m_Id);
    auto target = NodeOf(to.m_Id);
    if (from.m_Flow || !isPure(*source) || !isPure(*target))
        return false;

    std::unordered_map<Id, std::vector<Id>> feeds;
    for (auto& link : m_Links)
    {
        auto output = link.m_Active ? FindPin(link.m_From) : nullptr;
        if (!output || output->m_Flow)
            continue;
        auto fed = NodeOf(link.m_To);
        if (isPure(*NodeOf(output->m_Id)) && isPure(*fed))
            feeds[output->m_Node].push_back(fed->m_Id);
    }

    std::vector<Id>        stack = { target->m_Id };
    std::unordered_set<Id> seen;
    while (!stack.empty())
    {
        auto node = stack.back();
        stack.pop_back();
        if (node == source->m_Id)
            return true;
        if (!seen.insert(node).second)
            continue;
        if (auto it = feeds.find(node); it != feeds.end())
            stack.insert(stack.end(), it->second.begin(), it->second.end());
    }
    return false;
}

// Pins that depend on types repeat the two steps until the pins settle: nodes declare their pins
// from their bindings, and the links decide the types, which give new bindings.
void crude_blueprint::Graph::Update()
{
    const int c_MaxRounds = 8;
    for (int round = 1; ; ++round)
    {
        Declare();
        TypeResolution resolution(m_Blueprint->GetTypes());
        auto bases = Resolve(resolution);

        std::vector<const Node*> changed;
        for (size_t i = 0; i < m_Nodes.size(); ++i)
        {
            auto& node = m_Nodes[i];
            Bindings bindings;
            for (size_t v = 0; v < node.m_Variables.size(); ++v)
                if (auto type = resolution.TypeOf(TypeVariable{ bases[i] + static_cast<int>(v) }))
                    bindings.m_Types[node.m_Variables[v].m_Name] = type;
            if (bindings != node.m_Bindings)
            {
                node.m_Bindings = std::move(bindings);
                changed.push_back(&node);
            }
        }

        if (changed.empty())
            break;
        if (round == c_MaxRounds)
        {
            for (auto node : changed)
                m_Problems.push_back({ node->m_Id, Error(BlueprintError::Unsettled, "the types of " + node->Name() + " don't settle") });
            break;
        }
    }

    for (auto& node : m_Nodes)
    {
        std::erase_if(node.m_Values, [this](const auto& entry)
        {
            auto pin = FindPin(entry.first);
            return pin && pin->m_DecidedType && pin->m_DecidedType != entry.second.GetType();
        });
    }
    Diagnose();
}

void crude_blueprint::Graph::Declare()
{
    m_NodeIndex.clear();
    m_PinIndex.clear();
    for (size_t i = 0; i < m_Nodes.size(); ++i)
    {
        auto& node = m_Nodes[i];
        m_NodeIndex[node.m_Id] = i;

        NodeDeclaration declaration(node.m_Config);
        node.m_Type->Declare(declaration, node.m_Config, node.m_Bindings);
        node.m_Splits.clear();
        Split(declaration.m_Pins, node.m_Config.m_Split, node.m_Splits);
        node.m_Variables = std::move(declaration.m_Variables);
        node.m_Groups    = std::move(declaration.m_Groups);
        node.m_Pins.clear();
        for (auto& declared : declaration.m_Pins)
        {
            auto [it, added] = node.m_KeyIds.try_emplace(declared.m_Key, 0);
            if (added)
                it->second = m_Blueprint->NewId();
            Pin pin;
            static_cast<PinDeclaration&>(pin) = std::move(declared);
            pin.m_Id   = it->second;
            pin.m_Node = node.m_Id;
            m_PinIndex[pin.m_Id] = { i, node.m_Pins.size() };
            node.m_Pins.push_back(std::move(pin));
        }
    }
}

// Gives each node's first variable.
std::vector<int> crude_blueprint::Graph::Resolve(TypeResolution& resolution)
{
    m_Problems.clear();

    // A type the user chose counts as a link made before any other.
    std::vector<int> bases(m_Nodes.size());
    for (size_t i = 0; i < m_Nodes.size(); ++i)
    {
        auto& node = m_Nodes[i];
        if (auto placeholder = Cast<PlaceholderNodeType>(node.m_Type))
            m_Problems.push_back({ node.m_Id, placeholder->m_Reason });
        bases[i] = resolution.Size();
        for (auto& variable : node.m_Variables)
            resolution.AddVariable(node.Name() + "." + variable.m_Name, variable.m_Allowed);
        for (size_t v = 0; v < node.m_Variables.size(); ++v)
        {
            auto chosen = node.m_Config.m_Types.find(node.m_Variables[v].m_Name);
            if (chosen == node.m_Config.m_Types.end())
                continue;
            resolution.Begin();
            if (auto result = resolution.Unify(TypeVariable{ bases[i] + static_cast<int>(v) }, *chosen->second, "the type chosen on " + node.Name()); !result)
            {
                resolution.Rollback();
                m_Problems.push_back({ node.m_Id, result.GetError() });
            }
            else
                resolution.Commit();
        }
    }

    // Forced links first, the latest first; then the others, in the order they were made.
    std::vector<size_t> order(m_Links.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::stable_sort(order.begin(), order.end(), [this](size_t a, size_t b) { return m_Links[a].m_Forced > m_Links[b].m_Forced; });

    auto termOf = [this, &bases](const Pin& pin) { return Offset(pin.m_Type, bases[m_PinIndex.find(pin.m_Id)->second.first]); };
    auto disable = [](Link& link, Error reason)
    {
        link.m_Active = false;
        link.m_Reason = std::move(reason);
    };

    for (auto index : order)
    {
        auto& link = m_Links[index];
        link.m_Active = true;
        link.m_Reason = {};
        auto from = FindPin(link.m_From);
        auto to   = FindPin(link.m_To);
        if (!from || !to)
        {
            disable(link, Error(BlueprintError::NoPin, "its pin isn't on the node now"));
            continue;
        }
        if (auto check = CheckLink(*from, *to); !check)
        {
            disable(link, check.GetError());
            continue;
        }
        auto placeholder = IsA<PlaceholderNodeType>(NodeOf(from->m_Id)->m_Type) ? NodeOf(from->m_Id) : IsA<PlaceholderNodeType>(NodeOf(to->m_Id)->m_Type) ? NodeOf(to->m_Id) : nullptr;
        if (placeholder)
        {
            disable(link, Error(BlueprintError::Unsupported, "it links to " + placeholder->Name() + ", which stands in for a node this blueprint can't use"));
            continue;
        }
        if (from->m_Flow || !link.m_Cast.empty())
            continue;
        resolution.Begin();
        if (auto result = resolution.Unify(termOf(*from), termOf(*to), LinkName(link)); !result)
        {
            resolution.Rollback();
            disable(link, result.GetError());
        }
        else
            resolution.Commit();
    }

    for (size_t i = 0; i < m_Nodes.size(); ++i)
    {
        for (auto& pin : m_Nodes[i].m_Pins)
        {
            if (pin.m_Flow)
                continue;
            auto term = Offset(pin.m_Type, bases[i]);
            pin.m_DecidedType = resolution.TypeOf(term);
            pin.m_Wildcard    = pin.m_DecidedType ? std::string() : resolution.Text(term);
        }
    }

    // A cast link joins two types that the rest of the graph decides.
    for (auto index : order)
    {
        auto& link = m_Links[index];
        if (link.m_Cast.empty() || !link.m_Active)
            continue;
        auto from = FindPin(link.m_From);
        auto to   = FindPin(link.m_To);
        if (!from->m_DecidedType || !to->m_DecidedType)
        {
            disable(link, Error(BlueprintError::Undecided, "a cast needs both types decided"));
            continue;
        }
        auto cast = from->m_DecidedType->m_Name + "->" + to->m_DecidedType->m_Name;
        if (cast != link.m_Cast)
            disable(link, Error(BlueprintError::NoCast, "its cast is " + link.m_Cast + ", not " + cast));
        else if (!m_Blueprint->GetRegistry().FindCast(from->m_DecidedType, to->m_DecidedType))
            disable(link, Error(BlueprintError::NoCast, "no cast " + cast + " is registered"));
    }

    return bases;
}

void crude_blueprint::Graph::Diagnose()
{
    m_Diagnostics.clear();
    for (auto& problem : m_Problems)
        m_Diagnostics.push_back({ problem.m_Error, Severity::Error, m_Id, problem.m_Node, 0, 0 });
    for (auto& link : m_Links)
        if (!link.m_Active)
            m_Diagnostics.push_back({ link.m_Reason, Severity::Error, m_Id, 0, 0, link.m_Id });

    std::unordered_set<Id> linked;
    for (auto& link : m_Links)
    {
        if (!link.m_Active)
            continue;
        linked.insert(link.m_From);
        linked.insert(link.m_To);
    }
    for (auto& node : m_Nodes)
    {
        for (auto& pin : node.m_Pins)
        {
            if (pin.m_Requirement == Requirement::Optional || linked.count(pin.m_Id))
                continue;
            auto what     = pin.m_Direction == Direction::Input ? " needs a link" : pin.m_Flow ? " leads nowhere" : "'s result isn't used";
            auto severity = pin.m_Requirement == Requirement::Required ? Severity::Error : Severity::Warning;
            m_Diagnostics.push_back({ Error(BlueprintError::NeedsLink, PinName(pin) + what), severity, m_Id, node.m_Id, pin.m_Id, 0 });
        }
    }
}

// What an edit broke that wasn't broken before: a link it disabled, a node whose types it unsettled,
// or a chosen type that no longer fits. A forced edit may do the first two.
crude_blueprint::Result<void> crude_blueprint::Graph::Disruption(const Graph& before, bool forced) const
{
    for (auto& problem : m_Problems)
    {
        if (forced && !problem.m_Error.Is(BlueprintError::TypeMismatch))
            continue;
        auto known = std::any_of(before.m_Problems.begin(), before.m_Problems.end(), [&problem](const Problem& other) { return other.m_Node == problem.m_Node && other.m_Error == problem.m_Error; });
        if (!known)
            return problem.m_Error;
    }
    if (forced)
        return {};
    for (auto& link : m_Links)
    {
        if (link.m_Active)
            continue;
        if (auto old = before.FindLink(link.m_Id); old && old->m_Active)
            return Error(link.m_Reason.m_Category, link.m_Reason.m_Code, before.LinkName(*old) + " would be disabled: " + std::string(link.m_Reason.Message()));
    }
    return {};
}


//
// -------[ Blueprints ]-------
//

crude_blueprint::Blueprint::Blueprint(std::shared_ptr<const Registry> registry, std::shared_ptr<TypeTable> types)
    : m_Registry(std::move(registry))
    , m_Types(std::move(types))
{
    CRUDE_BP_ASSERT(m_Registry && m_Types);
}

crude_blueprint::Graph& crude_blueprint::Blueprint::AddGraph(std::string name)
{
    return *m_Graphs.emplace_back(std::make_unique<Graph>(*this, NewId(), std::move(name)));
}

crude_blueprint::Graph* crude_blueprint::Blueprint::FindGraph(Id graph)
{
    for (auto& other : m_Graphs)
        if (other->GetId() == graph)
            return other.get();
    return nullptr;
}

const crude_blueprint::Graph* crude_blueprint::Blueprint::FindGraph(Id graph) const
{
    for (auto& other : m_Graphs)
        if (other->GetId() == graph)
            return other.get();
    return nullptr;
}

const crude_blueprint::NodeType* crude_blueprint::Blueprint::FindNodeType(std::string_view name) const
{
    return m_Registry->FindNodeType(name);
}


//
// -------[ Programs ]-------
//

namespace crude_blueprint { namespace {

// Node code may throw; the run turns that into an error.
template <typename Function>
auto Contained(Function&& function) -> decltype(function())
{
    try
    {
        return function();
    }
    catch (const std::exception& exception)
    {
        return Error(BlueprintError::Threw, exception.what());
    }
    catch (...)
    {
        return Error(BlueprintError::Threw, "it threw");
    }
}

int FindPinByKey(const ProgramNode& node, std::string_view key)
{
    for (size_t i = 0; i < node.m_Pins.size(); ++i)
        if (node.m_Pins[i].m_Key == key)
            return static_cast<int>(i);
    return -1;
}

const SplitPin* FindSplit(const ProgramNode& node, std::string_view key, Direction direction)
{
    for (auto& split : node.m_Splits)
        if (split.m_Key == key && split.m_Direction == direction)
            return &split;
    return nullptr;
}

std::span<const StructField> FieldsOf(const SplitPin& split)
{
    return static_cast<const StructType*>(split.m_Type->m_Head)->Fields();
}

} } // namespace crude_blueprint { namespace {

crude_blueprint::Result<crude_blueprint::Exit> crude_blueprint::NodeType::Execute(ExecContext&, std::string_view) const
{
    return Error(BlueprintError::Unsupported, m_Name + " doesn't run");
}

crude_blueprint::Result<void> crude_blueprint::NodeType::Evaluate(EvalContext&) const
{
    return Error(BlueprintError::Unsupported, m_Name + " doesn't compute");
}

std::string crude_blueprint::NodeType::EventName(const NodeConfig&) const
{
    return {};
}

int crude_blueprint::Program::FindNode(Id node) const
{
    for (size_t i = 0; i < m_Nodes.size(); ++i)
        if (m_Nodes[i].m_Id == node)
            return static_cast<int>(i);
    return -1;
}

int crude_blueprint::Program::FindEvent(std::string_view name) const
{
    auto it = m_Events.find(name);
    return it != m_Events.end() ? it->second : -1;
}

crude_blueprint::CompileResult crude_blueprint::Compile(const Blueprint& blueprint)
{
    CompileResult result;
    auto program = std::make_shared<Program>();
    auto failed  = false;
    auto error   = [&result, &failed](Error what, Id graph, Id node, Id pin)
    {
        result.m_Diagnostics.push_back({ std::move(what), Severity::Error, graph, node, pin, 0 });
        failed = true;
    };

    std::unordered_map<Id, int> records; // each node's, by its ID
    for (auto& graph : blueprint.Graphs())
    {
        for (auto& diagnostic : graph->Diagnostics())
        {
            result.m_Diagnostics.push_back(diagnostic);
            failed = failed || diagnostic.m_Severity == Severity::Error;
        }

        for (auto& node : graph->Nodes())
        {
            const auto index = static_cast<int>(program->m_Nodes.size());
            records[node.m_Id] = index;
            auto& record = program->m_Nodes.emplace_back();
            record.m_Id     = node.m_Id;
            record.m_Graph  = graph->GetId();
            record.m_Type   = node.m_Type;
            record.m_Config = node.m_Config;
            record.m_Name   = node.Name();
            record.m_Pure   = std::none_of(node.m_Pins.begin(), node.m_Pins.end(), [](const Pin& pin) { return pin.m_Flow; });
            record.m_Splits = node.m_Splits;
            for (auto& pin : node.m_Pins)
            {
                auto& compiled = record.m_Pins.emplace_back();
                compiled.m_Id        = pin.m_Id;
                compiled.m_Key       = pin.m_Key;
                compiled.m_Direction = pin.m_Direction;
                compiled.m_Flow      = pin.m_Flow;
                compiled.m_Type      = pin.m_DecidedType;
                if (!pin.m_Flow && !pin.m_DecidedType)
                    error(Error(BlueprintError::Undecided, node.Name() + "." + pin.m_Key + "'s type isn't decided"), graph->GetId(), node.m_Id, pin.m_Id);
                if (!pin.m_Flow && pin.m_Direction == Direction::Output)
                {
                    compiled.m_Slot = static_cast<int>(program->m_Slots.size());
                    program->m_Slots.emplace_back(index, static_cast<int>(record.m_Pins.size()) - 1);
                }
                program->m_Pins[pin.m_Id] = { index, static_cast<int>(record.m_Pins.size()) - 1 };
            }

            auto event = node.m_Type->EventName(node.m_Config);
            if (!event.empty() && !program->m_Events.emplace(event, index).second)
                error(Error(BlueprintError::Duplicate, node.Name() + " starts " + event + ", as another node does"), graph->GetId(), node.m_Id, 0);
        }
    }

    for (auto& graph : blueprint.Graphs())
    {
        for (auto& link : graph->Links())
        {
            if (!link.m_Active)
                continue;
            auto [fromNode, fromPin] = program->m_Pins.at(link.m_From);
            auto [toNode, toPin]     = program->m_Pins.at(link.m_To);
            auto& from = program->m_Nodes[fromNode].m_Pins[fromPin];
            auto& to   = program->m_Nodes[toNode].m_Pins[toPin];
            if (from.m_Flow)
            {
                from.m_Target    = toNode;
                from.m_TargetPin = toPin;
                from.m_Link      = link.m_Id;
                continue;
            }
            to.m_Source = from.m_Slot;
            to.m_Link   = link.m_Id;
            if (!link.m_Cast.empty())
                to.m_Cast = blueprint.GetRegistry().FindCast(from.m_Type, to.m_Type);
        }

        // An unlinked input reads the value the user typed, else the declared default, else its type's.
        for (auto& node : graph->Nodes())
        {
            auto& record = program->m_Nodes[records.at(node.m_Id)];
            for (size_t i = 0; i < node.m_Pins.size(); ++i)
            {
                auto& pin      = node.m_Pins[i];
                auto& compiled = record.m_Pins[i];
                if (pin.m_Flow || pin.m_Direction != Direction::Input || compiled.m_Source >= 0 || !pin.m_DecidedType)
                    continue;
                auto typed = node.m_Values.find(pin.m_Id);
                if (typed != node.m_Values.end() && typed->second.GetType() == pin.m_DecidedType)
                    compiled.m_Constant = typed->second;
                else if (pin.m_Default.IsValid() && pin.m_Default.GetType() == pin.m_DecidedType)
                    compiled.m_Constant = pin.m_Default;
                else
                    compiled.m_Constant = pin.m_DecidedType->m_Head->Default(*pin.m_DecidedType);
            }
        }
    }

    // Kept links can close a loop of pure nodes, which no edit makes. A loop is named by the node
    // its search came back to, which is on the loop, unlike nodes that only read from it. The search
    // keeps its own stack, as a file can chain nodes deeper than the call stack goes.
    std::vector<int> state(program->m_Nodes.size(), 0); // 0: unseen, 1: on the search's path, 2: done
    std::vector<std::pair<int, size_t>> path;          // nodes, and the next of their pins to follow
    for (size_t root = 0; root < program->m_Nodes.size(); ++root)
    {
        if (!program->m_Nodes[root].m_Pure || state[root] != 0)
            continue;
        auto loop = -1;
        state[root] = 1;
        path.assign(1, { static_cast<int>(root), 0 });
        while (!path.empty() && loop < 0)
        {
            auto  node = path.back().first;
            auto& pins = program->m_Nodes[node].m_Pins;
            if (path.back().second == pins.size())
            {
                state[node] = 2;
                path.pop_back();
                continue;
            }
            auto& pin = pins[path.back().second++];
            if (pin.m_Flow || pin.m_Direction != Direction::Input || pin.m_Source < 0)
                continue;
            auto source = program->m_Slots[pin.m_Source].first;
            if (!program->m_Nodes[source].m_Pure)
                continue;
            if (state[source] == 1)
                loop = source;
            else if (state[source] == 0)
            {
                state[source] = 1;
                path.emplace_back(source, 0);
            }
        }
        for (auto& [node, next] : path)
            state[node] = 2;
        if (loop >= 0)
        {
            auto& node = program->m_Nodes[loop];
            error(Error(BlueprintError::Loop, node.m_Name + " is in a loop of pure nodes"), node.m_Graph, node.m_Id, 0);
        }
    }

    if (!failed)
        result.m_Program = std::move(program);
    return result;
}


//
// -------[ Contexts ]-------
//

crude_blueprint::Result<crude_blueprint::Value> crude_blueprint::NodeContext::Get(std::string_view input)
{
    auto& node = m_Context.m_Program->m_Nodes[m_Node];
    auto  pin  = FindPinByKey(node, input);
    if (pin < 0)
    {
        auto split = FindSplit(node, input, Direction::Input);
        CRUDE_BP_ASSERT(split && "no data input of that key");
        if (!split)
            return Error(BlueprintError::NotFound, node.m_Name + " has no input " + std::string(input));
        std::vector<Value> fields;
        for (auto& field : FieldsOf(*split))
        {
            auto value = Get(std::string(input) + "." + field.m_Name);
            if (!value)
                return value.GetError();
            fields.push_back(std::move(value).GetValue());
        }
        return Value::Struct(*split->m_Type, std::move(fields));
    }
    CRUDE_BP_ASSERT(!node.m_Pins[pin].m_Flow && node.m_Pins[pin].m_Direction == Direction::Input && "no data input of that key");
    return m_Context.Read(m_Node, pin);
}

crude_blueprint::Result<std::vector<crude_blueprint::Value>> crude_blueprint::NodeContext::Inputs(std::string_view prefix)
{
    auto& node = m_Context.m_Program->m_Nodes[m_Node];
    std::vector<Value> values;
    for (int index = 0;; ++index)
    {
        auto key = std::string(prefix) + "." + std::to_string(index);
        if (FindPinByKey(node, key) < 0 && !FindSplit(node, key, Direction::Input))
            return values;
        auto value = Get(key);
        if (!value)
            return value.GetError();
        values.push_back(std::move(value).GetValue());
    }
}

void crude_blueprint::NodeContext::Set(std::string_view output, Value value)
{
    auto& node = m_Context.m_Program->m_Nodes[m_Node];
    auto  pin  = FindPinByKey(node, output);
    if (pin < 0)
    {
        auto split = FindSplit(node, output, Direction::Output);
        CRUDE_BP_ASSERT(split && value.GetType() == split->m_Type && "no data output of that key, or a value of another type than the output's");
        if (!split || value.GetType() != split->m_Type)
            return;
        auto fields = FieldsOf(*split);
        for (size_t i = 0; i < fields.size(); ++i)
            Set(std::string(output) + "." + fields[i].m_Name, value.AsStruct()[i]);
        return;
    }
    CRUDE_BP_ASSERT(node.m_Pins[pin].m_Slot >= 0 && "no data output of that key");
    if (node.m_Pins[pin].m_Slot < 0)
        return;
    auto type = node.m_Pins[pin].m_Type;
    CRUDE_BP_ASSERT(value.GetType() == type && "a value of another type than the output's");
    if (value.GetType() != type)
        value = type->m_Head->Default(*type);
    m_Context.Write(m_Node, pin, std::move(value));
}

const crude_blueprint::Type* crude_blueprint::NodeContext::TypeOf(std::string_view pin) const
{
    auto& node  = m_Context.m_Program->m_Nodes[m_Node];
    auto  found = FindPinByKey(node, pin);
    if (found >= 0)
        return node.m_Pins[found].m_Type;
    for (auto& split : node.m_Splits)
        if (split.m_Key == pin)
            return split.m_Type;
    return nullptr;
}

bool crude_blueprint::NodeContext::HasPin(std::string_view pin) const
{
    auto& node = m_Context.m_Program->m_Nodes[m_Node];
    return FindPinByKey(node, pin) >= 0 || FindSplit(node, pin, Direction::Input) || FindSplit(node, pin, Direction::Output);
}

const crude_blueprint::NodeConfig& crude_blueprint::NodeContext::Config() const
{
    return m_Context.m_Program->m_Nodes[m_Node].m_Config;
}

void crude_blueprint::NodeContext::Log(std::string_view text)
{
    if (m_Context.m_Monitor)
        m_Context.m_Monitor->OnLog(m_Context, m_Context.m_Program->m_Nodes[m_Node].m_Id, text);
}

std::any& crude_blueprint::NodeContext::StateSlot()
{
    return m_Context.m_States[m_Node];
}

crude_blueprint::Error crude_blueprint::NodeContext::NotA(std::string_view input, const Type& type) const
{
    return Error(BlueprintError::WrongType, m_Context.m_Program->m_Nodes[m_Node].m_Name + "." + std::string(input) + " isn't " + (std::string_view("AEIOU").find(type.m_Name[0]) != std::string_view::npos ? "an " : "a ") + type.m_Name);
}

crude_blueprint::Context::Context(std::shared_ptr<const Program> program)
    : m_Program(std::move(program))
{
    CRUDE_BP_ASSERT(m_Program);
    m_Slots.resize(m_Program->m_Slots.size());
    m_SlotSteps.resize(m_Program->m_Slots.size(), 0);
    m_Computed.resize(m_Program->m_Nodes.size(), 0);
    m_States.resize(m_Program->m_Nodes.size());
    m_Breakpoints.resize(m_Program->m_Nodes.size(), false);
}

crude_blueprint::Result<crude_blueprint::FiberId> crude_blueprint::Context::Start(std::string_view event)
{
    auto node = m_Program->FindEvent(event);
    if (node < 0)
        return Error(BlueprintError::NotFound, "no node starts " + std::string(event));
    auto& fiber = m_Fibers.emplace_back();
    fiber.m_Info.m_Id   = m_NextFiber++;
    fiber.m_Info.m_Node = m_Program->m_Nodes[node].m_Id;
    fiber.m_Node        = node;
    return fiber.m_Info.m_Id;
}

crude_blueprint::Result<crude_blueprint::FiberState> crude_blueprint::Context::Step(FiberId id)
{
    auto fiber = FindFiber(id);
    if (!fiber)
        return Error(BlueprintError::NotFound, "no fiber " + std::to_string(id));
    if (fiber->m_Info.m_State != FiberState::Ready)
        return Error(BlueprintError::Unsupported, "fiber " + std::to_string(id) + " has ended");

    ++m_Step;
    ++m_Generation;
    m_ErrorNode = -1;
    m_ErrorLink = 0;

    const auto  index = fiber->m_Node;
    const auto  entry = fiber->m_Entry;
    auto&       node  = m_Program->m_Nodes[index];
    fiber->m_Info.m_Ran          = node.m_Id;
    fiber->m_Info.m_AtBreakpoint = false;
    ExecContext context(*this, index, fiber->m_Again);
    auto        exit  = Contained([&] { return node.m_Type->Execute(context, entry >= 0 ? std::string_view(node.m_Pins[entry].m_Key) : std::string_view()); });
    if (!exit)
        return Fail(*fiber, exit.GetError(), index);

    fiber->m_Node        = -1;
    fiber->m_Entry       = -1;
    fiber->m_Again       = false;
    fiber->m_Info.m_Link = 0;
    auto& taken = exit.GetValue();
    if (taken.m_Kind != Exit::Kind::None)
    {
        auto pin = FindPinByKey(node, taken.m_Pin);
        if (pin < 0 || !node.m_Pins[pin].m_Flow || node.m_Pins[pin].m_Direction != Direction::Output)
            return Fail(*fiber, Error(BlueprintError::NotFound, node.m_Name + " has no flow output " + taken.m_Pin), index);
        if (taken.m_Kind == Exit::Kind::Loop)
            fiber->m_Loops.emplace_back(index, entry);
        fiber->m_Node        = node.m_Pins[pin].m_Target;
        fiber->m_Entry       = node.m_Pins[pin].m_TargetPin;
        fiber->m_Info.m_Link = node.m_Pins[pin].m_Link;
    }
    Settle(*fiber);
    return fiber->m_Info.m_State;
}

crude_blueprint::RunState crude_blueprint::Context::Run(std::chrono::microseconds budget)
{
    const auto deadline = std::chrono::steady_clock::now() + budget;
    for (;;)
    {
        auto ready = false;
        for (size_t i = 0; i < m_Fibers.size(); ++i)
        {
            auto& fiber = m_Fibers[i];
            if (fiber.m_Info.m_State != FiberState::Ready)
                continue;
            if (m_Breakpoints[fiber.m_Node] && !fiber.m_Info.m_AtBreakpoint)
            {
                fiber.m_Info.m_AtBreakpoint = true;
                return RunState::Breakpoint;
            }
            ready = true;
            [[maybe_unused]] auto state = Step(fiber.m_Info.m_Id);
        }
        if (!ready)
            return RunState::Idle;
        if (std::chrono::steady_clock::now() >= deadline)
        {
            auto any = std::any_of(m_Fibers.begin(), m_Fibers.end(), [](const Fiber& fiber) { return fiber.m_Info.m_State == FiberState::Ready; });
            return any ? RunState::Running : RunState::Idle;
        }
    }
}

void crude_blueprint::Context::Stop(FiberId id)
{
    if (auto fiber = FindFiber(id); fiber && fiber->m_Info.m_State == FiberState::Ready)
    {
        fiber->m_Info.m_State        = FiberState::Stopped;
        fiber->m_Info.m_Node         = 0;
        fiber->m_Info.m_AtBreakpoint = false;
    }
}

void crude_blueprint::Context::SetBreakpoint(Id node, bool enabled)
{
    if (auto index = m_Program->FindNode(node); index >= 0)
        m_Breakpoints[index] = enabled;
}

std::vector<crude_blueprint::FiberInfo> crude_blueprint::Context::Fibers() const
{
    std::vector<FiberInfo> fibers;
    for (auto& fiber : m_Fibers)
        fibers.push_back(fiber.m_Info);
    return fibers;
}

crude_blueprint::Result<crude_blueprint::Watch> crude_blueprint::Context::Evaluate(Id pin)
{
    auto it = m_Program->m_Pins.find(pin);
    if (it == m_Program->m_Pins.end())
        return Error(BlueprintError::NotFound, "no pin " + std::to_string(pin) + " in the program");
    auto [node, index] = it->second;
    auto& compiled = m_Program->m_Nodes[node].m_Pins[index];
    if (compiled.m_Flow)
        return Error(BlueprintError::WrongType, "a flow pin has no value");

    m_ErrorNode = -1;
    m_ErrorLink = 0;
    auto slot  = compiled.m_Direction == Direction::Input ? compiled.m_Source : compiled.m_Slot;
    auto value = compiled.m_Direction == Direction::Input ? Read(node, index) : ReadSlot(slot);
    if (!value)
        return value.GetError();
    return Watch{ std::move(value).GetValue(), slot >= 0 ? m_SlotSteps[slot] : 0 };
}

crude_blueprint::Result<crude_blueprint::Value> crude_blueprint::Context::Read(int node, int pin)
{
    auto& compiled = m_Program->m_Nodes[node].m_Pins[pin];
    if (compiled.m_Source < 0)
        return compiled.m_Constant;
    auto value = ReadSlot(compiled.m_Source);
    if (!value || !compiled.m_Cast)
        return value;
    auto cast = compiled.m_Cast->m_Function(value.GetValue());
    if (!cast && m_ErrorNode < 0)
    {
        m_ErrorNode = node;
        m_ErrorLink = compiled.m_Link;
    }
    return cast;
}

// An output that nothing wrote reads as its type's default.
crude_blueprint::Result<crude_blueprint::Value> crude_blueprint::Context::ReadSlot(int slot)
{
    auto [node, pin] = m_Program->m_Slots[slot];
    if (m_Program->m_Nodes[node].m_Pure && m_Computed[node] != m_Generation)
        if (auto computed = EvaluateNode(node); !computed)
            return computed.GetError();
    if (!m_Slots[slot].IsValid())
    {
        auto type = m_Program->m_Nodes[node].m_Pins[pin].m_Type;
        return type->m_Head->Default(*type);
    }
    return m_Slots[slot];
}

// Pure nodes compute in the inputs that read them, so each nests in the call stack. 128 levels take
// about 600 KB where a level is largest, in MSVC's debug builds with AddressSanitizer, of a main
// thread's 1 MB.
crude_blueprint::Result<void> crude_blueprint::Context::EvaluateNode(int node)
{
    const int c_MaxDepth = 128;
    auto&     record     = m_Program->m_Nodes[node];
    if (m_Depth >= c_MaxDepth)
    {
        if (m_ErrorNode < 0)
            m_ErrorNode = node;
        return Error(BlueprintError::TooDeep, record.m_Name + " is computed in inputs nested deeper than " + std::to_string(c_MaxDepth) + " pure nodes");
    }
    ++m_Depth;
    EvalContext context(*this, node);
    auto        result = Contained([&] { return record.m_Type->Evaluate(context); });
    --m_Depth;
    if (!result)
    {
        if (m_ErrorNode < 0)
            m_ErrorNode = node;
        return result;
    }
    m_Computed[node] = m_Generation;
    return {};
}

void crude_blueprint::Context::Write(int node, int pin, Value value)
{
    auto slot = m_Program->m_Nodes[node].m_Pins[pin].m_Slot;
    m_Slots[slot]     = std::move(value);
    m_SlotSteps[slot] = m_Step;
    if (!m_Program->m_Nodes[node].m_Pure)
        ++m_Generation;
}

// A fiber whose branch ended enters its innermost loop again, or is done.
void crude_blueprint::Context::Settle(Fiber& fiber)
{
    if (fiber.m_Node < 0)
    {
        if (fiber.m_Loops.empty())
        {
            fiber.m_Info.m_State = FiberState::Done;
            fiber.m_Info.m_Node  = 0;
            return;
        }
        std::tie(fiber.m_Node, fiber.m_Entry) = fiber.m_Loops.back();
        fiber.m_Loops.pop_back();
        fiber.m_Again = true;
    }
    fiber.m_Info.m_Node = m_Program->m_Nodes[fiber.m_Node].m_Id;
}

crude_blueprint::FiberState crude_blueprint::Context::Fail(Fiber& fiber, Error error, int node)
{
    auto& at = m_Program->m_Nodes[m_ErrorNode >= 0 ? m_ErrorNode : node];
    m_Diagnostics.push_back({ std::move(error), Severity::Error, at.m_Graph, at.m_Id, 0, m_ErrorLink });
    fiber.m_Info.m_State = FiberState::Failed;
    return fiber.m_Info.m_State;
}

crude_blueprint::Context::Fiber* crude_blueprint::Context::FindFiber(FiberId id)
{
    for (auto& fiber : m_Fibers)
        if (fiber.m_Info.m_Id == id)
            return &fiber;
    return nullptr;
}


//
// -------[ Files ]-------
//

namespace crude_blueprint { namespace {

const char* const c_Format  = "crude_blueprint";
const int         c_Version = 2;

crude_json::value SaveValue(const Value& value)
{
    crude_json::object saved;
    saved["type"] = value.GetType()->m_Name;
    saved["data"] = value.GetType()->m_Head->Save(value);
    return saved;
}

crude_json::object SaveConfig(const NodeConfig& config)
{
    crude_json::object saved;
    if (!config.m_Groups.empty())
    {
        crude_json::object groups;
        for (auto& [name, count] : config.m_Groups)
            groups[name] = count;
        saved["groups"] = std::move(groups);
    }
    if (!config.m_Types.empty())
    {
        crude_json::object types;
        for (auto& [name, type] : config.m_Types)
            types[name] = type->m_Name;
        saved["types"] = std::move(types);
    }
    if (!config.m_Split.empty())
    {
        crude_json::array split;
        for (auto& key : config.m_Split)
            split.push_back(key);
        saved["split"] = std::move(split);
    }
    if (!config.m_Data.empty())
    {
        crude_json::object data;
        for (auto& [key, value] : config.m_Data)
            data[key] = SaveValue(value);
        saved["data"] = std::move(data);
    }
    return saved;
}

} } // namespace crude_blueprint { namespace {

// Messages name where a problem is: "graphs[0].nodes[2].pins.in.1".
struct crude_blueprint::FileLoader
{
    Blueprint&             m_Blueprint;
    TypeTable&             m_Types;
    const Registry&        m_Registry;
    std::unordered_set<Id> m_Ids;
    Id                     m_MaxId = 0;

    static Error Bad(const std::string& where, const std::string& what)
    {
        return Error(BlueprintError::BadFile, where + " " + what);
    }

    Result<Id> ReadId(const crude_json::value& object, const char* key, const std::string& where, bool unique = true)
    {
        auto member = object.find(key);
        if (!member)
            return Bad(where, std::string("has no ") + key);
        auto number = member->is_number() ? member->get<crude_json::number>() : 0.0;
        if (!(number >= 1.0 && number <= 4294967295.0) || number != std::trunc(number))
            return Bad(where + "." + key, "isn't an ID");
        auto id = static_cast<Id>(number);
        if (!unique)
            return id;
        if (!m_Ids.insert(id).second)
            return Bad(where + "." + key, "is " + std::to_string(id) + ", which another part has too");
        m_MaxId = std::max(m_MaxId, id);
        return id;
    }

    static Result<std::string> ReadString(const crude_json::value& object, const char* key, const std::string& where)
    {
        auto member = object.find(key);
        if (!member || !member->is_string())
            return Bad(where, std::string("has no ") + key + " text");
        return member->get<crude_json::string>();
    }

    static Result<int> ReadCount(const crude_json::value& json, const std::string& where)
    {
        auto number = json.is_number() ? json.get<crude_json::number>() : -1.0;
        if (!(number >= 0.0 && number <= 2147483647.0) || number != std::trunc(number))
            return Error(BlueprintError::BadFile, where + " isn't a count");
        return static_cast<int>(number);
    }

    Result<Value> ReadValue(const crude_json::value& json, const std::string& where)
    {
        auto typeText = ReadString(json, "type", where);
        if (!typeText)
            return typeText.GetError();
        auto type = m_Types.Parse(m_Registry, typeText.GetValue());
        if (!type)
            return Error(type.GetError().m_Category, type.GetError().m_Code, where + ": " + std::string(type.GetError().Message()));
        auto data = json.find("data");
        if (!data)
            return Bad(where, "has no data");
        auto value = type.GetValue()->m_Head->Load(*type.GetValue(), *data);
        if (!value)
            return Error(value.GetError().m_Category, value.GetError().m_Code, where + ".data: " + std::string(value.GetError().Message()));
        return value;
    }

    // What the node type reads; a problem here makes the node a placeholder.
    Result<void> ReadConfig(const crude_json::value& json, const std::string& where, NodeConfig& config)
    {
        if (auto groups = json.find("groups"))
        {
            if (!groups->is_object())
                return Bad(where + ".groups", "isn't an object");
            for (auto& [name, count] : groups->get<crude_json::object>())
            {
                auto read = ReadCount(count, where + ".groups." + name);
                if (!read)
                    return read.GetError();
                config.m_Groups[name] = read.GetValue();
            }
        }
        if (auto types = json.find("types"))
        {
            if (!types->is_object())
                return Bad(where + ".types", "isn't an object");
            for (auto& [name, text] : types->get<crude_json::object>())
            {
                auto type = text.is_string() ? m_Types.Parse(m_Registry, text.get<crude_json::string>()) : Result<const Type*>(Bad(where + ".types." + name, "isn't type text"));
                if (!type)
                    return Error(type.GetError().m_Category, type.GetError().m_Code, where + ".types." + name + ": " + std::string(type.GetError().Message()));
                config.m_Types[name] = type.GetValue();
            }
        }
        if (auto split = json.find("split"))
        {
            if (!split->is_array())
                return Bad(where + ".split", "isn't an array");
            for (auto& key : split->get<crude_json::array>())
            {
                if (!key.is_string())
                    return Bad(where + ".split", "holds what isn't a key");
                config.m_Split.push_back(key.get<crude_json::string>());
            }
        }
        if (auto data = json.find("data"))
        {
            if (!data->is_object())
                return Bad(where + ".data", "isn't an object");
            for (auto& [key, saved] : data->get<crude_json::object>())
            {
                auto value = ReadValue(saved, where + ".data." + key);
                if (!value)
                    return value.GetError();
                config.m_Data[key] = std::move(value).GetValue();
            }
        }
        return {};
    }

    // Its pins' IDs are read first, as a placeholder keeps them too.
    Result<void> ReadNode(Graph& graph, const crude_json::value& json, const std::string& where)
    {
        if (!json.is_object())
            return Bad(where, "isn't an object");
        auto& node = graph.m_Nodes.emplace_back();
        auto  id   = ReadId(json, "id", where);
        auto  name = ReadString(json, "type", where);
        if (!id || !name)
            return !id ? id.GetError() : name.GetError();
        node.m_Id = id.GetValue();

        auto pins = json.find("pins");
        if (pins && !pins->is_object())
            return Bad(where + ".pins", "isn't an object");
        if (pins)
        {
            for (auto& [key, pin] : pins->get<crude_json::object>())
            {
                if (!pin.is_object())
                    return Bad(where + ".pins." + key, "isn't an object");
                auto pinId = ReadId(pin, "id", where + ".pins." + key);
                if (!pinId)
                    return pinId.GetError();
                node.m_KeyIds[key] = pinId.GetValue();
            }
        }
        auto version = json.find("type_version");
        if (version && !version->is_number())
            return Bad(where + ".type_version", "isn't a number");

        auto understood = Understand(node, json, where, version ? static_cast<uint32_t>(std::clamp(version->get<crude_json::number>(), 0.0, 4294967295.0)) : 1u, name.GetValue());
        if (!understood)
        {
            auto& placeholder = m_Blueprint.m_Placeholders.emplace_back(std::make_unique<PlaceholderNodeType>(name.GetValue(), json, understood.GetError()));
            node.m_Type   = placeholder.get();
            node.m_Config = {};
            node.m_Values.clear();
        }
        return {};
    }

    Result<void> Understand(Node& node, const crude_json::value& saved, const std::string& where, uint32_t version, const std::string& name)
    {
        auto type = m_Registry.FindNodeType(name);
        if (!type)
            return Error(BlueprintError::UnknownType, "no node type is named " + name);
        if (version > type->m_Version)
            return Error(BlueprintError::Unsupported, name + " is at version " + std::to_string(type->m_Version) + ", and the file holds version " + std::to_string(version) + " of its node");

        auto json = saved;
        if (version < type->m_Version)
        {
            if (auto upgraded = type->Upgrade(json, version); !upgraded)
                return upgraded;

            // An upgrade may rename keys, but the pins keep their IDs.
            std::map<std::string, Id, std::less<>> keyIds;
            if (auto pins = json.find("pins"); pins && pins->is_object())
            {
                for (auto& [key, pin] : pins->get<crude_json::object>())
                {
                    auto id = pin.is_object() ? ReadId(pin, "id", where + ".pins." + key, false) : Result<Id>(Bad(where + ".pins." + key, "isn't an object"));
                    if (!id)
                        return id.GetError();
                    auto had = std::any_of(node.m_KeyIds.begin(), node.m_KeyIds.end(), [&id](const auto& entry) { return entry.second == id.GetValue(); });
                    if (!had)
                        return Error(BlueprintError::Unsupported, name + "'s upgrade gave pin " + key + " an ID the node didn't have");
                    keyIds[key] = id.GetValue();
                }
            }
            node.m_KeyIds = std::move(keyIds);
        }

        if (auto config = json.find("config"))
        {
            if (!config->is_object())
                return Bad(where + ".config", "isn't an object");
            if (auto read = ReadConfig(*config, where + ".config", node.m_Config); !read)
                return read;
        }
        if (auto pins = json.find("pins"); pins && pins->is_object())
        {
            for (auto& [key, pin] : pins->get<crude_json::object>())
            {
                auto value = pin.find("value");
                if (!value)
                    continue;
                auto id = node.m_KeyIds.find(key);
                if (id == node.m_KeyIds.end())
                    return Bad(where + ".pins." + key, "isn't a pin the node had");
                auto read = ReadValue(*value, where + ".pins." + key + ".value");
                if (!read)
                    return read.GetError();
                node.m_Values[id->second] = std::move(read).GetValue();
            }
        }
        node.m_Type = type;
        return {};
    }

    Result<void> ReadLink(Graph& graph, const std::unordered_set<Id>& pins, const crude_json::value& json, const std::string& where)
    {
        if (!json.is_object())
            return Bad(where, "isn't an object");
        auto& link = graph.m_Links.emplace_back();
        auto  id   = ReadId(json, "id", where);
        auto  from = ReadId(json, "from", where, false);
        auto  to   = ReadId(json, "to", where, false);
        if (!id || !from || !to)
            return !id ? id.GetError() : !from ? from.GetError() : to.GetError();
        for (auto end : { from.GetValue(), to.GetValue() })
            if (!pins.count(end))
                return Bad(where, "joins pin " + std::to_string(end) + ", which no node of its graph has");
        link.m_Id   = id.GetValue();
        link.m_From = from.GetValue();
        link.m_To   = to.GetValue();
        if (auto forced = json.find("forced"))
        {
            auto order = ReadCount(*forced, where + ".forced");
            if (!order)
                return order.GetError();
            link.m_Forced      = order.GetValue();
            graph.m_LastForced = std::max(graph.m_LastForced, link.m_Forced);
        }
        if (auto cast = json.find("cast"))
        {
            if (!cast->is_string())
                return Bad(where + ".cast", "isn't the name of a cast");
            link.m_Cast = cast->get<crude_json::string>();
        }
        return {};
    }

    Result<void> ReadGraph(const crude_json::value& json, const std::string& where)
    {
        if (!json.is_object())
            return Bad(where, "isn't an object");
        auto id   = ReadId(json, "id", where);
        auto name = ReadString(json, "name", where);
        auto kind = ReadString(json, "kind", where);
        if (!id || !name || !kind)
            return !id ? id.GetError() : !name ? name.GetError() : kind.GetError();
        if (kind.GetValue() != "event")
            return Bad(where + ".kind", "is " + kind.GetValue() + ", which this runtime doesn't know");
        auto& graph = *m_Blueprint.m_Graphs.emplace_back(std::make_unique<Graph>(m_Blueprint, id.GetValue(), name.GetValue()));

        auto nodes = json.find("nodes");
        auto links = json.find("links");
        if (!nodes || !nodes->is_array() || !links || !links->is_array())
            return Bad(where, std::string("has no ") + (!nodes || !nodes->is_array() ? "array of nodes" : "array of links"));
        auto& nodeList = nodes->get<crude_json::array>();
        for (size_t i = 0; i < nodeList.size(); ++i)
            if (auto read = ReadNode(graph, nodeList[i], where + ".nodes[" + std::to_string(i) + "]"); !read)
                return read;

        std::unordered_set<Id> pins;
        for (auto& node : graph.m_Nodes)
            for (auto& [key, pin] : node.m_KeyIds)
                pins.insert(pin);
        auto& linkList = links->get<crude_json::array>();
        for (size_t i = 0; i < linkList.size(); ++i)
            if (auto read = ReadLink(graph, pins, linkList[i], where + ".links[" + std::to_string(i) + "]"); !read)
                return read;
        return {};
    }

    Result<void> ReadFile(const crude_json::value& file)
    {
        if (!file.is_object())
            return Error(BlueprintError::BadFile, "a blueprint is an object");
        auto format  = file.find("format");
        auto version = file.find("version");
        if (!format || !format->is_string() || format->get<crude_json::string>() != c_Format)
            return Error(BlueprintError::BadFile, std::string("it isn't a ") + c_Format + " file");
        if (!version || !version->is_number() || version->get<crude_json::number>() != c_Version)
            return Error(BlueprintError::BadFile, "its version is " + (version ? version->dump() : std::string("missing")) + ", and this runtime reads version " + std::to_string(c_Version));
        auto nextId = ReadId(file, "next_id", "the file", false);
        if (!nextId)
            return nextId.GetError();
        auto graphs = file.find("graphs");
        if (!graphs || !graphs->is_array())
            return Error(BlueprintError::BadFile, "the file has no array of graphs");
        auto& graphList = graphs->get<crude_json::array>();
        for (size_t i = 0; i < graphList.size(); ++i)
            if (auto read = ReadGraph(graphList[i], "graphs[" + std::to_string(i) + "]"); !read)
                return read;

        // A file can say less than its IDs need.
        m_Blueprint.m_NextId = std::max(nextId.GetValue(), m_MaxId + 1);
        for (auto& graph : m_Blueprint.m_Graphs)
            graph->Update();
        return {};
    }
};

crude_blueprint::Result<void> crude_blueprint::NodeType::Upgrade(crude_json::value&, uint32_t version) const
{
    return Error(BlueprintError::Unsupported, m_Name + " can't read version " + std::to_string(version) + " of its nodes");
}

crude_json::value crude_blueprint::Save(const Blueprint& blueprint)
{
    crude_json::array graphs;
    for (auto& graph : blueprint.m_Graphs)
    {
        crude_json::array nodes;
        for (auto& node : graph->Nodes())
        {
            if (auto placeholder = Cast<PlaceholderNodeType>(node.m_Type))
            {
                nodes.push_back(placeholder->m_Saved);
                continue;
            }
            crude_json::object saved;
            saved["id"]           = node.m_Id;
            saved["type"]         = node.m_Type->m_Name;
            saved["type_version"] = node.m_Type->m_Version;
            if (auto config = SaveConfig(node.m_Config); !config.empty())
                saved["config"] = std::move(config);
            crude_json::object pins;
            for (auto& [key, id] : node.m_KeyIds)
            {
                crude_json::object pin;
                pin["id"] = id;
                if (auto declared = node.FindPin(key))
                {
                    pin["dir"] = declared->m_Direction == Direction::Input ? "in" : "out";
                    if (declared->m_Flow)
                        pin["flow"] = true;
                }
                if (auto value = node.m_Values.find(id); value != node.m_Values.end())
                    pin["value"] = SaveValue(value->second);
                pins[key] = std::move(pin);
            }
            saved["pins"] = std::move(pins);
            nodes.push_back(std::move(saved));
        }

        crude_json::array links;
        for (auto& link : graph->Links())
        {
            crude_json::object saved;
            saved["id"]   = link.m_Id;
            saved["from"] = link.m_From;
            saved["to"]   = link.m_To;
            if (link.m_Forced > 0)
                saved["forced"] = link.m_Forced;
            if (!link.m_Cast.empty())
                saved["cast"] = link.m_Cast;
            links.push_back(std::move(saved));
        }

        crude_json::object saved;
        saved["id"]    = graph->GetId();
        saved["kind"]  = "event";
        saved["name"]  = graph->GetName();
        saved["nodes"] = std::move(nodes);
        saved["links"] = std::move(links);
        graphs.push_back(std::move(saved));
    }

    crude_json::object file;
    file["format"]  = c_Format;
    file["version"] = c_Version;
    file["next_id"] = blueprint.m_NextId;
    file["graphs"]  = std::move(graphs);
    return file;
}

crude_blueprint::Result<void> crude_blueprint::Load(Blueprint& blueprint, const crude_json::value& file)
{
    if (!blueprint.m_Graphs.empty())
        return Error(BlueprintError::Unsupported, "a blueprint loads only while it's empty");
    FileLoader loader{ blueprint, blueprint.GetTypes(), blueprint.GetRegistry(), {}, 0 };
    auto read = loader.ReadFile(file);
    if (!read)
    {
        blueprint.m_Graphs.clear();
        blueprint.m_Placeholders.clear();
        blueprint.m_NextId = 1;
    }
    return read;
}
