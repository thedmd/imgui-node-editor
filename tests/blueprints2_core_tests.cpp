//------------------------------------------------------------------------------
// blueprints2-example's runtime: RTTI, boxes, errors, types, values, casts and the registry.
//------------------------------------------------------------------------------
# include "harness.h"
# include "crude_blueprint.h"
# include <crude_json.h>
# include <array>
# include <cstdint>
# include <limits>
# include <memory>
# include <string>
# include <vector>

using namespace crude_blueprint;

namespace {

struct Animal
{
    CRUDE_BP_RTTI_ROOT(Animal)
    virtual ~Animal() = default;
};

struct Cat : Animal
{
    CRUDE_BP_RTTI(Cat, Animal)
};

struct Lion final : Cat
{
    CRUDE_BP_RTTI(Lion, Cat)
};

struct Dog final : Animal
{
    CRUDE_BP_RTTI(Dog, Animal)
};

// A box that counts its instances.
struct CountedBox final : Box
{
    CRUDE_BP_RTTI(CountedBox, Box)

    CountedBox() { ++s_Alive; }
    ~CountedBox() override { --s_Alive; }

    static int s_Alive;
};

int CountedBox::s_Alive = 0;

const Type& ArrayOf(TypeTable& table, const Type& item)
{
    const std::array<const Type*, 1> args = { &item };
    return *table.Get(ArrayValueType(), args);
}

const Type& MapOf(TypeTable& table, const Type& key, const Type& value)
{
    const std::array<const Type*, 2> args = { &key, &value };
    return *table.Get(MapValueType(), args);
}

// Through text, as a file holds it.
Result<Value> RoundTrip(const Value& value)
{
    auto text = crude_json::value(value.GetType()->m_Head->Save(value)).dump();
    auto json = crude_json::value::parse(text);
    return value.GetType()->m_Head->Load(*value.GetType(), json);
}

Result<Value> LoadText(const Type& type, const char* text)
{
    return type.m_Head->Load(type, crude_json::value::parse(text));
}

// As a cast link converts.
Result<Value> Convert(const Registry& registry, const Value& value, const Type& to)
{
    auto cast = registry.FindCast(value.GetType(), &to);
    if (!cast)
        return Error(BlueprintError::UnknownType, "no cast " + value.GetType()->m_Name + "->" + to.m_Name);
    return cast->m_Function(value);
}

std::string Format(const Value& value)
{
    return value.GetType()->m_Head->Format(value);
}

// Array<Array<...<Int32>...>>
std::string NestedArrays(int depth)
{
    std::string text;
    for (int i = 0; i < depth; ++i)
        text += "Array<";
    return text + "Int32" + std::string(depth, '>');
}

} // namespace

// An application's own error codes, in a category of its own.
namespace application {

enum class GameError : uint16_t { NoFuel = 1 };

constexpr ErrorCategory CategoryOf(GameError) { return ErrorCategory{ 1000 }; }

} // namespace application

TEST_CASE(RttiIdentifiesClasses, "runtime.rtti_identifies_classes", nullptr, test::TestFlags_None)
{
    Lion lion;
    Dog  dog;
    const Animal& constLion = lion;
    Animal* noAnimal = nullptr;

    test::Check(IsA<Lion>(&lion) && IsA<Cat>(&lion) && IsA<Animal>(&lion), "a Lion is a Lion, a Cat and an Animal");
    test::Check(!IsA<Cat>(&dog) && !IsA<Lion>(&dog), "a Dog is neither a Cat nor a Lion");
    test::Check(Cast<Cat>(static_cast<Animal*>(&lion)) == &lion && Cast<Cat>(static_cast<Animal*>(&dog)) == nullptr, "Cast() gives the object when it is one, null when not");
    test::Check(Cast<Lion>(&constLion) == &lion, "Cast() of a const object gives a const pointer");
    test::Check(Cast<Cat>(noAnimal) == nullptr && !IsA<Animal>(noAnimal), "and null for null");
    test::Check(lion.GetTypeInfo().m_Name == "Lion" && Cat::StaticTypeInfo().m_Base == &Animal::StaticTypeInfo(), "type infos name the class and its base");
}

