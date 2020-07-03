#include "pch.h"

#include <common/common.h>

#include "ZoneWindow.h"
#include "trace.h"
#include "util.h"

#include <ShellScalingApi.h>
#include <mutex>

#include <gdiplus.h>

namespace ZoneWindowUtils
{
    const std::wstring& GetActiveZoneSetTmpPath()
    {
        static std::wstring activeZoneSetTmpFileName;
        static std::once_flag flag;

        std::call_once(flag, []() {
            wchar_t fileName[L_tmpnam_s];

            if (_wtmpnam_s(fileName, L_tmpnam_s) != 0)
                abort();

            activeZoneSetTmpFileName = std::wstring{ fileName };
        });

        return activeZoneSetTmpFileName;
    }

    const std::wstring& GetAppliedZoneSetTmpPath()
    {
        static std::wstring appliedZoneSetTmpFileName;
        static std::once_flag flag;

        std::call_once(flag, []() {
            wchar_t fileName[L_tmpnam_s];

            if (_wtmpnam_s(fileName, L_tmpnam_s) != 0)
                abort();

            appliedZoneSetTmpFileName = std::wstring{ fileName };
        });

        return appliedZoneSetTmpFileName;
    }

    const std::wstring& GetCustomZoneSetsTmpPath()
    {
        static std::wstring customZoneSetsTmpFileName;
        static std::once_flag flag;

        std::call_once(flag, []() {
            wchar_t fileName[L_tmpnam_s];

            if (_wtmpnam_s(fileName, L_tmpnam_s) != 0)
                abort();

            customZoneSetsTmpFileName = std::wstring{ fileName };
        });

        return customZoneSetsTmpFileName;
    }

    std::wstring GenerateUniqueId(HMONITOR monitor, PCWSTR deviceId, PCWSTR virtualDesktopId)
    {
        wchar_t uniqueId[256]{}; // Parsed deviceId + resolution + virtualDesktopId

        MONITORINFOEXW mi;
        mi.cbSize = sizeof(mi);
        if (virtualDesktopId && GetMonitorInfo(monitor, &mi))
        {
            wchar_t parsedId[256]{};
            ParseDeviceId(deviceId, parsedId, 256);

            Rect const monitorRect(mi.rcMonitor);
            StringCchPrintf(uniqueId, ARRAYSIZE(uniqueId), L"%s_%d_%d_%s", parsedId, monitorRect.width(), monitorRect.height(), virtualDesktopId);
        }
        return std::wstring{ uniqueId };
    }
}

namespace ZoneWindowDrawUtils
{
    struct ColorSetting
    {
        BYTE fillAlpha{};
        COLORREF fill{};
        BYTE borderAlpha{};
        COLORREF border{};
        int thickness{};
    };

    void DrawBackdrop(wil::unique_hdc& hdc, RECT const& clientRect) noexcept
    {
        FillRectARGB(hdc, &clientRect, 0, RGB(0, 0, 0), false);
    }

    void DrawIndex(wil::unique_hdc& hdc, Rect rect, size_t index)
    {
        Gdiplus::Graphics g(hdc.get());

        Gdiplus::FontFamily fontFamily(L"Segoe ui");
        Gdiplus::Font font(&fontFamily, 80, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush solidBrush(Gdiplus::Color(255, 0, 0, 0));

        std::wstring text = std::to_wstring(index);

        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
        Gdiplus::StringFormat stringFormat = new Gdiplus::StringFormat();
        stringFormat.SetAlignment(Gdiplus::StringAlignmentCenter);
        stringFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);

        Gdiplus::RectF gdiRect(static_cast<Gdiplus::REAL>(rect.left()),
                               static_cast<Gdiplus::REAL>(rect.top()),
                               static_cast<Gdiplus::REAL>(rect.width()),
                               static_cast<Gdiplus::REAL>(rect.height()));

        g.DrawString(text.c_str(), -1, &font, gdiRect, &stringFormat, &solidBrush);
    }

