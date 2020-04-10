#include "stdafx.h"
#include "Settings.h"
#include "PowerRenameInterfaces.h"
#include "settings_helpers.h"

#include <filesystem>
#include <commctrl.h>
#include <algorithm>

namespace
{
    const wchar_t c_powerRenameDataFilePath[] = L"power-rename-settings.json";

    const wchar_t c_rootRegPath[] = L"Software\\Microsoft\\PowerRename";
    const wchar_t c_mruSearchRegPath[] = L"\\SearchMRU";
    const wchar_t c_mruReplaceRegPath[] = L"\\ReplaceMRU";

    const wchar_t c_enabled[] = L"Enabled";
    const wchar_t c_showIconOnMenu[] = L"ShowIcon";
    const wchar_t c_extendedContextMenuOnly[] = L"ExtendedContextMenuOnly";
    const wchar_t c_persistState[] = L"PersistState";
    const wchar_t c_maxMRUSize[] = L"MaxMRUSize";
    const wchar_t c_flags[] = L"Flags";
    const wchar_t c_searchText[] = L"SearchText";
    const wchar_t c_replaceText[] = L"ReplaceText";
    const wchar_t c_mruEnabled[] = L"MRUEnabled";
    const wchar_t c_mruList[] = L"MRUList";
    const wchar_t c_mruSearchList[] = L"MRUSearchList";
    const wchar_t c_mruReplaceList[] = L"MRUReplaceList";

    long GetRegNumber(const std::wstring& valueName, long defaultValue)
    {
        DWORD type = REG_DWORD;
        DWORD data = 0;
        DWORD size = sizeof(DWORD);
        if (SHGetValue(HKEY_CURRENT_USER, c_rootRegPath, valueName.c_str(), &type, &data, &size) == ERROR_SUCCESS)
        {
            return data;
        }
        return defaultValue;
    }

    void SetRegNumber(const std::wstring& valueName, long value)
    {
        SHSetValue(HKEY_CURRENT_USER, c_rootRegPath, valueName.c_str(), REG_DWORD, &value, sizeof(value));
    }

    bool GetRegBoolean(const std::wstring& valueName, bool defaultValue)
    {
        DWORD value = GetRegNumber(valueName.c_str(), defaultValue ? 1 : 0);
        return (value == 0) ? false : true;
    }

    void SetRegBoolean(const std::wstring& valueName, bool value)
    {
        SetRegNumber(valueName, value ? 1 : 0);
    }

    std::wstring GetRegString(const std::wstring& valueName,const std::wstring& subPath)
    {
        wchar_t value[CSettings::MAX_INPUT_STRING_LEN];
        value[0] = L'\0';
        DWORD type = REG_SZ;
        DWORD size = CSettings::MAX_INPUT_STRING_LEN * sizeof(wchar_t);
        std::wstring completePath = std::wstring(c_rootRegPath) + subPath;
        SHGetValue(HKEY_CURRENT_USER, completePath.c_str(), valueName.c_str(), &type, value, &size);
        return std::wstring(value);
    }
}

class CRenameMRU :
    public IEnumString,
    public IPowerRenameMRU
{
public:
    // IUnknown
    IFACEMETHODIMP_(ULONG) AddRef();
    IFACEMETHODIMP_(ULONG) Release();
    IFACEMETHODIMP QueryInterface(_In_ REFIID riid, _Outptr_ void** ppv);

    // IEnumString
    IFACEMETHODIMP Next(__in ULONG celt, __out_ecount_part(celt, *pceltFetched) LPOLESTR* rgelt, __out_opt ULONG* pceltFetched);
    IFACEMETHODIMP Skip(__in ULONG) { return E_NOTIMPL; }
    IFACEMETHODIMP Reset();
    IFACEMETHODIMP Clone(__deref_out IEnumString** ppenum) { *ppenum = nullptr;  return E_NOTIMPL; }

    // IPowerRenameMRU
    IFACEMETHODIMP AddMRUString(_In_ PCWSTR entry);

    static HRESULT CreateInstance(_In_ CSettings::MRUStringType type, _Outptr_ IUnknown** ppUnk);

private:
    CRenameMRU(CSettings::MRUStringType type);

    CSettings::MRUStringType MRUType;
    long refCount = 0;
};

