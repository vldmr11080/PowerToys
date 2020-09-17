#include "pch.h"

#include "JsonHelpers.h"
#include "FancyZonesDataTypes.h"
#include "trace.h"
#include "util.h"

#include <filesystem>
#include <optional>
#include <utility>
#include <vector>

// Non-Localizable strings
namespace NonLocalizable
{
    const wchar_t ActiveZoneSetStr[] = L"active-zoneset";
    const wchar_t AppPathStr[] = L"app-path";
    const wchar_t AppZoneHistoryStr[] = L"app-zone-history";
    const wchar_t CanvasStr[] = L"canvas";
    const wchar_t CellChildMapStr[] = L"cell-child-map";
    const wchar_t ColumnsPercentageStr[] = L"columns-percentage";
    const wchar_t ColumnsStr[] = L"columns";
    const wchar_t CustomZoneSetsStr[] = L"custom-zone-sets";
    const wchar_t DeletedCustomZoneSetsStr[] = L"deleted-custom-zone-sets";
    const wchar_t DeviceIdStr[] = L"device-id";
    const wchar_t DevicesStr[] = L"devices";
    const wchar_t EditorShowSpacingStr[] = L"editor-show-spacing";
    const wchar_t EditorSpacingStr[] = L"editor-spacing";
    const wchar_t EditorZoneCountStr[] = L"editor-zone-count";
    const wchar_t GridStr[] = L"grid";
    const wchar_t HeightStr[] = L"height";
    const wchar_t HistoryStr[] = L"history";
    const wchar_t InfoStr[] = L"info";
    const wchar_t NameStr[] = L"name";
    const wchar_t RefHeightStr[] = L"ref-height";
    const wchar_t RefWidthStr[] = L"ref-width";
    const wchar_t RowsPercentageStr[] = L"rows-percentage";
    const wchar_t RowsStr[] = L"rows";
    const wchar_t ScreenHeightStr[] = L"screen-height";
    const wchar_t ScreenWidthStr[] = L"screen-width";
    const wchar_t TypeStr[] = L"type";
    const wchar_t UuidStr[] = L"uuid";
    const wchar_t WidthStr[] = L"width";
    const wchar_t XStr[] = L"X";
    const wchar_t YStr[] = L"Y";
    const wchar_t ZoneIndexSetStr[] = L"zone-index-set";
    const wchar_t ZoneIndexStr[] = L"zone-index";
    const wchar_t ZoneSetUuidStr[] = L"zoneset-uuid";
    const wchar_t ZonesStr[] = L"zones";
}

namespace
{
    json::JsonArray NumVecToJsonArray(const std::vector<int>& vec)
    {
        json::JsonArray arr;
        for (const auto& val : vec)
        {
            arr.Append(json::JsonValue::CreateNumberValue(val));
        }

        return arr;
    }

    std::vector<int> JsonArrayToNumVec(const json::JsonArray& arr)
    {
        std::vector<int> vec;
        for (const auto& val : arr)
        {
            vec.emplace_back(static_cast<int>(val.GetNumber()));
        }

        return vec;
    }

    std::optional<FancyZonesDataTypes::AppZoneHistoryData> ParseSingleAppZoneHistoryItem(const json::JsonObject& json)
    {
        FancyZonesDataTypes::AppZoneHistoryData data;
        if (json.HasKey(NonLocalizable::ZoneIndexSetStr))
        {
            data.zoneIndexSet = {};
            for (auto& value : json.GetNamedArray(NonLocalizable::ZoneIndexSetStr))
            {
                data.zoneIndexSet.push_back(static_cast<size_t>(value.GetNumber()));
            }
        }
        else if (json.HasKey(NonLocalizable::ZoneIndexStr))
        {
            data.zoneIndexSet = { static_cast<size_t>(json.GetNamedNumber(NonLocalizable::ZoneIndexStr)) };
        }

        data.deviceId = json.GetNamedString(NonLocalizable::DeviceIdStr);
        data.zoneSetUuid = json.GetNamedString(NonLocalizable::ZoneSetUuidStr);

        if (!FancyZonesUtils::IsValidGuid(data.zoneSetUuid) || !FancyZonesUtils::IsValidDeviceId(data.deviceId))
        {
            return std::nullopt;
        }

        return data;
    }