TEST_CASE(RefsShareBoxes, "runtime.refs_share_boxes_and_free_the_last", nullptr, test::TestFlags_None)
{
    const auto alive = CountedBox::s_Alive;
    size_t shared = 0, afterMove = 0, afterSelf = 0;
    bool moved = false;
    {
        auto first = MakeRef<CountedBox>();
        {
            auto second = first;
            Ref<const Box> base = second;
            shared = first.References();
            auto third = std::move(second);
            moved = !second && third;
            afterMove = first.References();
        }
        auto& same = first;
        first = same;
        afterSelf = first.References();
    }

    test::Check(shared == 3, "two copies, one of them as a const Box, share the box: %d references", int(shared));
    test::Check(moved && afterMove == 3, "moving a reference keeps the count: %d", int(afterMove));
    test::Check(afterSelf == 1, "assigning a reference to itself changes nothing: %d", int(afterSelf));
    test::Check(CountedBox::s_Alive == alive, "the last reference frees the box: %d alive", CountedBox::s_Alive - alive);
}

TEST_CASE(ResultsHoldValuesOrErrors, "runtime.results_hold_a_value_or_an_error", nullptr, test::TestFlags_None)
{
    auto parse = [](int value) -> Result<int32_t>
    {
        if (value < 0)
            return Error(BlueprintError::OutOfRange, "below zero");
        if (value > 100)
            return BlueprintError::OutOfRange;
        return value;
    };
    auto check = [](bool ok) -> Result<void>
    {
        if (!ok)
            return application::GameError::NoFuel;
        return {};
    };

    const auto ok       = parse(7);
    const auto negative = parse(-1);
    const auto large    = parse(1000);
    const auto passed   = check(true);
    const auto failed   = check(false);

    test::Check(ok && ok.GetValue() == 7 && ok.ValueOr(0) == 7, "a value: %d", ok ? ok.GetValue() : -1);
    test::Check(!negative && negative.GetError().Is(BlueprintError::OutOfRange) && negative.GetError().Message() == "below zero", "an error with a message");
    test::Check(!large && large.ValueOr(-1) == -1 && large.GetError().Message().empty() && !large.GetError().m_Message, "an error without a message holds no text");
    test::Check(negative.GetError() == large.GetError(), "errors are equal by category and code, whatever their messages");
    test::Check(passed && !failed, "Result<void> is true without an error");
    test::Check(failed.GetError().m_Category == ErrorCategory{ 1000 } && failed.GetError().Is(application::GameError::NoFuel), "an application's code has its category, found through CategoryOf()");
    test::Check(!failed.GetError().Is(BlueprintError::Threw), "and isn't the runtime's code of the same number");

    if (sizeof(void*) == 8)
    {
        test::Check(sizeof(Error) == 16, "an Error is 16 bytes: %d", int(sizeof(Error)));
        test::Check(sizeof(Result<void>) == 24, "a Result<void> is 24: %d", int(sizeof(Result<void>)));
    }
}

TEST_CASE(TypesAreInterned, "runtime.types_are_interned", nullptr, test::TestFlags_None)
{
    TypeTable table;
    auto& arrayOfInt  = ArrayOf(table, Int32Type());
    auto& again       = ArrayOf(table, Int32Type());
    auto& arrayOfText = ArrayOf(table, StringType());
    auto& map         = MapOf(table, StringType(), arrayOfInt);
    StructType vector2("example.Vector2", { { "x", &FloatType() }, { "y", &FloatType() } });

    test::Check(&arrayOfInt == &again && &arrayOfInt != &arrayOfText, "equal types are one object");
    test::Check(arrayOfInt.m_Name == "Array<Int32>" && map.m_Name == "Map<String,Array<Int32>>", "named as files write them: %s", map.m_Name.c_str());
    test::Check(table.Get(Int32ValueType()) == &Int32Type() && table.Get(vector2) == &vector2.AsType(), "a value type that isn't a type constructor is its own type");
    test::Check(vector2.AsType().m_Name == "example.Vector2" && vector2.FindField("y") == 1 && vector2.FindField("z") == -1, "a struct is a type of its own, with its fields");
}