    void DrawZone(wil::unique_hdc& hdc, ColorSetting const& colorSetting, winrt::com_ptr<IZone> zone, const std::vector<winrt::com_ptr<IZone>>& zones, bool flashMode) noexcept
    {
        RECT zoneRect = zone->GetZoneRect();

        Gdiplus::Graphics g(hdc.get());
        Gdiplus::Color fillColor(colorSetting.fillAlpha, GetRValue(colorSetting.fill), GetGValue(colorSetting.fill), GetBValue(colorSetting.fill));
        Gdiplus::Color borderColor(colorSetting.borderAlpha, GetRValue(colorSetting.border), GetGValue(colorSetting.border), GetBValue(colorSetting.border));

        Gdiplus::Rect rectangle(zoneRect.left, zoneRect.top, zoneRect.right - zoneRect.left - 1, zoneRect.bottom - zoneRect.top - 1);

        Gdiplus::Pen pen(borderColor, static_cast<Gdiplus::REAL>(colorSetting.thickness));
        g.FillRectangle(new Gdiplus::SolidBrush(fillColor), rectangle);
        g.DrawRectangle(&pen, rectangle);

        if (!flashMode)
        {
            DrawIndex(hdc, zoneRect, zone->Id());
        }
    }

    void DrawActiveZoneSet(wil::unique_hdc& hdc,
                           COLORREF zoneColor,
                           COLORREF zoneBorderColor,
                           COLORREF highlightColor,
                           int zoneOpacity,
                           const std::vector<winrt::com_ptr<IZone>>& zones,
                           const std::vector<int>& highlightZones,
                           bool flashMode,
                           bool drawHints) noexcept
    {
        //                                 { fillAlpha, fill, borderAlpha, border, thickness }
        ColorSetting const colorHints{ OpacitySettingToAlpha(zoneOpacity), RGB(81, 92, 107), 255, RGB(104, 118, 138), -2 };
        ColorSetting colorViewer{ OpacitySettingToAlpha(zoneOpacity), 0, 255, RGB(40, 50, 60), -2 };
        ColorSetting colorHighlight{ OpacitySettingToAlpha(zoneOpacity), 0, 255, 0, -2 };
        ColorSetting const colorFlash{ OpacitySettingToAlpha(zoneOpacity), RGB(81, 92, 107), 200, RGB(104, 118, 138), -2 };

        std::vector<bool> isHighlighted(zones.size(), false);
        for (int x : highlightZones)
        {
            isHighlighted[x] = true;
        }

        for (auto iter = zones.begin(); iter != zones.end(); iter++)
        {
            int zoneId = static_cast<int>(iter - zones.begin());
            winrt::com_ptr<IZone> zone = iter->try_as<IZone>();
            if (!zone)
            {
                continue;
            }

            if (!isHighlighted[zoneId])
            {
                if (flashMode)
                {
                    DrawZone(hdc, colorFlash, zone, zones, flashMode);
                }
                else if (drawHints)
                {
                    DrawZone(hdc, colorHints, zone, zones, flashMode);
                }
                {
                    colorViewer.fill = zoneColor;
                    colorViewer.border = zoneBorderColor;
                    DrawZone(hdc, colorViewer, zone, zones, flashMode);
                }
            }
            else
            {
                colorHighlight.fill = highlightColor;
                colorHighlight.border = zoneBorderColor;
                DrawZone(hdc, colorHighlight, zone, zones, flashMode);
            }
        }
    }
}

struct ZoneWindow : public winrt::implements<ZoneWindow, IZoneWindow>
{
public:
    ZoneWindow(HINSTANCE hinstance);
    ~ZoneWindow();

    bool Init(IZoneWindowHost* host, HINSTANCE hinstance, HMONITOR monitor, const std::wstring& uniqueId, const std::wstring& parentUniqueId, bool flashZones);