    inline bool DeleteTmpFile(std::wstring_view tmpFilePath)
    {
        return DeleteFileW(tmpFilePath.data());
    }
}

namespace JSONHelpers
{
    json::JsonObject CanvasLayoutInfoJSON::ToJson(const FancyZonesDataTypes::CanvasLayoutInfo& canvasInfo)
    {
        json::JsonObject infoJson{};
        infoJson.SetNamedValue(NonLocalizable::RefWidthStr, json::value(canvasInfo.workAreaWidth));
        infoJson.SetNamedValue(NonLocalizable::RefHeightStr, json::value(canvasInfo.workAreaHeight));

        if (canvasInfo.screenWidth.has_value() && canvasInfo.screenHeight.has_value())
        {
            infoJson.SetNamedValue(NonLocalizable::ScreenWidthStr, json::value(canvasInfo.screenWidth.value()));
            infoJson.SetNamedValue(NonLocalizable::ScreenHeightStr, json::value(canvasInfo.screenHeight.value()));
        }

        json::JsonArray zonesJson;

        for (const auto& [x, y, width, height] : canvasInfo.zones)
        {
            json::JsonObject zoneJson;
            zoneJson.SetNamedValue(NonLocalizable::XStr, json::value(x));
            zoneJson.SetNamedValue(NonLocalizable::YStr, json::value(y));
            zoneJson.SetNamedValue(NonLocalizable::WidthStr, json::value(width));
            zoneJson.SetNamedValue(NonLocalizable::HeightStr, json::value(height));
            zonesJson.Append(zoneJson);
        }
        infoJson.SetNamedValue(NonLocalizable::ZonesStr, zonesJson);
        return infoJson;
    }

    std::optional<FancyZonesDataTypes::CanvasLayoutInfo> CanvasLayoutInfoJSON::FromJson(const json::JsonObject& infoJson)
    {
        try
        {
            FancyZonesDataTypes::CanvasLayoutInfo info;
            info.workAreaWidth = static_cast<int>(infoJson.GetNamedNumber(NonLocalizable::RefWidthStr));
            info.workAreaHeight = static_cast<int>(infoJson.GetNamedNumber(NonLocalizable::RefHeightStr));

            if (infoJson.HasKey(NonLocalizable::ScreenWidthStr) &&
                infoJson.HasKey(NonLocalizable::ScreenHeightStr))
            {
                info.screenWidth = static_cast<int>(infoJson.GetNamedNumber(NonLocalizable::ScreenWidthStr));
                info.screenHeight = static_cast<int>(infoJson.GetNamedNumber(NonLocalizable::ScreenHeightStr));
            }

            json::JsonArray zonesJson = infoJson.GetNamedArray(NonLocalizable::ZonesStr);
            uint32_t size = zonesJson.Size();
            info.zones.reserve(size);
            for (uint32_t i = 0; i < size; ++i)
            {
                json::JsonObject zoneJson = zonesJson.GetObjectAt(i);
                const int x = static_cast<int>(zoneJson.GetNamedNumber(NonLocalizable::XStr));
                const int y = static_cast<int>(zoneJson.GetNamedNumber(NonLocalizable::YStr));
                const int width = static_cast<int>(zoneJson.GetNamedNumber(NonLocalizable::WidthStr));
                const int height = static_cast<int>(zoneJson.GetNamedNumber(NonLocalizable::HeightStr));
                FancyZonesDataTypes::CanvasLayoutInfo::Rect zone{ x, y, width, height };
                info.zones.push_back(zone);
            }
            return info;
        }
        catch (const winrt::hresult_error&)
        {
            return std::nullopt;
        }
    }