TEST_CASE(ScalarValues, "runtime.scalar_values", nullptr, test::TestFlags_None)
{
    auto yes  = Value::Bool(true);
    auto n    = Value::Int32(-7);
    auto f    = Value::Float(0.1f);
    auto text = Value::String("Za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87");
    auto copy = text;

    test::Check(yes.GetType() == &BoolType() && yes.AsBool(), "Bool");
    test::Check(n.GetType() == &Int32Type() && n.AsInt32() == -7, "Int32");
    test::Check(f.GetType() == &FloatType() && f.AsFloat() == 0.1f, "Float");
    test::Check(text.GetType() == &StringType() && copy.AsString().data() == text.AsString().data(), "a copied String shares its text");
    test::Check(StringType().m_Head->Format(text) == text.AsString(), "a String formats as itself");
    auto formatted = FloatType().m_Head->Format(f) + " " + Int32Type().m_Head->Format(n) + " " + BoolType().m_Head->Format(yes);
    test::Check(formatted == "0.1 -7 true", "Float, Int32 and Bool format as written: %s", formatted.c_str());
    test::Check(n == Value::Int32(-7) && !(n == Value::Int32(7)) && !(n == Value::Float(-7.0f)), "values are equal by type and content");
    test::Check(copy == Value::String(std::string(text.AsString())), "Strings are equal by their text");
    test::Check(Int32Type().m_Head->Default(Int32Type()) == Value::Int32(0) && StringType().m_Head->Default(StringType()) == Value::String({}), "each type has a default");
    test::Check(!Value().IsValid() && Value() == Value(), "a placeholder has no type");
    if (sizeof(void*) == 8)
        test::Check(sizeof(Value) == 24, "a Value is 24 bytes: %d", int(sizeof(Value)));
}

TEST_CASE(ContainerValues, "runtime.containers_and_structs", nullptr, test::TestFlags_None)
{
    TypeTable table;
    auto& numbers = ArrayOf(table, Int32Type());
    auto& names   = ArrayOf(table, StringType());
    auto& map     = MapOf(table, StringType(), Int32Type());
    StructType vector2("example.Vector2", { { "x", &FloatType() }, { "y", &FloatType() } });

    auto list   = Value::Array(numbers, { Value::Int32(1), Value::Int32(2), Value::Int32(3) });
    auto words  = Value::Array(names, { Value::String("a \"b\"") });
    auto scores = Value::Map(map, { { Value::String("b"), Value::Int32(2) }, { Value::String("a"), Value::Int32(1) }, { Value::String("b"), Value::Int32(3) } });
    auto point  = Value::Struct(vector2.AsType(), { Value::Float(1.5f), Value::Float(0.0f) });

    auto format = [](const Value& value) { return value.GetType()->m_Head->Format(value); };
    test::Check(format(list) == "[1, 2, 3]" && list.AsArray().size() == 3, "an Array: %s", format(list).c_str());
    test::Check(format(words) == "[\"a \\\"b\\\"\"]", "strings in an Array are quoted: %s", format(words).c_str());
    test::Check(format(scores) == "{\"a\": 1, \"b\": 3}", "a Map is sorted by key, and of equal keys the last stays: %s", format(scores).c_str());
    auto b = scores.Find(Value::String("b"));
    test::Check(b && *b == Value::Int32(3) && !scores.Find(Value::String("c")), "Find() gives a key's value, or null");
    test::Check(format(point) == "{x: 1.5, y: 0}", "a struct: %s", format(point).c_str());
    test::Check(list == Value::Array(numbers, { Value::Int32(1), Value::Int32(2), Value::Int32(3) }) && !(list == Value::Array(numbers, { Value::Int32(1) })), "containers are equal by content");
    test::Check(vector2.Default(vector2.AsType()) == Value::Struct(vector2.AsType(), { Value::Float(0.0f), Value::Float(0.0f) }), "a struct's default has its fields' defaults");
    test::Check(numbers.m_Head->Default(numbers).AsArray().empty() && map.m_Head->Default(map).AsMap().empty(), "containers start empty");
}

