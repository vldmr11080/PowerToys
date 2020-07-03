#pragma once

#include <common/settings_helpers.h>
#include <common/json.h>
#include <mutex>

#include <string>
#include <strsafe.h>
#include <unordered_map>
#include <variant>
#include <optional>
#include <vector>
#include <winnt.h>

namespace JSONHelpers
{
    constexpr int MAX_ZONE_COUNT = 50;

    #if defined(UNIT_TESTS)
    bool isValidGuid(const std::wstring& str);
    bool isValidDeviceId(const std::wstring& str);
    #endif

    enum class ZoneSetLayoutType : int
    {
        Blank = -1,
        Focus,
        Columns,
        Rows,
        Grid,
        PriorityGrid,
        Custom
    };

    enum class CustomLayoutType : int
    {
        Grid = 0,
        Canvas
    };

    std::wstring TypeToString(ZoneSetLayoutType type);
    ZoneSetLayoutType TypeFromString(const std::wstring& typeStr);

    ZoneSetLayoutType TypeFromLayoutId(int layoutID);

    struct CanvasLayoutInfo
    {
        int lastWorkAreaWidth;
        int lastWorkAreaHeight;

        struct Rect
        {
            int x;
            int y;
            int width;
            int height;
        };
        std::vector<CanvasLayoutInfo::Rect> zones;

        static json::JsonObject ToJson(const CanvasLayoutInfo& canvasInfo);
        static std::optional<CanvasLayoutInfo> FromJson(const json::JsonObject& infoJson);
    };

    class GridLayoutInfo
    {
    public:
        struct Minimal
        {
            int rows;
            int columns;
        };

        struct Full
        {
            int rows;
            int columns;
            const std::vector<int>& rowsPercents;
            const std::vector<int>& columnsPercents;
            const std::vector<std::vector<int>>& cellChildMap;
        };

        GridLayoutInfo(const Minimal& info);
        GridLayoutInfo(const Full& info);
        ~GridLayoutInfo() = default;

        static json::JsonObject ToJson(const GridLayoutInfo& gridInfo);
        static std::optional<GridLayoutInfo> FromJson(const json::JsonObject& infoJson);

        inline std::vector<int>& rowsPercents() { return m_rowsPercents; };
        inline std::vector<int>& columnsPercents() { return m_columnsPercents; };
        inline std::vector<std::vector<int>>& cellChildMap() { return m_cellChildMap; };

        inline int rows() const { return m_rows; }
        inline int columns() const { return m_columns; }

        inline const std::vector<int>& rowsPercents() const { return m_rowsPercents; };
        inline const std::vector<int>& columnsPercents() const { return m_columnsPercents; };
        inline const std::vector<std::vector<int>>& cellChildMap() const { return m_cellChildMap; };

    private:
        int m_rows;
        int m_columns;
        std::vector<int> m_rowsPercents;
        std::vector<int> m_columnsPercents;
        std::vector<std::vector<int>> m_cellChildMap;
    };

    struct CustomZoneSetData
    {
        std::wstring name;
        CustomLayoutType type;
        std::variant<CanvasLayoutInfo, GridLayoutInfo> info;
    };

    struct CustomZoneSetJSON
    {
        std::wstring uuid;
        CustomZoneSetData data;

        static json::JsonObject ToJson(const CustomZoneSetJSON& device);
        static std::optional<CustomZoneSetJSON> FromJson(const json::JsonObject& customZoneSet);
    };

    // TODO(stefan): This needs to be moved to ZoneSet.h (probably)
    struct ZoneSetData
    {
        std::wstring uuid;
        ZoneSetLayoutType type;

        static json::JsonObject ToJson(const ZoneSetData& zoneSet);
        static std::optional<ZoneSetData> FromJson(const json::JsonObject& zoneSet);
    };

    struct AppZoneHistoryData
    {
        std::unordered_map<DWORD, HWND> processIdToHandleMap; // maps process id of application to zoned window handle

        std::wstring zoneSetUuid;
        std::wstring deviceId;
        std::vector<int> zoneIndexSet;
    };

    struct AppZoneHistoryJSON
    {
        std::wstring appPath;
        std::vector<AppZoneHistoryData> data;

        static json::JsonObject ToJson(const AppZoneHistoryJSON& appZoneHistory);
        static std::optional<AppZoneHistoryJSON> FromJson(const json::JsonObject& zoneSet);
    };

    struct DeviceInfoData
    {
        ZoneSetData activeZoneSet;
        bool showSpacing;
        int spacing;
        int zoneCount;
    };

    struct DeviceInfoJSON
    {
        std::wstring deviceId;
        DeviceInfoData data;

        static json::JsonObject ToJson(const DeviceInfoJSON& device);
        static std::optional<DeviceInfoJSON> FromJson(const json::JsonObject& device);
    };

    class FancyZonesData
    {
        mutable std::recursive_mutex dataLock;