    json::JsonObject GridLayoutInfoJSON::ToJson(const FancyZonesDataTypes::GridLayoutInfo& gridInfo)
    {
        json::JsonObject infoJson;
        infoJson.SetNamedValue(NonLocalizable::RowsStr, json::value(gridInfo.m_rows));
        infoJson.SetNamedValue(NonLocalizable::ColumnsStr, json::value(gridInfo.m_columns));
        infoJson.SetNamedValue(NonLocalizable::RowsPercentageStr, NumVecToJsonArray(gridInfo.m_rowsPercents));
        infoJson.SetNamedValue(NonLocalizable::ColumnsPercentageStr, NumVecToJsonArray(gridInfo.m_columnsPercents));

        json::JsonArray cellChildMapJson;
        for (int i = 0; i < gridInfo.m_cellChildMap.size(); ++i)
        {
            cellChildMapJson.Append(NumVecToJsonArray(gridInfo.m_cellChildMap[i]));
        }
        infoJson.SetNamedValue(NonLocalizable::CellChildMapStr, cellChildMapJson);

        return infoJson;
    }

    std::optional<FancyZonesDataTypes::GridLayoutInfo> GridLayoutInfoJSON::FromJson(const json::JsonObject& infoJson)
    {
        try
        {
            FancyZonesDataTypes::GridLayoutInfo info(FancyZonesDataTypes::GridLayoutInfo::Minimal{});

            info.m_rows = static_cast<int>(infoJson.GetNamedNumber(NonLocalizable::RowsStr));
            info.m_columns = static_cast<int>(infoJson.GetNamedNumber(NonLocalizable::ColumnsStr));

            json::JsonArray rowsPercentage = infoJson.GetNamedArray(NonLocalizable::RowsPercentageStr);
            json::JsonArray columnsPercentage = infoJson.GetNamedArray(NonLocalizable::ColumnsPercentageStr);
            json::JsonArray cellChildMap = infoJson.GetNamedArray(NonLocalizable::CellChildMapStr);

            if (rowsPercentage.Size() != info.m_rows || columnsPercentage.Size() != info.m_columns || cellChildMap.Size() != info.m_rows)
            {
                return std::nullopt;
            }

            info.m_rowsPercents = JsonArrayToNumVec(rowsPercentage);
            info.m_columnsPercents = JsonArrayToNumVec(columnsPercentage);
            for (const auto& cellsRow : cellChildMap)
            {
                const auto cellsArray = cellsRow.GetArray();
                if (cellsArray.Size() != info.m_columns)
                {
                    return std::nullopt;
                }
                info.cellChildMap().push_back(JsonArrayToNumVec(cellsArray));
            }

            return info;
        }
        catch (const winrt::hresult_error&)
        {
            return std::nullopt;
        }
    }

    json::JsonObject CustomZoneSetJSON::ToJson(const CustomZoneSetJSON& customZoneSet)
    {
        json::JsonObject result{};

        result.SetNamedValue(NonLocalizable::UuidStr, json::value(customZoneSet.uuid));
        result.SetNamedValue(NonLocalizable::NameStr, json::value(customZoneSet.data.name));
        switch (customZoneSet.data.type)
        {
        case FancyZonesDataTypes::CustomLayoutType::Canvas:
        {
            result.SetNamedValue(NonLocalizable::TypeStr, json::value(NonLocalizable::CanvasStr));

            FancyZonesDataTypes::CanvasLayoutInfo info = std::get<FancyZonesDataTypes::CanvasLayoutInfo>(customZoneSet.data.info);
            result.SetNamedValue(NonLocalizable::InfoStr, CanvasLayoutInfoJSON::ToJson(info));

            break;
        }
        case FancyZonesDataTypes::CustomLayoutType::Grid:
        {
            result.SetNamedValue(NonLocalizable::TypeStr, json::value(NonLocalizable::GridStr));

            FancyZonesDataTypes::GridLayoutInfo gridInfo = std::get<FancyZonesDataTypes::GridLayoutInfo>(customZoneSet.data.info);
            result.SetNamedValue(NonLocalizable::InfoStr, GridLayoutInfoJSON::ToJson(gridInfo));

            break;
        }
        }

        return result;
    }

