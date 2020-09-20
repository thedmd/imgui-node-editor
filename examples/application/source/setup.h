# pragma once
# include "config.h"

# define DETAIL_PRIV_EXPAND(x)      x
# define EXPAND(x)                  DETAIL_PRIV_EXPAND(x)
# define DETAIL_PRIV_CONCAT(x, y)   x ## y
# define CONCAT(x, y)               DETAIL_PRIV_CONCAT(x, y)


// Define PLATFORM(x) which evaluate to 0 or 1 when
// 'x' is: WINDOWS, MACOS or LINUX
# if defined(_WIN32)
#     define PLATFORM_PRIV_WINDOWS()     1
# elif defined(__APPLE__)
#     define PLATFORM_PRIV_MACOS()       1
# elif defined(__linux__)
#     define PLATFORM_PRIV_LINUX()       1
# else
#     error Unsupported platform
# endif

# ifndef PLATFORM_PRIV_WINDOWS
#     define PLATFORM_PRIV_WINDOWS()     0
# endif
# ifndef PLATFORM_PRIV_MACOS
#     define PLATFORM_PRIV_MACOS()       0
# endif
# ifndef PLATFORM_PRIV_LINUX
#     define PLATFORM_PRIV_LINUX()       0
# endif

# define PLATFORM(x) (PLATFORM_PRIV_##x())


// Define BACKEND(x) which evaluate to 0 or 1 when
// 'x' is: IMGUI_WIN32 or IMGUI_GLFW
//
// Use BACKEND_CONFIG to override desired backend
//
# if PLATFORM(WINDOWS)
#     define BACKEND_HAVE_IMGUI_WIN32()        1
# endif
# if HAVE_GLFW3
#     define BACKEND_HAVE_IMGUI_GLFW()         1
# endif

# ifndef BACKEND_HAVE_IMGUI_WIN32
#     define BACKEND_HAVE_IMGUI_WIN32()        0
# endif
# ifndef BACKEND_HAVE_IMGUI_GLFW
#     define BACKEND_HAVE_IMGUI_GLFW()         0
# endif

# define BACKEND_PRIV_IMGUI_WIN32()            1
# define BACKEND_PRIV_IMGUI_GLFW()             2

# if !defined(BACKEND_CONFIG)
#     if PLATFORM(WINDOWS)
#         define BACKEND_CONFIG     IMGUI_WIN32
#     else
#         define BACKEND_CONFIG     IMGUI_GLFW
#     endif
# endif

# define BACKEND(x)         ((BACKEND_PRIV_##x()) == CONCAT(BACKEND_PRIV_, EXPAND(BACKEND_CONFIG))() && (BACKEND_HAVE_##x()))


// Define RENDERER(x) which evaluate to 0 or 1 when
// 'x' is: IMGUI_DX11 or IMGUI_OGL3
//
// Use RENDERER_CONFIG to override desired renderer
//
# if PLATFORM(WINDOWS)
#     define RENDERER_HAVE_IMGUI_DX11()         1
# endif
# if HAVE_OPENGL
#     define RENDERER_HAVE_IMGUI_OGL3()         1
# endif

# ifndef RENDERER_HAVE_IMGUI_DX11
#     define RENDERER_HAVE_IMGUI_DX11()         0
# endif
# ifndef RENDERER_HAVE_IMGUI_OGL3
#     define RENDERER_HAVE_IMGUI_OGL3()         0
# endif

# define RENDERER_PRIV_IMGUI_DX11()             1
# define RENDERER_PRIV_IMGUI_OGL3()             2

# if !defined(RENDERER_CONFIG)
#     if PLATFORM(WINDOWS)
#         define RENDERER_CONFIG     IMGUI_DX11
#     else
#         define RENDERER_CONFIG     IMGUI_OGL3
#     endif
# endif

# define RENDERER(x)         ((RENDERER_PRIV_##x()) == CONCAT(RENDERER_PRIV_, EXPAND(RENDERER_CONFIG))() && (RENDERER_HAVE_##x()))