    IFACEMETHODIMP MoveSizeEnter(HWND window) noexcept;
    IFACEMETHODIMP MoveSizeUpdate(POINT const& ptScreen, bool dragEnabled) noexcept;
    IFACEMETHODIMP MoveSizeEnd(HWND window, POINT const& ptScreen) noexcept;
    IFACEMETHODIMP_(void)
    RestoreOriginalTransparency() noexcept;
    IFACEMETHODIMP_(void)
    MoveWindowIntoZoneByIndex(HWND window, int index) noexcept;
    IFACEMETHODIMP_(void)
    MoveWindowIntoZoneByIndexSet(HWND window, const std::vector<int>& indexSet) noexcept;
    IFACEMETHODIMP_(bool)
    MoveWindowIntoZoneByDirection(HWND window, DWORD vkCode, bool cycle) noexcept;
    IFACEMETHODIMP_(void)
    CycleActiveZoneSet(DWORD vkCode) noexcept;
    IFACEMETHODIMP_(std::wstring)
    UniqueId() noexcept { return { m_uniqueId }; }
    IFACEMETHODIMP_(std::wstring)
    WorkAreaKey() noexcept { return { m_workArea }; }
    IFACEMETHODIMP_(void)
    SaveWindowProcessToZoneIndex(HWND window) noexcept;
    IFACEMETHODIMP_(IZoneSet*)
    ActiveZoneSet() noexcept { return m_activeZoneSet.get(); }
    IFACEMETHODIMP_(void)
    ShowZoneWindow() noexcept;
    IFACEMETHODIMP_(void)
    HideZoneWindow() noexcept;
    IFACEMETHODIMP_(void)
    UpdateActiveZoneSet() noexcept;

protected:
    static LRESULT CALLBACK s_WndProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept;

private:
    void InitializeZoneSets(const std::wstring& parentUniqueId) noexcept;
    void CalculateZoneSet() noexcept;
    void UpdateActiveZoneSet(_In_opt_ IZoneSet* zoneSet) noexcept;
    LRESULT WndProc(UINT message, WPARAM wparam, LPARAM lparam) noexcept;
    void OnPaint(wil::unique_hdc& hdc) noexcept;
    void OnKeyUp(WPARAM wparam) noexcept;
    std::vector<int> ZonesFromPoint(POINT pt) noexcept;
    void CycleActiveZoneSetInternal(DWORD wparam, Trace::ZoneWindow::InputMode mode) noexcept;
    void FlashZones() noexcept;

    winrt::com_ptr<IZoneWindowHost> m_host;
    HMONITOR m_monitor{};
    std::wstring m_uniqueId; // Parsed deviceId + resolution + virtualDesktopId
    wchar_t m_workArea[256]{};
    wil::unique_hwnd m_window{}; // Hidden tool window used to represent current monitor desktop work area.
    HWND m_windowMoveSize{};
    bool m_drawHints{};
    bool m_flashMode{};
    winrt::com_ptr<IZoneSet> m_activeZoneSet;
    std::vector<winrt::com_ptr<IZoneSet>> m_zoneSets;
    std::vector<int> m_highlightZone;
    WPARAM m_keyLast{};
    size_t m_keyCycle{};
    static const UINT m_showAnimationDuration = 200; // ms
    static const UINT m_flashDuration = 700; // ms

    HWND draggedWindow = nullptr;
    long draggedWindowExstyle = 0;
    COLORREF draggedWindowCrKey = RGB(0, 0, 0);
    DWORD draggedWindowDwFlags = 0;
    BYTE draggedWindowInitialAlpha = 0;

    ULONG_PTR gdiplusToken;
};

ZoneWindow::ZoneWindow(HINSTANCE hinstance)
{
    WNDCLASSEXW wcex{};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.lpfnWndProc = s_WndProc;
    wcex.hInstance = hinstance;
    wcex.lpszClassName = L"SuperFancyZones_ZoneWindow";
    wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wcex);

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
}

ZoneWindow::~ZoneWindow()
{
    Gdiplus::GdiplusShutdown(gdiplusToken);
}