    std::optional<CustomZoneSetJSON> CustomZoneSetJSON::FromJson(const json::JsonObject& customZoneSet)
    {
        try
        {
            CustomZoneSetJSON result;

            result.uuid = customZoneSet.GetNamedString(NonLocalizable::UuidStr);
            if (!FancyZonesUtils::IsValidGuid(result.uuid))
            {
                return std::nullopt;
            }

            result.data.name = customZoneSet.GetNamedString(NonLocalizable::NameStr);

            json::JsonObject infoJson = customZoneSet.GetNamedObject(NonLocalizable::InfoStr);
            std::wstring zoneSetType = std::wstring{ customZoneSet.GetNamedString(NonLocalizable::TypeStr) };
            if (zoneSetType.compare(NonLocalizable::CanvasStr) == 0)
            {
                if (auto info = CanvasLayoutInfoJSON::FromJson(infoJson); info.has_value())
                {
                    result.data.type = FancyZonesDataTypes::CustomLayoutType::Canvas;
                    result.data.info = std::move(info.value());
                }
                else
                {
                    return std::nullopt;
                }
            }
            else if (zoneSetType.compare(NonLocalizable::GridStr) == 0)
            {
                if (auto info = GridLayoutInfoJSON::FromJson(infoJson); info.has_value())
                {
                    result.data.type = FancyZonesDataTypes::CustomLayoutType::Grid;
                    result.data.info = std::move(info.value());
                }
                else
                {
                    return std::nullopt;
                }
            }
            else
            {
                return std::nullopt;
            }

            return result;
        }
        catch (const winrt::hresult_error&)
        {
            return std::nullopt;
        }
    }

    json::JsonObject ZoneSetDataJSON::ToJson(const FancyZonesDataTypes::ZoneSetData& zoneSet)
    {
        json::JsonObject result{};

        result.SetNamedValue(NonLocalizable::UuidStr, json::value(zoneSet.uuid));
        result.SetNamedValue(NonLocalizable::TypeStr, json::value(TypeToString(zoneSet.type)));

        return result;
    }

    std::optional<FancyZonesDataTypes::ZoneSetData> ZoneSetDataJSON::FromJson(const json::JsonObject& zoneSet)
    {
        try
        {
            FancyZonesDataTypes::ZoneSetData zoneSetData;
            zoneSetData.uuid = zoneSet.GetNamedString(NonLocalizable::UuidStr);
            zoneSetData.type = FancyZonesDataTypes::TypeFromString(std::wstring{ zoneSet.GetNamedString(NonLocalizable::TypeStr) });

            if (!FancyZonesUtils::IsValidGuid(zoneSetData.uuid))
            {
                return std::nullopt;
            }

            return zoneSetData;
        }
        catch (const winrt::hresult_error&)
        {
            return std::nullopt;
        }
    }

    json::JsonObject AppZoneHistoryJSON::ToJson(const AppZoneHistoryJSON& appZoneHistory)
    {
        json::JsonObject result{};

        result.SetNamedValue(NonLocalizable::AppPathStr, json::value(appZoneHistory.appPath));

        json::JsonArray appHistoryArray;
        for (const auto& data : appZoneHistory.data)
        {
            json::JsonObject desktopData;
            json::JsonArray jsonIndexSet;
            for (size_t index : data.zoneIndexSet)
            {
                jsonIndexSet.Append(json::value(static_cast<int>(index)));
            }

            desktopData.SetNamedValue(NonLocalizable::ZoneIndexSetStr, jsonIndexSet);
            desktopData.SetNamedValue(NonLocalizable::DeviceIdStr, json::value(data.deviceId));
            desktopData.SetNamedValue(NonLocalizable::ZoneSetUuidStr, json::value(data.zoneSetUuid));

            appHistoryArray.Append(desktopData);
        }

        result.SetNamedValue(NonLocalizable::HistoryStr, appHistoryArray);

        return result;
    }

