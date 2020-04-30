#include "pch.h"

#include "VirtualDesktopUtils.h"

namespace VirtualDesktopUtils
{
    const CLSID CLSID_ImmersiveShell = { 0xC2F03A33, 0x21F5, 0x47FA, 0xB4, 0xBB, 0x15, 0x63, 0x62, 0xA2, 0xF2, 0x39 };
    const wchar_t GUID_EmptyGUID[] = L"{00000000-0000-0000-0000-000000000000}";

    const wchar_t RegNameCurrentVirtualDesktop[] = L"CurrentVirtualDesktop";
    const wchar_t RegNameVirtualDesktopIds[] = L"VirtualDesktopIDs";
    const wchar_t RegKeyVirtualDesktops[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VirtualDesktops";

    IServiceProvider* GetServiceProvider()
    {
        IServiceProvider* provider{ nullptr };
        if (FAILED(CoCreateInstance(CLSID_ImmersiveShell, nullptr, CLSCTX_LOCAL_SERVER, __uuidof(provider), (PVOID*)&provider)))
        {
            return nullptr;
        }
        return provider;
    }

    IVirtualDesktopManager* GetVirtualDesktopManager()
    {
        IVirtualDesktopManager* manager{ nullptr };
        IServiceProvider* serviceProvider = GetServiceProvider();
        if (serviceProvider == nullptr || FAILED(serviceProvider->QueryService(__uuidof(manager), &manager)))
        {
            return nullptr;
        }
        return manager;
    }

    bool GetWindowDesktopId(HWND topLevelWindow, GUID* desktopId)
    {
        static IVirtualDesktopManager* virtualDesktopManager = GetVirtualDesktopManager();
        return (virtualDesktopManager != nullptr) &&
               SUCCEEDED(virtualDesktopManager->GetWindowDesktopId(topLevelWindow, desktopId));
    }

    bool GetZoneWindowDesktopId(IZoneWindow* zoneWindow, GUID* desktopId)
    {
        // Format: <device-id>_<resolution>_<virtual-desktop-id>
        std::wstring uniqueId = zoneWindow->UniqueId();
        std::wstring virtualDesktopId = uniqueId.substr(uniqueId.rfind('_') + 1);
        if (virtualDesktopId == GUID_EmptyGUID)
        {
            return false;
        }
        return SUCCEEDED(CLSIDFromString(virtualDesktopId.c_str(), desktopId));
    }

    bool GetDesktopIdFromCurrentSession(GUID* desktopId)
    {
        DWORD sessionId;
        ProcessIdToSessionId(GetCurrentProcessId(), &sessionId);

        wchar_t sessionKeyPath[256]{};
        RETURN_IF_FAILED(
            StringCchPrintfW(
                sessionKeyPath,
                ARRAYSIZE(sessionKeyPath),
                L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\SessionInfo\\%d\\VirtualDesktops",
                sessionId));

        wil::unique_hkey key{};
        GUID value{};
        if (RegOpenKeyExW(HKEY_CURRENT_USER, sessionKeyPath, 0, KEY_ALL_ACCESS, &key) == ERROR_SUCCESS)
        {
            DWORD size = sizeof(GUID);
            if (RegQueryValueExW(key.get(), RegNameCurrentVirtualDesktop, 0, nullptr, reinterpret_cast<BYTE*>(&value), &size) == ERROR_SUCCESS)
            {
                *desktopId = value;
                return true;
            }
        }
        return false;
    }

    bool GetCurrentVirtualDesktopId(GUID* desktopId)
    {
        if (!GetDesktopIdFromCurrentSession(desktopId))
        {
            // Explorer persists current virtual desktop identifier to registry on a per session basis,
            // but only after first virtual desktop switch happens. If the user hasn't switched virtual
            // desktops (only primary desktop) in this session value in registry will be empty.
            // If this value is empty take first element from array of virtual desktops (not kept per session).
            std::vector<GUID> ids{};
            if (!GetVirtualDekstopIds(ids) || ids.empty())
            {
                return false;
            }
            *desktopId = ids[0];
        }
        return true;
    }

    bool GetVirtualDekstopIds(HKEY hKey, std::vector<GUID>& ids)
    {
        if (!hKey)
        {
            return false;
        }
        DWORD bufferCapacity;
        // request regkey binary buffer capacity only
        if (RegQueryValueExW(hKey, RegNameVirtualDesktopIds, 0, nullptr, nullptr, &bufferCapacity) != ERROR_SUCCESS)
        {
            return false;
        }
        std::unique_ptr<BYTE[]> buffer = std::make_unique<BYTE[]>(bufferCapacity);
        // request regkey binary content
        if (RegQueryValueExW(hKey, RegNameVirtualDesktopIds, 0, nullptr, buffer.get(), &bufferCapacity) != ERROR_SUCCESS)
        {
            return false;
        }
        const size_t guidSize = sizeof(GUID);
        std::vector<GUID> temp;
        temp.reserve(bufferCapacity / guidSize);
        for (size_t i = 0; i < bufferCapacity; i += guidSize)
        {
            GUID* guid = reinterpret_cast<GUID*>(buffer.get() + i);
            temp.push_back(*guid);
        }
        ids = std::move(temp);
        return true;
    }

    bool GetVirtualDekstopIds(std::vector<GUID>& ids)
    {
        return GetVirtualDekstopIds(GetVirtualDesktopsRegKey(), ids);
    }

    HKEY OpenVirtualDesktopsRegKey()
    {
        HKEY hKey{ nullptr };
        if (RegOpenKeyEx(HKEY_CURRENT_USER, RegKeyVirtualDesktops, 0, KEY_ALL_ACCESS, &hKey) == ERROR_SUCCESS)
        {
            return hKey;
        }
        return nullptr;
    }

    HKEY GetVirtualDesktopsRegKey()
    {
        static HKEY virtualDesktopsKey = OpenVirtualDesktopsRegKey();
        return virtualDesktopsKey;
    }

    void CloseVirtualDesktopsRegKey()
    {
        HKEY hKey = GetVirtualDesktopsRegKey();
        if (hKey)
        {
            RegCloseKey(hKey);
        }
    }
}