bool ZoneWindow::Init(IZoneWindowHost* host, HINSTANCE hinstance, HMONITOR monitor, const std::wstring& uniqueId, const std::wstring& parentUniqueId, bool flashZones)
{
    m_host.copy_from(host);

    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(monitor, &mi))
    {
        return false;
    }

    m_monitor = monitor;
    const UINT dpi = GetDpiForMonitor(m_monitor);
    const Rect monitorRect(mi.rcMonitor);
    const Rect workAreaRect(mi.rcWork, dpi);
    StringCchPrintf(m_workArea, ARRAYSIZE(m_workArea), L"%d_%d", monitorRect.width(), monitorRect.height());

    m_uniqueId = uniqueId;
    InitializeZoneSets(parentUniqueId);

    m_window = wil::unique_hwnd{
        CreateWindowExW(WS_EX_TOOLWINDOW, L"SuperFancyZones_ZoneWindow", L"", WS_POPUP, workAreaRect.left(), workAreaRect.top(), workAreaRect.width(), workAreaRect.height(), nullptr, nullptr, hinstance, this)
    };

    if (!m_window)
    {
        return false;
    }

    MakeWindowTransparent(m_window.get());
    if (flashZones)
    {
        // Don't flash if the foreground window is in full screen mode
        RECT windowRect;
        if (!(GetWindowRect(GetForegroundWindow(), &windowRect) &&
              windowRect.left == mi.rcMonitor.left &&
              windowRect.top == mi.rcMonitor.top &&
              windowRect.right == mi.rcMonitor.right &&
              windowRect.bottom == mi.rcMonitor.bottom))
        {
            FlashZones();
        }
    }

    return true;
}

IFACEMETHODIMP ZoneWindow::MoveSizeEnter(HWND window) noexcept
{
    if (m_windowMoveSize)
    {
        return E_INVALIDARG;
    }

    if (m_host->isMakeDraggedWindowTransparentActive())
    {
        draggedWindowExstyle = GetWindowLong(window, GWL_EXSTYLE);

        draggedWindow = window;
        SetWindowLong(window,
                      GWL_EXSTYLE,
                      draggedWindowExstyle | WS_EX_LAYERED);

        GetLayeredWindowAttributes(window, &draggedWindowCrKey, &draggedWindowInitialAlpha, &draggedWindowDwFlags);

        SetLayeredWindowAttributes(window, 0, (255 * 50) / 100, LWA_ALPHA);
    }

    m_windowMoveSize = window;
    m_drawHints = true;
    m_highlightZone = {};
    ShowZoneWindow();
    return S_OK;
}

IFACEMETHODIMP ZoneWindow::MoveSizeUpdate(POINT const& ptScreen, bool dragEnabled) noexcept
{
    bool redraw = false;
    POINT ptClient = ptScreen;
    MapWindowPoints(nullptr, m_window.get(), &ptClient, 1);

    if (dragEnabled)
    {
        auto highlightZone = ZonesFromPoint(ptClient);
        redraw = (highlightZone != m_highlightZone);
        m_highlightZone = std::move(highlightZone);
    }
    else if (m_highlightZone.size())
    {
        m_highlightZone = {};
        redraw = true;
    }

    if (redraw)
    {
        InvalidateRect(m_window.get(), nullptr, true);
    }
    return S_OK;
}

IFACEMETHODIMP ZoneWindow::MoveSizeEnd(HWND window, POINT const& ptScreen) noexcept
{
    RestoreOriginalTransparency();

    if (m_windowMoveSize != window)
    {
        return E_INVALIDARG;
    }

    if (m_activeZoneSet)
    {
        POINT ptClient = ptScreen;
        MapWindowPoints(nullptr, m_window.get(), &ptClient, 1);
        m_activeZoneSet->MoveWindowIntoZoneByPoint(window, m_window.get(), ptClient);

        SaveWindowProcessToZoneIndex(window);
    }
    Trace::ZoneWindow::MoveSizeEnd(m_activeZoneSet);

    HideZoneWindow();
    m_windowMoveSize = nullptr;
    return S_OK;
}