    std::optional<AppZoneHistoryJSON> AppZoneHistoryJSON::FromJson(const json::JsonObject& zoneSet)
    {
        try
        {
            AppZoneHistoryJSON result;

            result.appPath = zoneSet.GetNamedString(NonLocalizable::AppPathStr);
            if (zoneSet.HasKey(NonLocalizable::HistoryStr))
            {
                auto appHistoryArray = zoneSet.GetNamedArray(NonLocalizable::HistoryStr);
                for (uint32_t i = 0; i < appHistoryArray.Size(); ++i)
                {
                    json::JsonObject json = appHistoryArray.GetObjectAt(i);
                    if (auto data = ParseSingleAppZoneHistoryItem(json); data.has_value())
                    {
                        result.data.push_back(std::move(data.value()));
                    }
                }
            }
            else
            {
                // handle previous file format, with single desktop layout information per application
                if (auto data = ParseSingleAppZoneHistoryItem(zoneSet); data.has_value())
                {
                    result.data.push_back(std::move(data.value()));
                }
            }
            if (result.data.empty())
            {
                return std::nullopt;
            }

            return result;
        }
        catch (const winrt::hresult_error&)
        {
            return std::nullopt;
        }
    }

    json::JsonObject DeviceInfoJSON::ToJson(const DeviceInfoJSON& device)
    {
        json::JsonObject result{};

        result.SetNamedValue(NonLocalizable::DeviceIdStr, json::value(device.deviceId));
        result.SetNamedValue(NonLocalizable::ActiveZoneSetStr, JSONHelpers::ZoneSetDataJSON::ToJson(device.data.activeZoneSet));
        result.SetNamedValue(NonLocalizable::EditorShowSpacingStr, json::value(device.data.showSpacing));
        result.SetNamedValue(NonLocalizable::EditorSpacingStr, json::value(device.data.spacing));
        result.SetNamedValue(NonLocalizable::EditorZoneCountStr, json::value(device.data.zoneCount));

        return result;
    }

    std::optional<DeviceInfoJSON> DeviceInfoJSON::FromJson(const json::JsonObject& device)
    {
        try
        {
            DeviceInfoJSON result;

            result.deviceId = device.GetNamedString(NonLocalizable::DeviceIdStr);
            if (!FancyZonesUtils::IsValidDeviceId(result.deviceId))
            {
                return std::nullopt;
            }

            if (auto zoneSet = JSONHelpers::ZoneSetDataJSON::FromJson(device.GetNamedObject(NonLocalizable::ActiveZoneSetStr)); zoneSet.has_value())
            {
                result.data.activeZoneSet = std::move(zoneSet.value());
            }
            else
            {
                return std::nullopt;
            }

            result.data.showSpacing = device.GetNamedBoolean(NonLocalizable::EditorShowSpacingStr);
            result.data.spacing = static_cast<int>(device.GetNamedNumber(NonLocalizable::EditorSpacingStr));
            result.data.zoneCount = static_cast<int>(
                device.GetNamedNumber(NonLocalizable::EditorZoneCountStr));

            return result;
        }
        catch (const winrt::hresult_error&)
        {
            return std::nullopt;
        }
    }

    json::JsonObject GetPersistFancyZonesJSON(const std::wstring& zonesSettingsFileName, const std::wstring& appZoneHistoryFileName)
    {
        auto result = json::from_file(zonesSettingsFileName);
        if (result)
        {
            if (!result->HasKey(NonLocalizable::AppZoneHistoryStr))
            {
                auto appZoneHistory = json::from_file(appZoneHistoryFileName);
                if (appZoneHistory)
                {
                    result->SetNamedValue(NonLocalizable::AppZoneHistoryStr, appZoneHistory->GetNamedArray(NonLocalizable::AppZoneHistoryStr));
                }
                else
                {
                    result->SetNamedValue(NonLocalizable::AppZoneHistoryStr, json::JsonArray());
                }
            }
            return *result;
        }
        else
        {
            return json::JsonObject();
        }
    }

