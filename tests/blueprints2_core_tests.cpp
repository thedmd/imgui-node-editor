//------------------------------------------------------------------------------
// blueprints2-example's runtime: RTTI, boxes and errors.
//------------------------------------------------------------------------------
# include "harness.h"
# include "crude_blueprint.h"
# include <string>

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
