# pragma once

# include <utility>

# define AX_CONCATENATE_IMPL(s1, s2) s1##s2
# define AX_CONCATENATE(s1, s2)    AX_CONCATENATE_IMPL(s1, s2)
# ifdef __COUNTER__
#     define AX_ANONYMOUS_VARIABLE(str) AX_CONCATENATE(str, __COUNTER__)
# else
#     define AX_ANONYMOUS_VARIABLE(str) AX_CONCATENATE(str, __LINE__)
# endif

namespace ax {
namespace scopeguard_detail {

enum class ScopeGuardOnExit {};
template <typename F>
class ScopeGuard
{
    F f_;
    bool active_;
public:
    ScopeGuard() = delete;
    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;
    ScopeGuard(ScopeGuard&& rhs): f_(std::move(rhs.f_)), active_(rhs.active_) { rhs.dismiss(); }
    ScopeGuard(F f): f_(std::move(f)), active_(true) {}
    ~ScopeGuard() { if (active_) f_(); }
    void dismiss() { active_ = false; }
};
template <typename F>
inline ScopeGuard<F> operator+(ScopeGuardOnExit, F&& f)
{
    return ScopeGuard<F>(std::forward<F>(f));
}

} // namespace scopeguard_detail
} // namespace ax

# define AX_SCOPE_EXIT \
    auto AX_ANONYMOUS_VARIABLE(AX_SCOPE_EXIT_STATE) \
    = ::ax::scopeguard_detail::ScopeGuardOnExit() + [&]()