    public:
        FancyZonesData();

        inline const std::wstring& GetPersistFancyZonesJSONPath() const
        {
            return jsonFilePath;
        }

        inline const std::wstring& GetPersistAppZoneHistoryFilePath() const
        {
            return appZoneHistoryFilePath;
        }

        json::JsonObject GetPersistFancyZonesJSON();

        std::optional<DeviceInfoData> FindDeviceInfo(const std::wstring& zoneWindowId) const;

        std::optional<CustomZoneSetData> FindCustomZoneSet(const std::wstring& guid) const;

        inline const std::unordered_map<std::wstring, DeviceInfoData>& GetDeviceInfoMap() const
        {
            std::scoped_lock lock{ dataLock };
            return deviceInfoMap;
        }

        inline const std::unordered_map<std::wstring, CustomZoneSetData>& GetCustomZoneSetsMap() const
        {
            std::scoped_lock lock{ dataLock };
            return customZoneSetsMap;
        }

        inline const std::unordered_map<std::wstring, std::vector<AppZoneHistoryData>>& GetAppZoneHistoryMap() const
        {
            std::scoped_lock lock{ dataLock };
            return appZoneHistoryMap;
        }

#if defined(UNIT_TESTS)
        inline void clear_data()
        {
            appZoneHistoryMap.clear();
            deviceInfoMap.clear();
            customZoneSetsMap.clear();
        }

        inline void SetDeviceInfo(const std::wstring& deviceId, DeviceInfoData data)
        {
            deviceInfoMap[deviceId] = data;
        }

        inline void SetSettingsModulePath(std::wstring_view moduleName)
        {
            std::wstring result = PTSettingsHelper::get_module_save_folder_location(moduleName);
            jsonFilePath = result + L"\\" + std::wstring(L"zones-settings.json");
            appZoneHistoryFilePath = result + L"\\" + std::wstring(L"app-zone-history.json");
        }
#endif

        inline bool DeleteTmpFile(std::wstring_view tmpFilePath) const
        {
            return DeleteFileW(tmpFilePath.data());
        }

        void AddDevice(const std::wstring& deviceId);
        void CloneDeviceInfo(const std::wstring& source, const std::wstring& destination);
        void UpdatePrimaryDesktopData(const std::wstring& desktopId);
        void RemoveDeletedDesktops(const std::vector<std::wstring>& activeDesktops);

        bool IsAnotherWindowOfApplicationInstanceZoned(HWND window, const std::wstring_view& deviceId) const;
        void UpdateProcessIdToHandleMap(HWND window, const std::wstring_view& deviceId);
        std::vector<int> GetAppLastZoneIndexSet(HWND window, const std::wstring_view& deviceId, const std::wstring_view& zoneSetId) const;
        bool RemoveAppLastZone(HWND window, const std::wstring_view& deviceId, const std::wstring_view& zoneSetId);
        bool SetAppLastZones(HWND window, const std::wstring& deviceId, const std::wstring& zoneSetId, const std::vector<int>& zoneIndexSet);

        void SetActiveZoneSet(const std::wstring& deviceId, const ZoneSetData& zoneSet);

        void SerializeDeviceInfoToTmpFile(const DeviceInfoJSON& deviceInfo, std::wstring_view tmpFilePath) const;

        void ParseDeviceInfoFromTmpFile(std::wstring_view tmpFilePath);
        bool ParseCustomZoneSetFromTmpFile(std::wstring_view tmpFilePath);
        bool ParseDeletedCustomZoneSetsFromTmpFile(std::wstring_view tmpFilePath);

        bool ParseAppZoneHistory(const json::JsonObject& fancyZonesDataJSON);
        json::JsonArray SerializeAppZoneHistory() const;
        bool ParseDeviceInfos(const json::JsonObject& fancyZonesDataJSON);
        json::JsonArray SerializeDeviceInfos() const;
        bool ParseCustomZoneSets(const json::JsonObject& fancyZonesDataJSON);
        json::JsonArray SerializeCustomZoneSets() const;
        void CustomZoneSetsToJsonFile(std::wstring_view filePath) const;

        void LoadFancyZonesData();
        void SaveFancyZonesData() const;

    private:
        void MigrateCustomZoneSetsFromRegistry();
        void RemoveDesktopAppZoneHistory(const std::wstring& desktopId);
        bool HandleWindowRemovalFromZone(HWND window, AppZoneHistoryData& data);

        std::unordered_map<std::wstring, std::vector<AppZoneHistoryData>> appZoneHistoryMap{};
        std::unordered_map<std::wstring, DeviceInfoData> deviceInfoMap{};
        std::unordered_map<std::wstring, CustomZoneSetData> customZoneSetsMap{};

        std::wstring jsonFilePath;
        std::wstring appZoneHistoryFilePath;
    };

    FancyZonesData& FancyZonesDataInstance();
}
