#include "stdafx.h"
#include "Settings.h"
#include "PowerRenameInterfaces.h"

#include <settings_helpers.h>
#include <filesystem>
#include <commctrl.h>

namespace
{
    const wchar_t* POWER_RENAME_DATA_FILE = L"power-rename-settings.json";

    const wchar_t c_rootRegPath[] = L"Software\\Microsoft\\PowerRename";
    const wchar_t c_mruSearchRegPath[] = L"SearchMRU";
    const wchar_t c_mruReplaceRegPath[] = L"ReplaceMRU";

    const wchar_t c_enabled[] = L"Enabled";
    const wchar_t c_showIconOnMenu[] = L"ShowIcon";
    const wchar_t c_extendedContextMenuOnly[] = L"ExtendedContextMenuOnly";
    const wchar_t c_persistState[] = L"PersistState";
    const wchar_t c_maxMRUSize[] = L"MaxMRUSize";
    const wchar_t c_flags[] = L"Flags";
    const wchar_t c_searchText[] = L"SearchText";
    const wchar_t c_replaceText[] = L"ReplaceText";
    const wchar_t c_mruEnabled[] = L"MRUEnabled";

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

    bool GetRegBoolean(const std::wstring& valueName, bool defaultValue)
    {
        DWORD value = GetRegNumber(valueName.c_str(), (defaultValue == 0) ? false : true);
        return (value == 0) ? false : true;
    }

    std::wstring GetRegString(const std::wstring& valueName) {
        wchar_t* value = new wchar_t[CSettings::MAX_INPUT_STRING_LEN];
        value[0] = L'\0';
        DWORD type = REG_SZ;
        DWORD size = CSettings::MAX_INPUT_STRING_LEN * sizeof(wchar_t);
        if (SUCCEEDED(HRESULT_FROM_WIN32(SHGetValue(HKEY_CURRENT_USER, c_rootRegPath, valueName.c_str(), &type, value, &size) == ERROR_SUCCESS)))
        {
            return std::wstring(value);
        }
        return std::wstring{};
    }
}

typedef int (CALLBACK* MRUCMPPROC)(LPCWSTR, LPCWSTR);

typedef struct {
    DWORD      cbSize;
    UINT       uMax;
    UINT       fFlags;
    HKEY       hKey;
    LPCTSTR    lpszSubKey;
    MRUCMPPROC lpfnCompare;
} MRUINFO;

typedef HANDLE (*CreateMRUListFn)(MRUINFO* pmi);
typedef int (*AddMRUStringFn)(HANDLE hMRU, LPCWSTR data);
typedef int (*EnumMRUListFn)(HANDLE hMRU, int nItem, void* lpData, UINT uLen);
typedef int (*FreeMRUListFn)(HANDLE hMRU);

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

    static HRESULT CreateInstance(_In_ PCWSTR regPathMRU, _In_ ULONG maxMRUSize, _Outptr_ IUnknown** ppUnk);

private:
    CRenameMRU();
    ~CRenameMRU();

    HRESULT _Initialize(_In_ PCWSTR regPathMRU, __in ULONG maxMRUSize);
    HRESULT _CreateMRUList(_In_ MRUINFO* pmi);
    int _AddMRUString(_In_ PCWSTR data);
    int _EnumMRUList(_In_ int nItem, _Out_ void* lpData, _In_ UINT uLen);
    void _FreeMRUList();

    long   m_refCount = 0;
    HKEY   m_hKey = NULL;
    ULONG  m_maxMRUSize = 0;
    ULONG  m_mruIndex = 0;
    ULONG  m_mruSize = 0;
    HANDLE m_mruHandle = NULL;
    HMODULE m_hComctl32Dll = NULL;
    PWSTR  m_regPath = nullptr;
};

CRenameMRU::CRenameMRU() :
    m_refCount(1)
{}

CRenameMRU::~CRenameMRU()
{
    if (m_hKey)
    {
        RegCloseKey(m_hKey);
    }

    _FreeMRUList();

    if (m_hComctl32Dll)
    {
        FreeLibrary(m_hComctl32Dll);
    }

    CoTaskMemFree(m_regPath);
}

HRESULT CRenameMRU::CreateInstance(_In_ PCWSTR regPathMRU, _In_ ULONG maxMRUSize, _Outptr_ IUnknown** ppUnk)
{
    *ppUnk = nullptr;
    HRESULT hr = (regPathMRU && maxMRUSize > 0) ? S_OK : E_FAIL;
    if (SUCCEEDED(hr))
    {
        CRenameMRU* renameMRU = new CRenameMRU();
        hr = renameMRU ? S_OK : E_OUTOFMEMORY;
        if (SUCCEEDED(hr))
        {
            hr = renameMRU->_Initialize(regPathMRU, maxMRUSize);
            if (SUCCEEDED(hr))
            {
                hr = renameMRU->QueryInterface(IID_PPV_ARGS(ppUnk));
            }

            renameMRU->Release();
        }
    }

    return hr;
}