TEST_CASE(ValuesRoundTrip, "runtime.values_round_trip_through_files", nullptr, test::TestFlags_None)
{
    TypeTable table;
    StructType vector2("example.Vector2", { { "x", &FloatType() }, { "y", &FloatType() } });
    auto& grid = ArrayOf(table, ArrayOf(table, Int32Type()));
    auto& map  = MapOf(table, Int32Type(), vector2.AsType());

    const Value values[] =
    {
        Value::Bool(false),
        Value::Int32(INT32_MIN),
        Value::Int32(INT32_MAX),
        Value::Float(0.1f),
        Value::Float(-3.4e38f),
        Value::Float(1.17549435e-38f),
        Value::String("Za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87 \"quoted\" \\ back"),
        Value::Array(grid, { Value::Array(ArrayOf(table, Int32Type()), { Value::Int32(1), Value::Int32(-2) }), Value::Array(ArrayOf(table, Int32Type()), {}) }),
        Value::Map(map, { { Value::Int32(-1), Value::Struct(vector2.AsType(), { Value::Float(1.0f), Value::Float(2.0f) }) }, { Value::Int32(5), vector2.Default(vector2.AsType()) } }),
    };

    for (auto& value : values)
    {
        auto loaded = RoundTrip(value);
        auto text   = value.GetType()->m_Head->Format(value);
        test::Check(loaded && loaded.GetValue() == value, "%s round-trips: %s", value.GetType()->m_Name.c_str(), text.c_str());
    }
}

TEST_CASE(MalformedValues, "runtime.loading_refuses_malformed_values", nullptr, test::TestFlags_None)
{
    TypeTable table;
    StructType vector2("example.Vector2", { { "x", &FloatType() }, { "y", &FloatType() } });
    auto& numbers = ArrayOf(table, Int32Type());
    auto& map     = MapOf(table, StringType(), Int32Type());
    auto& badKeys = MapOf(table, BoolType(), Int32Type());

    struct Case
    {
        const Type*    type;
        const char*    text;
        BlueprintError code;
        const char*    message; // a part of it
    };
    const Case cases[] =
    {
        { &Int32Type(),      "1.5",                    BlueprintError::WrongType,  "whole"         },
        { &Int32Type(),      "3e9",                    BlueprintError::OutOfRange, "Int32"         },
        { &Int32Type(),      "\"7\"",                  BlueprintError::WrongType,  "Int32"         },
        { &FloatType(),      "1e39",                   BlueprintError::OutOfRange, "Float"         },
        { &FloatType(),      "null",                   BlueprintError::WrongType,  "Float"         },
        { &BoolType(),       "1",                      BlueprintError::WrongType,  "Bool"          },
        { &StringType(),     "[]",                     BlueprintError::WrongType,  "String"        },
        { &numbers,          "[1, \"x\"]",             BlueprintError::WrongType,  "item 1"        },
        { &numbers,          "{}",                     BlueprintError::WrongType,  "array"         },
        { &map,              "[[\"a\", 1], [\"a\", 2]]", BlueprintError::Duplicate, "twice"        },
        { &map,              "[[\"a\"]]",              BlueprintError::WrongType,  "entry 0"       },
        { &map,              "[[\"a\", true]]",        BlueprintError::WrongType,  "entry 0 value" },
        { &badKeys,          "[]",                     BlueprintError::WrongType,  "key"           },
        { &vector2.AsType(), "[1, 2]",                 BlueprintError::WrongType,  "object"        },
        { &vector2.AsType(), "{\"x\": \"one\"}",       BlueprintError::WrongType,  "x"             },
    };

    for (auto& c : cases)
    {
        auto loaded = LoadText(*c.type, c.text);
        const bool refused = !loaded && loaded.GetError().Is(c.code) && loaded.GetError().Message().find(c.message) != std::string_view::npos;
        test::Check(refused, "%s from %s is refused: %s", c.type->m_Name.c_str(), c.text, loaded ? "loaded" : std::string(loaded.GetError().Message()).c_str());
    }
}

