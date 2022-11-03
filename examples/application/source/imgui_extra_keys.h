# pragma once
# include <imgui.h>

# if !defined(IMGUI_VERSION_NUM) || (IMGUI_VERSION_NUM < 18822)

# include <type_traits>

// https://stackoverflow.com/a/8597498
# define DECLARE_HAS_NESTED(Name, Member)                                          \
                                                                                   \
    template<class T>                                                              \
    struct has_nested_ ## Name                                                     \
    {                                                                              \
        typedef char yes;                                                          \
        typedef yes(&no)[2];                                                       \
                                                                                   \
        template<class U> static yes test(decltype(U::Member)*);                   \
        template<class U> static no  test(...);                                    \
                                                                                   \
        static bool const value = sizeof(test<T>(0)) == sizeof(yes);               \
    };

# define DECLARE_KEY_TESTER(Key)                                                                    \
    DECLARE_HAS_NESTED(Key, Key)                                                                    \
    struct KeyTester_ ## Key                                                                        \
    {                                                                                               \
        template <typename T>                                                                       \
        static int Get(typename std::enable_if<has_nested_ ## Key<ImGuiKey_>::value, T>::type*)     \
        {                                                                                           \
            return T::Key;                                                                          \
        }                                                                                           \
                                                                                                    \
        template <typename T>                                                                       \
        static int Get(typename std::enable_if<!has_nested_ ## Key<ImGuiKey_>::value, T>::type*)    \
        {                                                                                           \
            return -1;                                                                              \
        }                                                                                           \
    }

DECLARE_KEY_TESTER(ImGuiKey_F);
DECLARE_KEY_TESTER(ImGuiKey_D);

static inline int GetEnumValueForF()
{
    return KeyTester_ImGuiKey_F::Get<ImGuiKey_>(nullptr);
}

static inline int GetEnumValueForD()
{
    return KeyTester_ImGuiKey_D::Get<ImGuiKey_>(nullptr);
}

# else

static inline ImGuiKey GetEnumValueForF()
{
    return ImGuiKey_F;
}

static inline ImGuiKey GetEnumValueForD()
{
    return ImGuiKey_D;
}

# endif