// IUnknown
IFACEMETHODIMP_(ULONG) CRenameMRU::AddRef()
{
    return InterlockedIncrement(&m_refCount);
}

IFACEMETHODIMP_(ULONG) CRenameMRU::Release()
{
    long refCount = InterlockedDecrement(&m_refCount);

    if (refCount == 0)
    {
        delete this;
    }
    return refCount;
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

HRESULT CRenameMRU::_Initialize(_In_ PCWSTR regPathMRU, __in ULONG maxMRUSize)
{
    m_maxMRUSize = maxMRUSize;

    wchar_t regPath[MAX_PATH] = { 0 };
    HRESULT hr = StringCchPrintf(regPath, ARRAYSIZE(regPath), L"%s\\%s", c_rootRegPath, regPathMRU);
    if (SUCCEEDED(hr))
    {
        hr = SHStrDup(regPathMRU, &m_regPath);

        if (SUCCEEDED(hr))
        {
            MRUINFO mi = {
                sizeof(MRUINFO),
                maxMRUSize,
                0,
                HKEY_CURRENT_USER,
                regPath,
                nullptr
            };

            hr = _CreateMRUList(&mi);
            if (SUCCEEDED(hr))
            {
                m_mruSize = _EnumMRUList(-1, NULL, 0);
            }
            else
            {
                hr = E_FAIL;
            }
        }
    }

    return hr;
}

// IEnumString
IFACEMETHODIMP CRenameMRU::Reset()
{
    m_mruIndex = 0;
    return S_OK;
}

#define MAX_ENTRY_STRING 1024

IFACEMETHODIMP CRenameMRU::Next(__in ULONG celt, __out_ecount_part(celt, *pceltFetched) LPOLESTR* rgelt, __out_opt ULONG* pceltFetched)
{
    HRESULT hr = S_OK;
    WCHAR mruEntry[MAX_ENTRY_STRING];
    mruEntry[0] = L'\0';

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

    hr = S_FALSE;
    if (m_mruIndex <= m_mruSize && _EnumMRUList(m_mruIndex++, (void*)mruEntry, ARRAYSIZE(mruEntry)) > 0)
    {
        hr = SHStrDup(mruEntry, rgelt);
        if (SUCCEEDED(hr) && pceltFetched != nullptr)
        {
            *pceltFetched = 1;
        }
    }

    return hr;
}

IFACEMETHODIMP CRenameMRU::AddMRUString(_In_ PCWSTR entry)
{
    return (_AddMRUString(entry) < 0) ? E_FAIL : S_OK;
}

HRESULT CRenameMRU::_CreateMRUList(_In_ MRUINFO* pmi)
{
    if (m_mruHandle != NULL)
    {
        _FreeMRUList();
    }

    if (m_hComctl32Dll == NULL)
    {
        m_hComctl32Dll = LoadLibraryEx(L"comctl32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    }

    if (m_hComctl32Dll != nullptr)
    {
        CreateMRUListFn pfnCreateMRUList = reinterpret_cast<CreateMRUListFn>(GetProcAddress(m_hComctl32Dll, (LPCSTR)MAKEINTRESOURCE(400)));
        if (pfnCreateMRUList != nullptr)
        {
            m_mruHandle = pfnCreateMRUList(pmi);
        }
    }

    return (m_mruHandle != NULL) ? S_OK : E_FAIL;
}

int CRenameMRU::_AddMRUString(_In_ PCWSTR data)
{
    int retVal = -1;
    if (m_mruHandle != NULL)
    {
        if (m_hComctl32Dll == NULL)
        {
            m_hComctl32Dll = LoadLibraryEx(L"comctl32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        }

        if (m_hComctl32Dll != nullptr)
        {
            AddMRUStringFn pfnAddMRUString = reinterpret_cast<AddMRUStringFn>(GetProcAddress(m_hComctl32Dll, (LPCSTR)MAKEINTRESOURCE(401)));
            if (pfnAddMRUString != nullptr)
            {
                retVal = pfnAddMRUString(m_mruHandle, data);
            }
        }
    }

    return retVal;
}

int CRenameMRU::_EnumMRUList(_In_ int nItem, _Out_ void* lpData, _In_ UINT uLen)
{
    int retVal = -1;
    if (m_mruHandle != NULL)
    {
        if (m_hComctl32Dll == NULL)
        {
            m_hComctl32Dll = LoadLibraryEx(L"comctl32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        }

        if (m_hComctl32Dll != nullptr)
        {
            EnumMRUListFn pfnEnumMRUList = reinterpret_cast<EnumMRUListFn>(GetProcAddress(m_hComctl32Dll, (LPCSTR)MAKEINTRESOURCE(403)));
            if (pfnEnumMRUList != nullptr)
            {
                retVal = pfnEnumMRUList(m_mruHandle, nItem, lpData, uLen);
            }
        }
    }

    return retVal;
}

void CRenameMRU::_FreeMRUList()
{
    if (m_mruHandle != NULL)
    {
        if (m_hComctl32Dll == NULL)
        {
            m_hComctl32Dll = LoadLibraryEx(L"comctl32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        }

        if (m_hComctl32Dll != nullptr)
        {
            FreeMRUListFn pfnFreeMRUList = reinterpret_cast<FreeMRUListFn>(GetProcAddress(m_hComctl32Dll, (LPCSTR)MAKEINTRESOURCE(152)));
            if (pfnFreeMRUList != nullptr)
            {
                pfnFreeMRUList(m_mruHandle);
            }
            
        }
        m_mruHandle = NULL;
    }
}

CSettings::CSettings()
{
    std::wstring result = PTSettingsHelper::get_module_save_folder_location(L"PowerRename");
    mJsonFilePath = result + L"\\" + std::wstring(POWER_RENAME_DATA_FILE);
}

void CSettings::LoadPowerRenameData()
{
    if (!std::filesystem::exists(mJsonFilePath))
    {
        MigrateSettingsFromRegistry();
    }
    else
    {
        ParseJsonSettings(GetPersistPowerRenameData());

        SavePowerRenameData();
    }
}

void CSettings::SavePowerRenameData() const
{
    json::JsonObject jsonData;

    jsonData.SetNamedValue(c_enabled, json::value(mSettings.mEnabled));
    jsonData.SetNamedValue(c_showIconOnMenu, json::value(mSettings.mShowIconOnMenu));
    jsonData.SetNamedValue(c_extendedContextMenuOnly, json::value(mSettings.mExtendedContextMenuOnly));
    jsonData.SetNamedValue(c_persistState, json::value(mSettings.mPersistState));
    jsonData.SetNamedValue(c_mruEnabled, json::value(mSettings.mMRUEnabled));
    jsonData.SetNamedValue(c_maxMRUSize, json::value(mSettings.mMaxMRUSize));
    jsonData.SetNamedValue(c_flags, json::value(mSettings.mFlags));
    jsonData.SetNamedValue(c_searchText, json::value(mSettings.mSearchText));
    jsonData.SetNamedValue(c_replaceText, json::value(mSettings.mReplaceText));

    json::to_file(mJsonFilePath, jsonData);
}

json::JsonObject CSettings::GetPersistPowerRenameData()
{
    auto jsonData = json::from_file(mJsonFilePath);
    return jsonData ? jsonData.value() : json::JsonObject();
}

void CSettings::MigrateSettingsFromRegistry()
{
    mSettings.mEnabled = GetRegBoolean(c_enabled, true);
    mSettings.mShowIconOnMenu = GetRegBoolean(c_showIconOnMenu, true);
    mSettings.mExtendedContextMenuOnly = GetRegBoolean(c_extendedContextMenuOnly, false);
    mSettings.mPersistState = GetRegBoolean(c_persistState, true);
    mSettings.mMRUEnabled = GetRegBoolean(c_mruEnabled, true);
    mSettings.mMaxMRUSize = GetRegNumber(c_maxMRUSize, 10);
    mSettings.mFlags = GetRegNumber(c_flags, 0);
    mSettings.mSearchText = GetRegString(c_searchText);
    mSettings.mReplaceText = GetRegString(c_replaceText);
}

void CSettings::ParseJsonSettings(const json::JsonObject& jsonSettings)
{
    try
    {
        mSettings.mEnabled = jsonSettings.GetNamedBoolean(c_enabled);
        mSettings.mShowIconOnMenu = jsonSettings.GetNamedBoolean(c_showIconOnMenu);
        mSettings.mExtendedContextMenuOnly = jsonSettings.GetNamedBoolean(c_extendedContextMenuOnly);
        mSettings.mPersistState = jsonSettings.GetNamedBoolean(c_persistState);
        mSettings.mMRUEnabled = jsonSettings.GetNamedBoolean(c_mruEnabled);
        mSettings.mMaxMRUSize = (long)jsonSettings.GetNamedNumber(c_maxMRUSize);
        mSettings.mFlags = (long)jsonSettings.GetNamedNumber(c_flags);
        mSettings.mSearchText = jsonSettings.GetNamedString(c_searchText);
        mSettings.mReplaceText = jsonSettings.GetNamedString(c_replaceText);
    }
    catch (const winrt::hresult_error&) { }
}

CSettings& CSettingsInstance()
{
    static CSettings instance;
    return instance;
}

HRESULT CRenameMRUSearch_CreateInstance(_Outptr_ IUnknown** ppUnk)
{
    return CRenameMRU::CreateInstance(c_mruSearchRegPath, CSettingsInstance().GetMaxMRUSize(), ppUnk);
}

HRESULT CRenameMRUReplace_CreateInstance(_Outptr_ IUnknown** ppUnk)
{
    return CRenameMRU::CreateInstance(c_mruReplaceRegPath, CSettingsInstance().GetMaxMRUSize(), ppUnk);
}