TEST_CASE(StructFieldsChange, "runtime.structs_load_with_fields_missing_or_unknown", nullptr, test::TestFlags_None)
{
    // A file written before y existed and after z went.
    StructType vector2("example.Vector2", { { "x", &FloatType() }, { "y", &FloatType() } });
    auto loaded = LoadText(vector2.AsType(), "{\"x\": 2, \"z\": 5}");

    test::Check(loaded && loaded.GetValue() == Value::Struct(vector2.AsType(), { Value::Float(2.0f), Value::Float(0.0f) }), "x loads, y takes its default, z is ignored");
}

TEST_CASE(RegistryHoldsTypesAndCasts, "runtime.registry_holds_value_types_and_casts", nullptr, test::TestFlags_None)
{
    Registry registry;
    test::Check(!registry.FindValueType("Int32") && registry.CastsFrom(&Int32Type()).empty(), "a registry starts empty");

    auto builtins = RegisterBuiltins(registry);
    auto again    = RegisterBuiltins(registry);
    test::Check(builtins && registry.FindValueType("Int32") == &Int32ValueType() && registry.FindValueType("Map") == &MapValueType(), "the built-in value types are registered by name");
    test::Check(!again && again.GetError().Is(BlueprintError::Duplicate), "and only once: %s", again ? "registered" : std::string(again.GetError().Message()).c_str());

    auto owned   = std::make_unique<StructType>("example.Vector2", std::vector<StructField>{ { "x", &FloatType() }, { "y", &FloatType() } });
    auto vector2 = owned.get();
    auto added   = registry.AddValueType(std::move(owned));
    auto twice   = registry.AddValueType(std::make_unique<StructType>("Int32", std::vector<StructField>{}));
    auto spaced  = registry.AddValueType(std::make_unique<StructType>("example.Vector 2", std::vector<StructField>{}));
    auto unnamed = registry.AddValueType(std::make_unique<StructType>("", std::vector<StructField>{}));
    test::Check(added && registry.FindValueType("example.Vector2") == vector2, "an application's struct is registered, and the registry keeps it");
    test::Check(!twice && twice.GetError().Is(BlueprintError::Duplicate) && registry.FindValueType("Int32") == &Int32ValueType(), "a second value type of a name is refused");
    test::Check(!spaced && spaced.GetError().Is(BlueprintError::BadTypeText) && !unnamed && unnamed.GetError().Is(BlueprintError::BadTypeText), "and so is a name that type text can't hold");

    auto toString  = [](const Value& value) -> Result<Value> { return Value::String(Format(value)); };
    auto cast      = registry.AddCast({ &vector2->AsType(), &StringType(), false, toString });
    auto duplicate = registry.AddCast({ &Int32Type(), &FloatType(), false, toString });
    auto found     = registry.FindCast(&vector2->AsType(), &StringType());
    auto converted = Convert(registry, Value::Struct(vector2->AsType(), { Value::Float(1.0f), Value::Float(2.0f) }), StringType());
    test::Check(cast && found && found->Name() == "example.Vector2->String", "an application's cast is found by its types");
    test::Check(converted && converted.GetValue() == Value::String("{x: 1, y: 2}"), "and converts");
    test::Check(!duplicate && duplicate.GetError().Is(BlueprintError::Duplicate), "a second cast between two types is refused: %s", duplicate ? "registered" : std::string(duplicate.GetError().Message()).c_str());
    test::Check(!registry.FindCast(&StringType(), &BoolType()) && !registry.FindCast(&FloatType(), &FloatType()), "a cast nothing registered isn't found");

    std::string fromInt32;
    for (auto c : registry.CastsFrom(&Int32Type()))
        fromInt32 += (fromInt32.empty() ? "" : " ") + c->Name();
    test::Check(fromInt32 == "Int32->Float Int32->Bool Int32->String", "the casts from a type, in the order they were registered: %s", fromInt32.c_str());
}

