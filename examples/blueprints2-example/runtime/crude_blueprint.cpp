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

    Result<Value> Load(const Type& type, const crude_json::value& json) const override
    {
        auto& keyType   = *type.m_Args[0];
        auto& valueType = *type.m_Args[1];
        if (!keyType.m_Head->IsKey())
            return Error(BlueprintError::WrongType, keyType.m_Name + " can't be a key");
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
