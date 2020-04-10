#pragma once

#include "json.h"

class CSettings
{
public:
    static const int MAX_INPUT_STRING_LEN = 1024;

    enum class MRUStringType
    {
        MRU_SEARCH,
        MRU_REPLACE
    };

    CSettings();

    bool GetEnabled();

    void SetEnabled(bool enabled);

    inline bool GetShowIconOnMenu() const
    {
        return settings.showIconOnMenu;
    }

    inline void SetShowIconOnMenu(bool show)
    {
        settings.showIconOnMenu = show;
    }

    inline bool GetExtendedContextMenuOnly() const
    {
        return settings.extendedContextMenuOnly;
    }

    inline void SetExtendedContextMenuOnly(bool extendedOnly)
    {
        settings.extendedContextMenuOnly = extendedOnly;
    }

    inline bool GetPersistState() const
    {
        return settings.persistState;
    }

    inline void SetPersistState(bool persistState)
    {
        settings.persistState = persistState;
    }

    inline bool GetMRUEnabled() const
    {
        return settings.MRUEnabled;
    }

    inline void SetMRUEnabled(bool MRUEnabled)
    {
        settings.MRUEnabled = MRUEnabled;
    }

    inline long GetMaxMRUSize() const
    {
        return settings.maxMRUSize;
    }

    inline void SetMaxMRUSize(long maxMRUSize)
    {
        settings.maxMRUSize = maxMRUSize;
        if (searchMRUList)
        {
            searchMRUList->Resize(maxMRUSize);
        }
        if (replaceMRUList)
        {
            replaceMRUList->Resize(maxMRUSize);
        }
    }

    inline long GetFlags() const
    {
        return settings.flags;
    }

    inline void SetFlags(long flags)
    {
        settings.flags = flags;
    }

    inline const std::wstring& GetSearchText() const
    {
        return settings.searchText;
    }

    inline void SetSearchText(const std::wstring& text)
    {
        settings.searchText = text;
    }

    inline const std::wstring& GetReplaceText() const
    {
        return settings.replaceText;
    }

    inline void SetReplaceText(const std::wstring& text)
    {
        settings.replaceText = text;
    }

    void AddMRUString(const std::wstring& data, MRUStringType type);
    bool NextMRUString(std::wstring& data, MRUStringType type);
    void ResetMRUList(MRUStringType type);

    void SavePowerRenameData();

private:
    struct Settings
    {
        bool showIconOnMenu{ true };
        bool extendedContextMenuOnly{ false }; // Disabled by default.
        bool persistState{ true };
        bool MRUEnabled{ true };
        long maxMRUSize{ 10 };
        long flags{ 0 };
        std::wstring searchText{};
        std::wstring replaceText{};
    };

    class MRUList
    {
    public:
        MRUList(int size) :
            pushIdx(0),
            nextIdx(1),
            size(size)
        {
            items.resize(size);
        }

        void Push(const std::wstring& data);
        bool Next(std::wstring& data);

        void Resize(int newSize);
        void Reset();

    private:
        bool Exists(const std::wstring& data);

        std::vector<std::wstring> items;
        int pushIdx;
        int nextIdx;
        int size;
    };

    void LoadPowerRenameData();

    json::JsonArray SerializeSearchMRUList();
    json::JsonArray SerializeReplaceMRUList();

    void MigrateSettingsFromRegistry();
    void MigrateSearchMRUList();
    void MigrateReplaceMRUList();

    void ParseJsonSettings();

    Settings settings;
    std::wstring jsonFilePath;

    std::unique_ptr<MRUList> searchMRUList{ nullptr };
    std::unique_ptr<MRUList> replaceMRUList{ nullptr };
};

CSettings& CSettingsInstance();

HRESULT CRenameMRUSearch_CreateInstance(_Outptr_ IUnknown** ppUnk);
HRESULT CRenameMRUReplace_CreateInstance(_Outptr_ IUnknown** ppUnk);