#include "ControllerSession.h"
#include "Preferences.h"
#include <string_view>
#include <winrt/base.h>

#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

int wmain(int argc, wchar_t** argv)
{
    const std::wstring_view option = argc == 2 ? argv[1] : L"";
    if (option == L"--enable-startup")
        return SetStartup(true) ? 0 : 1;
    if (option == L"--quit")
    {
        const winrt::handle stop(OpenEventW(EVENT_MODIFY_STATE, FALSE, TaskbarStop));
        return stop && SetEvent(stop.get()) ? 0 : 1;
    }

    const winrt::handle controller(CreateMutexW(nullptr, TRUE, TaskbarMutex));
    if (!controller || GetLastError() == ERROR_ALREADY_EXISTS)
        return 1;

    int status = RunSession(option == L"--test", option == L"--startup");
    while (status == 2)
        status = RunSession(false, true);

    ReleaseMutex(controller.get());
    return status;
}