TEST_CASE(TypeTextParses, "runtime.type_text_parses", nullptr, test::TestFlags_None)
{
    Registry registry;
    StructType vector2("example.Vector2", { { "x", &FloatType() }, { "y", &FloatType() } });
    test::Check(RegisterBuiltins(registry) && registry.AddValueType(vector2), "the value types are registered");

    // Parsing interns into the table; the registry stays as it is.
    const Registry& shared = registry;
    TypeTable table;
    auto parse = [&](std::string_view text) { return table.Parse(shared, text); };

    auto int32  = parse("Int32");
    auto point  = parse("example.Vector2");
    auto array  = parse("Array<Int32>");
    auto spaced = parse(" Map < String , Array<example.Vector2> > ");
    test::Check(int32.ValueOr(nullptr) == &Int32Type() && point.ValueOr(nullptr) == &vector2.AsType(), "a name gives its value type's own type");
    test::Check(array.ValueOr(nullptr) == &ArrayOf(table, Int32Type()), "a constructed type is the table's");
    test::Check(spaced && spaced.GetValue()->m_Name == "Map<String,Array<example.Vector2>>", "spaces don't count: %s", spaced ? spaced.GetValue()->m_Name.c_str() : std::string(spaced.GetError().Message()).c_str());
    test::Check(spaced && parse(spaced.GetValue()->m_Name).ValueOr(nullptr) == spaced.GetValue(), "a type's name parses back to the type");

    auto deep    = parse(NestedArrays(32));
    auto deeper  = parse(NestedArrays(33));
    auto deepest = parse(NestedArrays(100000));
    test::Check(deep.IsOk(), "types nest 32 deep: %s", deep ? "parsed" : std::string(deep.GetError().Message()).c_str());
    test::Check(!deeper && deeper.GetError().Is(BlueprintError::BadTypeText) && !deepest, "and no deeper, so text can't exhaust the stack");

    struct Case
    {
        const char*    text;
        BlueprintError code;
        const char*    message; // a part of it
    };
    const Case cases[] =
    {
        { "",                  BlueprintError::BadTypeText, "ends too soon"        },
        { "Array<Int32",       BlueprintError::BadTypeText, "ends too soon"        },
        { "Array<>",           BlueprintError::BadTypeText, "'>' at 6"             },
        { "Array<Int32>>",     BlueprintError::BadTypeText, "'>' at 12"            },
        { "Int32 Float",       BlueprintError::BadTypeText, "'F' at 6"             },
        { "Map<String;Int32>", BlueprintError::BadTypeText, "';' at 10"            },
        { "Int32<Float>",      BlueprintError::BadTypeText, "takes 0 types, not 1" },
        { "Array",             BlueprintError::BadTypeText, "takes 1 type, not 0"  },
        { "Map<String>",       BlueprintError::BadTypeText, "takes 2 types, not 1" },
        { "Vector2",           BlueprintError::UnknownType, "Vector2"              },
        { "Array<Foo>",        BlueprintError::UnknownType, "Foo"                  },
        { "Map<Float,Int32>",  BlueprintError::WrongType,   "Float can't be a key" },
    };

    for (auto& c : cases)
    {
        auto parsed = parse(c.text);
        const bool refused = !parsed && parsed.GetError().Is(c.code) && parsed.GetError().Message().find(c.message) != std::string_view::npos;
        test::Check(refused, "\"%s\" is refused: %s", c.text, parsed ? parsed.GetValue()->m_Name.c_str() : std::string(parsed.GetError().Message()).c_str());
    }
}