CRenameMRU::CRenameMRU(CSettings::MRUStringType type) :
    MRUType(type),
    refCount(1) {}

HRESULT CRenameMRU::CreateInstance(_In_ CSettings::MRUStringType type, _Outptr_ IUnknown** ppUnk)
{
    *ppUnk = nullptr;
    HRESULT hr = CSettingsInstance().GetMaxMRUSize() > 0 ? S_OK : E_FAIL;
    if (SUCCEEDED(hr))
    {
        CRenameMRU* renameMRU = new CRenameMRU(type);
        hr = renameMRU ? S_OK : E_OUTOFMEMORY;
        if (SUCCEEDED(hr))
        {
            renameMRU->QueryInterface(IID_PPV_ARGS(ppUnk));
            renameMRU->Release();
        }
    }

    return hr;
}

// IUnknown
IFACEMETHODIMP_(ULONG) CRenameMRU::AddRef()
{
    return InterlockedIncrement(&refCount);
}

IFACEMETHODIMP_(ULONG) CRenameMRU::Release()
{
    long cnt = InterlockedDecrement(&refCount);

    if (cnt == 0)
    {
        delete this;
    }
    return cnt;
}

IFACEMETHODIMP CRenameMRU::QueryInterface(_In_ REFIID riid, _Outptr_ void** ppv)
{
    static const QITAB qit[] = {
        QITABENT(CRenameMRU, IEnumString),
        QITABENT(CRenameMRU, IPowerRenameMRU),
        { 0 }
    };
    return QISearch(this, qit, riid, ppv);
}


// IEnumString
IFACEMETHODIMP CRenameMRU::Reset()
{
    CSettingsInstance().ResetMRUList(MRUType);
    return S_OK;
}

IFACEMETHODIMP CRenameMRU::Next(__in ULONG celt, __out_ecount_part(celt, *pceltFetched) LPOLESTR* rgelt, __out_opt ULONG* pceltFetched)
{
    if (pceltFetched)
    {
        *pceltFetched = 0;
    }

    if (!celt)
    {
        return S_OK;
    }

    if (!rgelt)
    {
        return S_FALSE;
    }

    HRESULT hr = S_FALSE;
    if (std::wstring data{}; CSettingsInstance().NextMRUString(data, MRUType))
    {
        hr = SHStrDup(data.c_str(), rgelt);
        if (SUCCEEDED(hr) && pceltFetched != nullptr)
        {
            *pceltFetched = 1;
        }
    }

    return hr;
}

IFACEMETHODIMP CRenameMRU::AddMRUString(_In_ PCWSTR entry)
{
    CSettingsInstance().AddMRUString(entry, MRUType);
    return S_OK;
}

CSettings::CSettings()
{
    std::wstring result = PTSettingsHelper::get_module_save_folder_location(L"PowerRename");
    jsonFilePath = result + L"\\" + std::wstring(c_powerRenameDataFilePath);
    LoadPowerRenameData();
}

bool CSettings::GetEnabled()
{
    return GetRegBoolean(c_enabled, true);
}

void CSettings::SetEnabled(bool enabled)
{
    SetRegBoolean(c_enabled, enabled);
}

void CSettings::AddMRUString(const std::wstring& data, MRUStringType type)
{
    if (type == MRUStringType::MRU_SEARCH && searchMRUList)
    {
        searchMRUList->Push(data);
    }
    else if (type == MRUStringType::MRU_REPLACE && replaceMRUList)
    {
        replaceMRUList->Push(data);
    }
}

bool CSettings::NextMRUString(std::wstring& data, MRUStringType type)
{
    if (type == MRUStringType::MRU_SEARCH && searchMRUList)
    {
        return searchMRUList->Next(data);
    }
    else if (type == MRUStringType::MRU_REPLACE && replaceMRUList)
    {
        return replaceMRUList->Next(data);
    }
    return false;
}

void CSettings::ResetMRUList(MRUStringType type)
{
    if (type == MRUStringType::MRU_SEARCH && searchMRUList)
    {
        searchMRUList->Reset();
    }
    else if (type == MRUStringType::MRU_REPLACE && replaceMRUList)
    {
        replaceMRUList->Reset();
    }
}

