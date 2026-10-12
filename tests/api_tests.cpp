//------------------------------------------------------------------------------
// Public API misuse.
//------------------------------------------------------------------------------
# include "harness.h"

TEST_CASE(DestroyCurrentEditor, "api.destroying_current_editor_clears_it", "API-8", test::TestFlags_None)
{
    test::Host host;
    test::Storage storage;
    const auto config = test::MakeConfig(&storage);
    auto first  = ed::CreateEditor(&config);
    auto second = ed::CreateEditor(&config);

    ed::SetCurrentEditor(first);
    ed::DestroyEditor(second);
    test::Check(ed::GetCurrentEditor() == first, "destroying another editor keeps the current one");

    ed::DestroyEditor(first);
    test::Check(ed::GetCurrentEditor() == nullptr, "destroying the current editor clears it");
    test::Check(ed::GetConfig().UserPointer == nullptr, "GetConfig() without a current editor returns an empty config");
}

TEST_CASE(CallWithoutEditor, "api.call_without_current_editor_asserts", "API-8", test::TestFlags_None)
{
    test::ExpectAsserts(1);
    test::Host host;
    ed::SetCurrentEditor(nullptr);

    bool asserted = false;
    try
    {
        test::ThrowOnAssert throwOnAssert;
        ed::GetStyle();
    }
    catch (const test::AssertFailure&)
    {
        asserted = true;
    }
    test::Check(asserted, "ed::GetStyle() without a current editor asserts");
}