IFACEMETHODIMP_(void)
ZoneWindow::RestoreOriginalTransparency() noexcept
{
    if (m_host->isMakeDraggedWindowTransparentActive() && draggedWindow != nullptr)
    {
        SetLayeredWindowAttributes(draggedWindow, draggedWindowCrKey, draggedWindowInitialAlpha, draggedWindowDwFlags);
        SetWindowLong(draggedWindow, GWL_EXSTYLE, draggedWindowExstyle);
        draggedWindow = nullptr;
    }
}

IFACEMETHODIMP_(void)
ZoneWindow::MoveWindowIntoZoneByIndex(HWND window, int index) noexcept
{
    MoveWindowIntoZoneByIndexSet(window, { index });
}

IFACEMETHODIMP_(void)
ZoneWindow::MoveWindowIntoZoneByIndexSet(HWND window, const std::vector<int>& indexSet) noexcept
{
    if (m_activeZoneSet)
    {
        m_activeZoneSet->MoveWindowIntoZoneByIndexSet(window, m_window.get(), indexSet);
    }
}

IFACEMETHODIMP_(bool)
ZoneWindow::MoveWindowIntoZoneByDirection(HWND window, DWORD vkCode, bool cycle) noexcept
{
    if (m_activeZoneSet)
    {
        if (m_activeZoneSet->MoveWindowIntoZoneByDirection(window, m_window.get(), vkCode, cycle))
        {
            SaveWindowProcessToZoneIndex(window);
            return true;
        }
    }
    return false;
}

IFACEMETHODIMP_(void)
ZoneWindow::CycleActiveZoneSet(DWORD wparam) noexcept
{
    CycleActiveZoneSetInternal(wparam, Trace::ZoneWindow::InputMode::Keyboard);

    if (m_windowMoveSize)
    {
        InvalidateRect(m_window.get(), nullptr, true);
    }
    else
    {
        FlashZones();
    }
}

IFACEMETHODIMP_(void)
ZoneWindow::SaveWindowProcessToZoneIndex(HWND window) noexcept
{
    if (m_activeZoneSet)
    {
        auto zoneIndexSet = m_activeZoneSet->GetZoneIndexSetFromWindow(window);
        if (zoneIndexSet.size())
        {
            OLECHAR* guidString;
            if (StringFromCLSID(m_activeZoneSet->Id(), &guidString) == S_OK)
            {
                JSONHelpers::FancyZonesDataInstance().SetAppLastZones(window, m_uniqueId, guidString, zoneIndexSet);
            }

            CoTaskMemFree(guidString);
        }
    }
}

IFACEMETHODIMP_(void)
ZoneWindow::ShowZoneWindow() noexcept
{
    auto window = m_window.get();
    if (!window)
    {
        return;
    }

    m_flashMode = false;

    UINT flags = SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE;

    HWND windowInsertAfter = m_windowMoveSize;
    if (windowInsertAfter == nullptr)
    {
        windowInsertAfter = HWND_TOPMOST;
    }

    SetWindowPos(window, windowInsertAfter, 0, 0, 0, 0, flags);

    std::thread{ [=]() {
        AnimateWindow(window, m_showAnimationDuration, AW_BLEND);
        InvalidateRect(window, nullptr, true);
        if (!m_host->InMoveSize())
        {
            HideZoneWindow();
        }
    } }.detach();
}

IFACEMETHODIMP_(void)
ZoneWindow::HideZoneWindow() noexcept
{
    if (m_window)
    {
        ShowWindow(m_window.get(), SW_HIDE);
        m_keyLast = 0;
        m_windowMoveSize = nullptr;
        m_drawHints = false;
        m_highlightZone = {};
    }
}

IFACEMETHODIMP_(void)
ZoneWindow::UpdateActiveZoneSet() noexcept
{
    CalculateZoneSet();
}

#pragma region private

