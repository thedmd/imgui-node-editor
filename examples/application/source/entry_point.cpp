# include "application.h"
# include "platform.h"

# if PLATFORM(WINDOWS)
#     define NOMINMAX
#     define WIN32_LEAN_AND_MEAN
#     include <windows.h>
#     include <stdlib.h> // __argc, argv
# endif

# if defined(_WIN32) && !defined(_CONSOLE)
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
{
    return Main(__argc, __argv);
}
# else
int main(int argc, char** argv)
{
    return Main(argc, argv);
}
# endif
