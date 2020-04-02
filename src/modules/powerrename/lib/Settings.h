#pragma once

#include <json.h>
#include <string>

class CSettings
{
public:
    static const int MAX_INPUT_STRING_LEN = 1024;

    CSettings();

    inline bool GetEnabled() const {
        return mSettings.mEnabled;
    }

    inline void SetEnabled(bool enabled) {
        mSettings.mEnabled = enabled;
    }

    inline bool GetShowIconOnMenu() const {
        return mSettings.mShowIconOnMenu;
    }

    inline void SetShowIconOnMenu(bool show) {
        mSettings.mShowIconOnMenu = show;
    }

    inline bool GetExtendedContextMenuOnly() const {
        return mSettings.mExtendedContextMenuOnly;
    }

    inline void SetExtendedContextMenuOnly(bool extendedOnly) {
        mSettings.mExtendedContextMenuOnly = extendedOnly;
    }

    inline bool GetPersistState() const {
        return mSettings.mPersistState;
    }

    inline void SetPersistState(bool persistState) {
        mSettings.mPersistState = persistState;
    }

    inline bool GetMRUEnabled() const {
        return mSettings.mMRUEnabled;
    }

    inline void SetMRUEnabled(bool enabled) {
        mSettings.mMRUEnabled = enabled;
    }

    inline long GetMaxMRUSize() const {
        return mSettings.mMaxMRUSize;
    }

    inline void SetMaxMRUSize(long maxMRUSize) {
        mSettings.mMaxMRUSize = maxMRUSize;
    }

    inline long GetFlags() const {
        return mSettings.mFlags;
    }

    inline void SetFlags(long flags) {
        mSettings.mFlags = flags;
    }

    inline const std::wstring& GetSearchText() const {
        return mSettings.mSearchText;
    }

    inline void SetSearchText(const std::wstring& text) {
        mSettings.mSearchText = text;
    }

    inline const std::wstring& GetReplaceText() const {
        return mSettings.mReplaceText;
    }

    inline void SetReplaceText(const std::wstring& text) {
        mSettings.mReplaceText = text;
    }

    void LoadPowerRenameData();
    void SavePowerRenameData() const;

private:
    struct Settings
    {
        bool mEnabled{ true };
        bool mShowIconOnMenu{ true };
        bool mExtendedContextMenuOnly{ false };
        bool mPersistState{ true };
        bool mMRUEnabled{ true };
        long mMaxMRUSize{ 10 };
        long mFlags{ 0 };
        std::wstring mSearchText;
        std::wstring mReplaceText;
    };

    json::JsonObject GetPersistPowerRenameData();

    void MigrateSettingsFromRegistry();
    void ParseJsonSettings(const json::JsonObject& jsonSettings);

    Settings mSettings;
    std::wstring mJsonFilePath;
};

CSettings& CSettingsInstance();

HRESULT CRenameMRUSearch_CreateInstance(_Outptr_ IUnknown** ppUnk);
HRESULT CRenameMRUReplace_CreateInstance(_Outptr_ IUnknown** ppUnk);