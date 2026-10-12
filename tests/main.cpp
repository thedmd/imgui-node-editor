//------------------------------------------------------------------------------
// Test runner.
//
// Usage:
//   node_editor_tests                 runs every test that can run in this build
//   node_editor_tests <name>...       runs the named tests
//   node_editor_tests --list          lists the tests with their findings and flags
//
// Exit code: 0 when every test passed, 77 when the only test run was skipped,
// 1 otherwise.
//------------------------------------------------------------------------------
# include "harness.h"
# include <cstdio>
# include <cstring>

# if defined(_WIN32)
#     include <crtdbg.h>
#     include <cstdlib>
#     define WIN32_LEAN_AND_MEAN
#     define NOMINMAX
#     include <windows.h>
# endif

static const int c_SkipExitCode = 77;

# if defined(IMGUI_NODE_EDITOR_TESTS_SANITIZE)
static const bool c_Sanitized = true;
# else
static const bool c_Sanitized = false;
# endif

static void DisableErrorDialogs()
{
# if defined(_WIN32)
    // Crashes and CRT assertions must end the test instead of waiting for a user.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
# endif
}

// Returns 0 on pass, 1 on failure, c_SkipExitCode when skipped.
static int Run(const test::TestCase& testCase)
{
    printf("[%s]%s%s\n", testCase.Name, testCase.Finding ? " " : "", testCase.Finding ? testCase.Finding : "");

    test::ResetResults();
    if ((testCase.Flags & test::TestFlags_NeedsSanitizer) && !c_Sanitized)
        test::Skip("needs a build with IMGUI_NODE_EDITOR_TESTS_SANITIZE");
    else
        testCase.Function();

    if (test::IsSkipped())
        return c_SkipExitCode;

    bool passed = test::CheckFailures() == 0;
    if (test::AssertCount() != test::ExpectedAsserts())
    {
        printf("    FAIL %d IM_ASSERT failures, expected %d\n", test::AssertCount(), test::ExpectedAsserts());
        passed = false;
    }
    printf("    %s\n", passed ? "passed" : "FAILED");
    return passed ? 0 : 1;
}

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    DisableErrorDialogs();
    printf("Dear ImGui %s (%d)%s\n", IMGUI_VERSION, IMGUI_VERSION_NUM, c_Sanitized ? ", sanitized" : "");

    const auto& registry = test::Registry();

    if (argc > 1 && strcmp(argv[1], "--list") == 0)
    {
        for (auto& testCase : registry)
            printf("%-60s %-10s%s%s\n", testCase.Name, testCase.Finding ? testCase.Finding : "-",
                (testCase.Flags & test::TestFlags_KnownFailure)   ? " known-failure" : "",
                (testCase.Flags & test::TestFlags_NeedsSanitizer) ? " needs-sanitizer" : "");
        return 0;
    }

    int run = 0, passed = 0, failed = 0, skipped = 0;
    for (auto& testCase : registry)
    {
        bool selected = argc < 2;
        for (int i = 1; i < argc; ++i)
            if (strcmp(argv[i], testCase.Name) == 0)
                selected = true;
        if (!selected)
            continue;

        ++run;
        switch (Run(testCase))
        {
            case 0:              ++passed;  break;
            case c_SkipExitCode: ++skipped; break;
            default:             ++failed;  break;
        }
    }

    if (run == 0)
    {
        printf("no test matched\n");
        return 1;
    }

    printf("%d passed, %d failed, %d skipped\n", passed, failed, skipped);
    if (failed > 0)
        return 1;
    return (skipped == run) ? c_SkipExitCode : 0;
}