    void SaveFancyZonesData(const std::wstring& zonesSettingsFileName,
                            const std::wstring& appZoneHistoryFileName,
                            const TDeviceInfoMap& deviceInfoMap,
                            const TCustomZoneSetsMap& customZoneSetsMap,
                            const TAppZoneHistoryMap& appZoneHistoryMap)

    {
        json::JsonObject root{};
        json::JsonObject appZoneHistoryRoot{};

        appZoneHistoryRoot.SetNamedValue(NonLocalizable::AppZoneHistoryStr, JSONHelpers::SerializeAppZoneHistory(appZoneHistoryMap));
        root.SetNamedValue(NonLocalizable::DevicesStr, JSONHelpers::SerializeDeviceInfos(deviceInfoMap));
        root.SetNamedValue(NonLocalizable::CustomZoneSetsStr, JSONHelpers::SerializeCustomZoneSets(customZoneSetsMap));

        auto before = json::from_file(zonesSettingsFileName);
        if (!before.has_value() || before.value().Stringify() != root.Stringify())
        {
            Trace::FancyZones::DataChanged();
        }

        json::to_file(zonesSettingsFileName, root);
        json::to_file(appZoneHistoryFileName, appZoneHistoryRoot);
    }

    TAppZoneHistoryMap ParseAppZoneHistory(const json::JsonObject& fancyZonesDataJSON)
    {
        try
        {
            TAppZoneHistoryMap appZoneHistoryMap{};
            auto appLastZones = fancyZonesDataJSON.GetNamedArray(NonLocalizable::AppZoneHistoryStr);

            for (uint32_t i = 0; i < appLastZones.Size(); ++i)
            {
                json::JsonObject appLastZone = appLastZones.GetObjectAt(i);
                if (auto appZoneHistory = AppZoneHistoryJSON::FromJson(appLastZone); appZoneHistory.has_value())
                {
                    appZoneHistoryMap[appZoneHistory->appPath] = std::move(appZoneHistory->data);
                }
            }

            return std::move(appZoneHistoryMap);
        }
        catch (const winrt::hresult_error&)
        {
            return {};
        }
    }

    json::JsonArray SerializeAppZoneHistory(const TAppZoneHistoryMap& appZoneHistoryMap)
    {
        json::JsonArray appHistoryArray;

        for (const auto& [appPath, appZoneHistoryData] : appZoneHistoryMap)
        {
            appHistoryArray.Append(AppZoneHistoryJSON::ToJson(AppZoneHistoryJSON{ appPath, appZoneHistoryData }));
        }

        return appHistoryArray;
    }

    TDeviceInfoMap ParseDeviceInfos(const json::JsonObject& fancyZonesDataJSON)
    {
        try
        {
            TDeviceInfoMap deviceInfoMap{};
            auto devices = fancyZonesDataJSON.GetNamedArray(NonLocalizable::DevicesStr);

            for (uint32_t i = 0; i < devices.Size(); ++i)
            {
                if (auto device = DeviceInfoJSON::DeviceInfoJSON::FromJson(devices.GetObjectAt(i)); device.has_value())
                {
                    deviceInfoMap[device->deviceId] = std::move(device->data);
                }
            }

            return std::move(deviceInfoMap);
        }
        catch (const winrt::hresult_error&)
        {
            return {};
        }
    }

    json::JsonArray SerializeDeviceInfos(const TDeviceInfoMap& deviceInfoMap)
    {
        json::JsonArray DeviceInfosJSON{};

        for (const auto& [deviceID, deviceData] : deviceInfoMap)
        {
            if (deviceData.activeZoneSet.type != FancyZonesDataTypes::ZoneSetLayoutType::Blank)
            {
                DeviceInfosJSON.Append(DeviceInfoJSON::DeviceInfoJSON::ToJson(DeviceInfoJSON{ deviceID, deviceData }));
            }
        }

        return DeviceInfosJSON;
    }