void ZoneWindow::InitializeZoneSets(const std::wstring& parentUniqueId) noexcept
{
    // If there is not defined zone layout for this work area, created default entry.
    JSONHelpers::FancyZonesDataInstance().AddDevice(m_uniqueId);
    if (!parentUniqueId.empty())
    {
        JSONHelpers::FancyZonesDataInstance().CloneDeviceInfo(parentUniqueId, m_uniqueId);
    }
    CalculateZoneSet();
}

void ZoneWindow::CalculateZoneSet() noexcept
{
    const auto& fancyZonesData = JSONHelpers::FancyZonesDataInstance();
    const auto deviceInfoData = fancyZonesData.FindDeviceInfo(m_uniqueId);

    if (!deviceInfoData.has_value())
    {
        return;
    }

    const auto& activeZoneSet = deviceInfoData->activeZoneSet;

    if (activeZoneSet.uuid.empty() || activeZoneSet.type == JSONHelpers::ZoneSetLayoutType::Blank)
    {
        return;
    }

    GUID zoneSetId;
    if (SUCCEEDED_LOG(CLSIDFromString(activeZoneSet.uuid.c_str(), &zoneSetId)))
    {
        auto zoneSet = MakeZoneSet(ZoneSetConfig(
            zoneSetId,
            activeZoneSet.type,
            m_monitor,
            m_workArea));
        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        if (GetMonitorInfoW(m_monitor, &monitorInfo))
        {
            bool showSpacing = deviceInfoData->showSpacing;
            int spacing = showSpacing ? deviceInfoData->spacing : 0;
            int zoneCount = deviceInfoData->zoneCount;
            zoneSet->CalculateZones(monitorInfo, zoneCount, spacing);
            UpdateActiveZoneSet(zoneSet.get());
        }
    }
}

void ZoneWindow::UpdateActiveZoneSet(_In_opt_ IZoneSet* zoneSet) noexcept
{
    m_activeZoneSet.copy_from(zoneSet);

    if (m_activeZoneSet)
    {
        wil::unique_cotaskmem_string zoneSetId;
        if (SUCCEEDED_LOG(StringFromCLSID(m_activeZoneSet->Id(), &zoneSetId)))
        {
            JSONHelpers::ZoneSetData data{
                .uuid = zoneSetId.get(),
                .type = m_activeZoneSet->LayoutType()
            };
            JSONHelpers::FancyZonesDataInstance().SetActiveZoneSet(m_uniqueId, data);
        }
    }
}

LRESULT ZoneWindow::WndProc(UINT message, WPARAM wparam, LPARAM lparam) noexcept
{
    switch (message)
    {
    case WM_NCDESTROY:
    {
        ::DefWindowProc(m_window.get(), message, wparam, lparam);
        SetWindowLongPtr(m_window.get(), GWLP_USERDATA, 0);
    }
    break;

    case WM_ERASEBKGND:
        return 1;

    case WM_PRINTCLIENT:
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        wil::unique_hdc hdc{ reinterpret_cast<HDC>(wparam) };
        if (!hdc)
        {
            hdc.reset(BeginPaint(m_window.get(), &ps));
        }

        OnPaint(hdc);

        if (wparam == 0)
        {
            EndPaint(m_window.get(), &ps);
        }

        hdc.release();
    }
    break;

    default:
    {
        return DefWindowProc(m_window.get(), message, wparam, lparam);
    }
    }
    return 0;
}

void ZoneWindow::OnPaint(wil::unique_hdc& hdc) noexcept
{
    RECT clientRect;
    GetClientRect(m_window.get(), &clientRect);

    wil::unique_hdc hdcMem;
    HPAINTBUFFER bufferedPaint = BeginBufferedPaint(hdc.get(), &clientRect, BPBF_TOPDOWNDIB, nullptr, &hdcMem);
    if (bufferedPaint)
    {
        ZoneWindowDrawUtils::DrawBackdrop(hdcMem, clientRect);

        if (m_activeZoneSet && m_host)
        {
            ZoneWindowDrawUtils::DrawActiveZoneSet(hdcMem,
                                                   m_host->GetZoneColor(),
                                                   m_host->GetZoneBorderColor(),
                                                   m_host->GetZoneHighlightColor(),
                                                   m_host->GetZoneHighlightOpacity(),
                                                   m_activeZoneSet->GetZones(),
                                                   m_highlightZone,
                                                   m_flashMode,
                                                   m_drawHints);
        }

        EndBufferedPaint(bufferedPaint, TRUE);
    }
}