TEST_CASE(BuiltInCasts, "runtime.built_in_casts", nullptr, test::TestFlags_None)
{
    Registry registry;
    test::Check(RegisterBuiltins(registry).IsOk(), "the built-ins are registered");

    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto inf = std::numeric_limits<float>::infinity();

    struct Case
    {
        Value       from;
        const Type* to;
        Value       expected; // none when the cast fails
        const char* message;  // a part of the failure's
    };
    const Case cases[] =
    {
        { Value::Int32(7),              &FloatType(),  Value::Float(7.0f),          nullptr                },
        { Value::Int32(INT32_MAX),      &FloatType(),  Value::Float(2147483648.0f), nullptr                },
        { Value::Float(2.9f),           &Int32Type(),  Value::Int32(2),             nullptr                },
        { Value::Float(-2.9f),          &Int32Type(),  Value::Int32(-2),            nullptr                },
        { Value::Float(-2147483648.0f), &Int32Type(),  Value::Int32(INT32_MIN),     nullptr                },
        { Value::Float(2147483520.0f),  &Int32Type(),  Value::Int32(2147483520),    nullptr                },
        { Value::Float(2147483648.0f),  &Int32Type(),  Value(),                     "doesn't fit Int32"    },
        { Value::Float(nan),            &Int32Type(),  Value(),                     "doesn't fit Int32"    },
        { Value::Float(-inf),           &Int32Type(),  Value(),                     "doesn't fit Int32"    },
        { Value::Bool(true),            &Int32Type(),  Value::Int32(1),             nullptr                },
        { Value::Bool(false),           &Int32Type(),  Value::Int32(0),             nullptr                },
        { Value::Int32(-3),             &BoolType(),   Value::Bool(true),           nullptr                },
        { Value::Int32(0),              &BoolType(),   Value::Bool(false),          nullptr                },
        { Value::Bool(true),            &StringType(), Value::String("true"),       nullptr                },
        { Value::Int32(-7),             &StringType(), Value::String("-7"),         nullptr                },
        { Value::Float(0.1f),           &StringType(), Value::String("0.1"),        nullptr                },
        { Value::String("42"),          &Int32Type(),  Value::Int32(42),            nullptr                },
        { Value::String("-2147483648"), &Int32Type(),  Value::Int32(INT32_MIN),     nullptr                },
        { Value::String("2147483648"),  &Int32Type(),  Value(),                     "doesn't fit Int32"    },
        { Value::String(" 42"),         &Int32Type(),  Value(),                     "isn't a whole number" },
        { Value::String("+42"),         &Int32Type(),  Value(),                     "isn't a whole number" },
        { Value::String("4.2"),         &Int32Type(),  Value(),                     "isn't a whole number" },
        { Value::String(""),            &Int32Type(),  Value(),                     "isn't a whole number" },
        { Value::String("1.5"),         &FloatType(),  Value::Float(1.5f),          nullptr                },
        { Value::String("-25e-3"),      &FloatType(),  Value::Float(-0.025f),       nullptr                },
        { Value::String("1e39"),        &FloatType(),  Value(),                     "doesn't fit Float"    },
        { Value::String("1e400"),       &FloatType(),  Value(),                     "isn't a number"       },
        { Value::String("+1"),          &FloatType(),  Value(),                     "isn't a number"       },
        { Value::String("nan"),         &FloatType(),  Value(),                     "isn't a number"       },
        { Value::String("1.5 "),        &FloatType(),  Value(),                     "isn't a number"       },
        { Value::String("1,5"),         &FloatType(),  Value(),                     "isn't a number"       },
        { Value::String(""),            &FloatType(),  Value(),                     "isn't a number"       },
    };

    for (auto& c : cases)
    {
        auto converted = Convert(registry, c.from, *c.to);
        auto from      = c.from.GetType() == &StringType() ? "\"" + Format(c.from) + "\"" : Format(c.from);
        auto what      = c.from.GetType()->m_Name + " " + from + " to " + c.to->m_Name;
        auto outcome   = converted ? Format(converted.GetValue()) : std::string(converted.GetError().Message());
        if (c.message)
        {
            const bool failed = !converted && converted.GetError().Is(BlueprintError::CastFailed) && outcome.find(c.message) != std::string::npos;
            test::Check(failed, "%s fails: %s", what.c_str(), outcome.c_str());
        }
        else
            test::Check(converted && converted.GetValue() == c.expected, "%s: %s", what.c_str(), outcome.c_str());
    }

    auto mayFail = [&registry](const Type& from, const Type& to)
    {
        auto cast = registry.FindCast(&from, &to);
        return cast ? int(cast->m_MayFail) : -1;
    };
    test::Check(mayFail(Int32Type(), FloatType()) == 0 && mayFail(BoolType(), Int32Type()) == 0 && mayFail(Int32Type(), BoolType()) == 0 && mayFail(FloatType(), StringType()) == 0, "casts that can't fail say so");
    test::Check(mayFail(FloatType(), Int32Type()) == 1 && mayFail(StringType(), Int32Type()) == 1 && mayFail(StringType(), FloatType()) == 1, "and so do those that may");
}