    TCustomZoneSetsMap ParseCustomZoneSets(const json::JsonObject& fancyZonesDataJSON)
    {
        try
        {
            TCustomZoneSetsMap customZoneSetsMap{};
            auto customZoneSets = fancyZonesDataJSON.GetNamedArray(NonLocalizable::CustomZoneSetsStr);

            for (uint32_t i = 0; i < customZoneSets.Size(); ++i)
            {
                if (auto zoneSet = CustomZoneSetJSON::FromJson(customZoneSets.GetObjectAt(i)); zoneSet.has_value())
                {
                    customZoneSetsMap[zoneSet->uuid] = std::move(zoneSet->data);
                }
            }

            return std::move(customZoneSetsMap);
        }
        catch (const winrt::hresult_error&)
        {
            return {};
        }
    }

    json::JsonArray SerializeCustomZoneSets(const TCustomZoneSetsMap& customZoneSetsMap)
    {
        json::JsonArray customZoneSetsJSON{};

        for (const auto& [zoneSetId, zoneSetData] : customZoneSetsMap)
        {
            customZoneSetsJSON.Append(CustomZoneSetJSON::ToJson(CustomZoneSetJSON{ zoneSetId, zoneSetData }));
        }

        return customZoneSetsJSON;
    }

    void SerializeDeviceInfoToTmpFile(const JSONHelpers::DeviceInfoJSON& deviceInfo, std::wstring_view tmpFilePath)
    {
        json::JsonObject deviceInfoJson = JSONHelpers::DeviceInfoJSON::ToJson(deviceInfo);
        json::to_file(tmpFilePath, deviceInfoJson);
    }

    std::optional<DeviceInfoJSON> ParseDeviceInfoFromTmpFile(std::wstring_view tmpFilePath)
    {
        std::optional<DeviceInfoJSON> result{ std::nullopt };
        if (std::filesystem::exists(tmpFilePath))
        {
            if (auto zoneSetJson = json::from_file(tmpFilePath); zoneSetJson.has_value())
            {
                if (auto deviceInfo = JSONHelpers::DeviceInfoJSON::FromJson(zoneSetJson.value()); deviceInfo.has_value())
                {
                    result = std::move(deviceInfo);
                }
            }
        }

        DeleteTmpFile(tmpFilePath);
        return result;
    }

    std::optional<CustomZoneSetJSON> ParseCustomZoneSetFromTmpFile(std::wstring_view tmpFilePath)
    {
        std::optional<CustomZoneSetJSON> result{ std::nullopt };
        if (std::filesystem::exists(tmpFilePath))
        {
            try
            {
                if (auto customZoneSetJson = json::from_file(tmpFilePath); customZoneSetJson.has_value())
                {
                    if (auto customZoneSet = JSONHelpers::CustomZoneSetJSON::FromJson(customZoneSetJson.value()); customZoneSet.has_value())
                    {
                        result = std::move(customZoneSet);
                    }
                }
            }
            catch (const winrt::hresult_error&)
            {
                result = std::nullopt;
            }

            DeleteTmpFile(tmpFilePath);
        }
        return result;
    }

    std::vector<std::wstring> ParseDeletedCustomZoneSetsFromTmpFile(std::wstring_view tmpFilePath)
    {
        std::vector<std::wstring> result{};
        if (std::filesystem::exists(tmpFilePath))
        {
            auto deletedZoneSetsJson = json::from_file(tmpFilePath);
            try
            {
                auto deletedCustomZoneSets = deletedZoneSetsJson->GetNamedArray(NonLocalizable::DeletedCustomZoneSetsStr);
                for (auto zoneSet : deletedCustomZoneSets)
                {
                    std::wstring uuid = L"{" + std::wstring{ zoneSet.GetString() } + L"}";
                    result.push_back(uuid);
                }
            }
            catch (const winrt::hresult_error&)
            {
            }

            DeleteTmpFile(tmpFilePath);
        }

        return result;
    }
}
