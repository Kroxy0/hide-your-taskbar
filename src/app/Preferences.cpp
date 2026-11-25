#include "Preferences.h"
#include <initializer_list>
#include <string>

namespace
{
constexpr wchar_t SettingsKey[] = L"Software\\HideYourTaskbar";
constexpr wchar_t RunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t RunValue[] = L"HideYourTaskbar";

AppearanceSettings settings;

void StoreSettings(bool save)
{
    struct Entry { const wchar_t* name; DWORD& value; };
    for (auto entry : {Entry{L"Normal", settings.normal}, Entry{L"Full", settings.full},
                       Entry{L"Separate", settings.separate}, Entry{L"Color", settings.color},
                       Entry{L"Opacity", settings.opacity}})
    {
        if (save)
            RegSetKeyValueW(HKEY_CURRENT_USER, SettingsKey, entry.name, REG_DWORD, &entry.value, sizeof(DWORD));
        else
        {
            DWORD value = 0;
            DWORD size = sizeof(value);
            if (RegGetValueW(HKEY_CURRENT_USER, SettingsKey, entry.name, RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS)
                entry.value = value;
        }
    }
    if (settings.normal > static_cast<DWORD>(TaskbarMode::Color))
        settings.normal = static_cast<DWORD>(TaskbarMode::Clear);
    if (settings.full > static_cast<DWORD>(TaskbarMode::Color))
        settings.full = static_cast<DWORD>(TaskbarMode::Default);
    if (settings.opacity > MaximumColorOpacity)
        settings.opacity = DefaultColorOpacity;
}
}

AppearanceSettings& GetAppearanceSettings()
{
    return settings;
}

void LoadAppearanceSettings()
{
    StoreSettings(false);
}

void SaveAppearanceSettings()
{
    StoreSettings(true);
}

bool StartupEnabled()
{
    DWORD size = 0;
    return RegGetValueW(HKEY_CURRENT_USER, RunKey, RunValue, RRF_RT_REG_SZ, nullptr, nullptr, &size) == ERROR_SUCCESS;
}

bool SetStartup(bool enable)
{
    if (!enable)
    {
        const LSTATUS result = RegDeleteKeyValueW(HKEY_CURRENT_USER, RunKey, RunValue);
        return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
    }

    wchar_t path[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
    if (!length || length == ARRAYSIZE(path))
        return false;

    const auto command = L"\"" + std::wstring(path, length) + L"\" --startup";
    return RegSetKeyValueW(HKEY_CURRENT_USER, RunKey, RunValue, REG_SZ, command.c_str(),
        static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}