void ZoneWindow::OnKeyUp(WPARAM wparam) noexcept
{
    bool fRedraw = false;
    Trace::ZoneWindow::KeyUp(wparam);

    if ((wparam >= '0') && (wparam <= '9'))
    {
        CycleActiveZoneSetInternal(static_cast<DWORD>(wparam), Trace::ZoneWindow::InputMode::Keyboard);
        InvalidateRect(m_window.get(), nullptr, true);
    }
}

std::vector<int> ZoneWindow::ZonesFromPoint(POINT pt) noexcept
{
    if (m_activeZoneSet)
    {
        return m_activeZoneSet->ZonesFromPoint(pt);
    }
    return {};
}

void ZoneWindow::CycleActiveZoneSetInternal(DWORD wparam, Trace::ZoneWindow::InputMode mode) noexcept
{
    Trace::ZoneWindow::CycleActiveZoneSet(m_activeZoneSet, mode);
    if (m_keyLast != wparam)
    {
        m_keyCycle = 0;
    }

    m_keyLast = wparam;

    bool loopAround = true;
    size_t const val = static_cast<size_t>(wparam - L'0');
    size_t i = 0;
    for (auto zoneSet : m_zoneSets)
    {
        if (zoneSet->GetZones().size() == val)
        {
            if (i < m_keyCycle)
            {
                i++;
            }
            else
            {
                UpdateActiveZoneSet(zoneSet.get());
                loopAround = false;
                break;
            }
        }
    }

    if ((m_keyCycle > 0) && loopAround)
    {
        // Cycling through a non-empty group and hit the end
        m_keyCycle = 0;
        OnKeyUp(wparam);
    }
    else
    {
        m_keyCycle++;
    }

    if (m_host)
    {
        m_host->MoveWindowsOnActiveZoneSetChange();
    }
    m_highlightZone = {};
}

void ZoneWindow::FlashZones() noexcept
{
    // "Turning FLASHING_ZONE option off"
    if (true)
    {
        return;
    }

    m_flashMode = true;

    ShowWindow(m_window.get(), SW_SHOWNA);
    std::thread([window = m_window.get()]() {
        AnimateWindow(window, m_flashDuration, AW_HIDE | AW_BLEND);
    }).detach();
}
#pragma endregion

LRESULT CALLBACK ZoneWindow::s_WndProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept
{
    auto thisRef = reinterpret_cast<ZoneWindow*>(GetWindowLongPtr(window, GWLP_USERDATA));
    if ((thisRef == nullptr) && (message == WM_CREATE))
    {
        auto createStruct = reinterpret_cast<LPCREATESTRUCT>(lparam);
        thisRef = reinterpret_cast<ZoneWindow*>(createStruct->lpCreateParams);
        SetWindowLongPtr(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(thisRef));
    }

    return (thisRef != nullptr) ? thisRef->WndProc(message, wparam, lparam) :
                                  DefWindowProc(window, message, wparam, lparam);
}

winrt::com_ptr<IZoneWindow> MakeZoneWindow(IZoneWindowHost* host, HINSTANCE hinstance, HMONITOR monitor, const std::wstring& uniqueId, const std::wstring& parentUniqueId, bool flashZones) noexcept
{
    auto self = winrt::make_self<ZoneWindow>(hinstance);
    if (self->Init(host, hinstance, monitor, uniqueId, parentUniqueId, flashZones))
    {
        return self;
    }

    return nullptr;
}