void CSettings::SavePowerRenameData()
{
    json::JsonObject jsonData;

    jsonData.SetNamedValue(c_showIconOnMenu,          json::value(settings.showIconOnMenu));
    jsonData.SetNamedValue(c_extendedContextMenuOnly, json::value(settings.extendedContextMenuOnly));
    jsonData.SetNamedValue(c_persistState,            json::value(settings.persistState));
    jsonData.SetNamedValue(c_mruEnabled,              json::value(settings.MRUEnabled));
    jsonData.SetNamedValue(c_maxMRUSize,              json::value(settings.maxMRUSize));
    jsonData.SetNamedValue(c_flags,                   json::value(settings.flags));
    jsonData.SetNamedValue(c_searchText,              json::value(settings.searchText));
    jsonData.SetNamedValue(c_replaceText,             json::value(settings.replaceText));

    if (settings.MRUEnabled)
    {
        if (searchMRUList)
        {
            jsonData.SetNamedValue(c_mruSearchList, SerializeSearchMRUList());
        }
        if (replaceMRUList)
        {
            jsonData.SetNamedValue(c_mruReplaceList, SerializeReplaceMRUList());
        }
    }

    json::to_file(jsonFilePath, jsonData);
}

void CSettings::LoadPowerRenameData()
{
    if (!std::filesystem::exists(jsonFilePath))
    {
        MigrateSettingsFromRegistry();

        SavePowerRenameData();
    }
    else
    {
        ParseJsonSettings();
    }
}

json::JsonArray CSettings::SerializeSearchMRUList()
{
    json::JsonArray searchMRU{};

    std::wstring data{};
    while (searchMRUList->Next(data))
    {
        searchMRU.Append(json::value(data));
    }
    searchMRUList->Reset();

    return searchMRU;
}

json::JsonArray CSettings::SerializeReplaceMRUList()
{
    json::JsonArray replaceMRU{};

    std::wstring data{};
    while (replaceMRUList->Next(data))
    {
        replaceMRU.Append(json::value(data));
    }
    replaceMRUList->Reset();

    return replaceMRU;
}

void CSettings::MigrateSettingsFromRegistry()
{
    settings.showIconOnMenu          = GetRegBoolean(c_showIconOnMenu, true);
    settings.extendedContextMenuOnly = GetRegBoolean(c_extendedContextMenuOnly, false); // Disabled by default.
    settings.persistState            = GetRegBoolean(c_persistState, true);
    settings.MRUEnabled              = GetRegBoolean(c_mruEnabled, true);
    settings.maxMRUSize              = GetRegNumber(c_maxMRUSize, 10);
    settings.flags                   = GetRegNumber(c_flags, 0);
    settings.searchText              = GetRegString(c_searchText, L"");
    settings.replaceText             = GetRegString(c_replaceText, L"");

    MigrateSearchMRUList();
    MigrateReplaceMRUList();
}

void CSettings::MigrateSearchMRUList()
{
    searchMRUList = std::make_unique<MRUList>(settings.maxMRUSize);
    std::wstring searchListKeys = GetRegString(c_mruList, c_mruSearchRegPath);
    std::sort(std::begin(searchListKeys), std::end(searchListKeys));
    for (const wchar_t& key : searchListKeys)
    {
        searchMRUList->Push(GetRegString(std::wstring(1, key), c_mruSearchRegPath));
    }
}

void CSettings::MigrateReplaceMRUList()
{
    replaceMRUList = std::make_unique<MRUList>(settings.maxMRUSize);
    std::wstring replaceListKeys = GetRegString(c_mruList, c_mruReplaceRegPath);
    std::sort(std::begin(replaceListKeys), std::end(replaceListKeys));
    for (const wchar_t& key : replaceListKeys)
    {
        replaceMRUList->Push(GetRegString(std::wstring(1, key), c_mruReplaceRegPath));
    }
}

void CSettings::ParseJsonSettings()
{
    auto json = json::from_file(jsonFilePath);
    if (json)
    {
        const json::JsonObject& jsonSettings = json.value();
        try
        {
            if (json::has(jsonSettings, c_showIconOnMenu, json::JsonValueType::Boolean))
            {
                settings.showIconOnMenu = jsonSettings.GetNamedBoolean(c_showIconOnMenu);
            }
            if (json::has(jsonSettings, c_extendedContextMenuOnly, json::JsonValueType::Boolean))
            {
                settings.extendedContextMenuOnly = jsonSettings.GetNamedBoolean(c_extendedContextMenuOnly);
            }
            if (json::has(jsonSettings, c_persistState, json::JsonValueType::Boolean))
            {
                settings.persistState = jsonSettings.GetNamedBoolean(c_persistState);
            }
            if (json::has(jsonSettings, c_mruEnabled, json::JsonValueType::Boolean))
            {
                settings.MRUEnabled = jsonSettings.GetNamedBoolean(c_mruEnabled);
            }
            if (json::has(jsonSettings, c_maxMRUSize, json::JsonValueType::Number))
            {
                settings.maxMRUSize = (long)jsonSettings.GetNamedNumber(c_maxMRUSize);
            }
            if (json::has(jsonSettings, c_flags, json::JsonValueType::Number))
            {
                settings.flags = (long)jsonSettings.GetNamedNumber(c_flags);
            }
            if (json::has(jsonSettings, c_searchText, json::JsonValueType::String))
            {
                settings.searchText = jsonSettings.GetNamedString(c_searchText);
            }
            if (json::has(jsonSettings, c_replaceText, json::JsonValueType::String))
            {
                settings.replaceText = jsonSettings.GetNamedString(c_replaceText);
            }
            if (json::has(jsonSettings, c_mruSearchList, json::JsonValueType::Array))
            {
                searchMRUList = std::make_unique<MRUList>(settings.maxMRUSize);
                auto searchList = jsonSettings.GetNamedArray(c_mruSearchList);
                for (uint32_t i = 0; i < searchList.Size(); ++i)
                {
                    searchMRUList->Push(std::wstring(searchList.GetStringAt(i)));
                }
            }
            if (json::has(jsonSettings, c_mruReplaceList, json::JsonValueType::Array))
            {
                replaceMRUList = std::make_unique<MRUList>(settings.maxMRUSize);
                auto replaceList = jsonSettings.GetNamedArray(c_mruReplaceList);
                for (uint32_t i = 0; i < replaceList.Size(); ++i)
                {
                    replaceMRUList->Push(std::wstring(replaceList.GetStringAt(i)));
                }
            }
        }
        catch (const winrt::hresult_error&) { }
    }
}

CSettings& CSettingsInstance()
{
    static CSettings instance;
    return instance;
}

HRESULT CRenameMRUSearch_CreateInstance(_Outptr_ IUnknown** ppUnk)
{
    return CRenameMRU::CreateInstance(CSettings::MRUStringType::MRU_SEARCH, ppUnk);
}

HRESULT CRenameMRUReplace_CreateInstance(_Outptr_ IUnknown** ppUnk)
{
    return CRenameMRU::CreateInstance(CSettings::MRUStringType::MRU_REPLACE, ppUnk);
}

void CSettings::MRUList::Push(const std::wstring& data)
{
    if (Exists(data))
    {
        // TODO: Already existing item should be put on top of MRU list.
        return;
    }
    items[pushIdx] = data;
    pushIdx = (pushIdx + 1) % size;
}

bool CSettings::MRUList::Next(std::wstring& data)
{
    if (nextIdx == size + 1)
    {
        Reset();
        return false;
    }
    // Go backwards to consume latest items first.
    int idx = (pushIdx + size - nextIdx) % size;
    if (items[idx].empty())
    {
        Reset();
        return false;
    }
    data = items[idx];
    ++nextIdx;
    return true;
}

void CSettings::MRUList::Resize(int newSize)
{
    if (newSize < size)
    {
        // Size of most recently used items list is reduced. Take only latest ones.
        std::vector<std::wstring> temp;
        temp.reserve(newSize);
        std::wstring data{};
        for (size_t i = 0; i < newSize && Next(data); ++i)
        {
            temp.push_back(data);
        }
        pushIdx = temp.size() % newSize;
        std::reverse(std::begin(temp), std::end(temp));
        items = std::move(temp);
    }
    else
    {
        items.resize(newSize);
    }
    size = newSize;
    Reset();
}

void CSettings::MRUList::Reset()
{
    nextIdx = 1;
}

bool CSettings::MRUList::Exists(const std::wstring& data)
{
    return std::find(std::begin(items), std::end(items), data) != std::end(items);
}
