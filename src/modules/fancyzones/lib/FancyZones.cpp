#include "pch.h"

#include <common/common.h>
#include <common/dpi_aware.h>
#include <common/on_thread_executor.h>
#include <common/window_helpers.h>

#include "FancyZones.h"
#include "lib/Settings.h"
#include "lib/ZoneWindow.h"
#include "lib/FancyZonesData.h"
#include "lib/ZoneSet.h"
#include "lib/WindowMoveHandler.h"
#include "lib/FancyZonesWinHookEventIDs.h"
#include "lib/util.h"
#include "trace.h"
#include "VirtualDesktopUtils.h"
#include "MonitorWorkAreaHandler.h"

#include <lib/SecondaryMouseButtonsHook.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

enum class DisplayChangeType
{
    WorkArea,
    DisplayChange,
    VirtualDesktop,
    Initialization
};

namespace
{
    constexpr int CUSTOM_POSITIONING_LEFT_TOP_PADDING = 16;
}

// Non-localizable strings
namespace NonLocalizable
{
    const wchar_t ToolWindowClassName[] = L"SuperFancyZones";
    const wchar_t FZEditorExecutablePath[] = L"modules\\FancyZones\\FancyZonesEditor.exe";
    const wchar_t SplashClassName[] = L"MsoSplash";
}

struct FancyZones : public winrt::implements<FancyZones, IFancyZones, IFancyZonesCallback, IZoneWindowHost>
{
public:
    FancyZones(HINSTANCE hinstance, const winrt::com_ptr<IFancyZonesSettings>& settings, std::function<void()> disableModuleCallback) noexcept :
        m_hinstance(hinstance),
        m_settings(settings),
        m_windowMoveHandler(settings, [this]() {
            PostMessageW(m_window, WM_PRIV_LOCATIONCHANGE, NULL, NULL);
        })
    {
        m_settings->SetCallback(this);

        this->disableModuleCallback = std::move(disableModuleCallback);
    }

    // IFancyZones
    IFACEMETHODIMP_(void)
    Run() noexcept;
    IFACEMETHODIMP_(void)
    Destroy() noexcept;

    void MoveSizeStart(HWND window, HMONITOR monitor, POINT const& ptScreen) noexcept
    {
        std::unique_lock writeLock(m_lock);
        if (m_settings->GetSettings()->spanZonesAcrossMonitors)
        {
            monitor = NULL;
        }
        m_windowMoveHandler.MoveSizeStart(window, monitor, ptScreen, m_workAreaHandler.GetWorkAreasByDesktopId(m_currentDesktopId));
    }

    void MoveSizeUpdate(HMONITOR monitor, POINT const& ptScreen) noexcept
    {
        std::unique_lock writeLock(m_lock);
        if (m_settings->GetSettings()->spanZonesAcrossMonitors)
        {
            monitor = NULL;
        }
        m_windowMoveHandler.MoveSizeUpdate(monitor, ptScreen, m_workAreaHandler.GetWorkAreasByDesktopId(m_currentDesktopId));
    }

    void MoveSizeEnd(HWND window, POINT const& ptScreen) noexcept
    {
        std::unique_lock writeLock(m_lock);
        m_windowMoveHandler.MoveSizeEnd(window, ptScreen, m_workAreaHandler.GetWorkAreasByDesktopId(m_currentDesktopId));
    }

    IFACEMETHODIMP_(void)
    HandleWinHookEvent(const WinHookEvent* data) noexcept
    {
        const auto wparam = reinterpret_cast<WPARAM>(data->hwnd);
        const LONG lparam = 0;
        std::shared_lock readLock(m_lock);
        switch (data->event)
        {
        case EVENT_SYSTEM_MOVESIZESTART:
            PostMessageW(m_window, WM_PRIV_MOVESIZESTART, wparam, lparam);
            break;
        case EVENT_SYSTEM_MOVESIZEEND:
            PostMessageW(m_window, WM_PRIV_MOVESIZEEND, wparam, lparam);
            break;
        case EVENT_OBJECT_LOCATIONCHANGE:
            PostMessageW(m_window, WM_PRIV_LOCATIONCHANGE, wparam, lparam);
            break;
        case EVENT_OBJECT_NAMECHANGE:
            PostMessageW(m_window, WM_PRIV_NAMECHANGE, wparam, lparam);
            break;

        case EVENT_OBJECT_UNCLOAKED:
        case EVENT_OBJECT_SHOW:
        case EVENT_OBJECT_CREATE:
            if (data->idObject == OBJID_WINDOW)
            {
                PostMessageW(m_window, WM_PRIV_WINDOWCREATED, wparam, lparam);
            }
            break;
        }
    }

    IFACEMETHODIMP_(void)
    VirtualDesktopChanged() noexcept;
    IFACEMETHODIMP_(void)
    VirtualDesktopInitialize() noexcept;
    IFACEMETHODIMP_(bool)
    OnKeyDown(PKBDLLHOOKSTRUCT info) noexcept;
    IFACEMETHODIMP_(void)
    ToggleEditor() noexcept;
    IFACEMETHODIMP_(void)
    SettingsChanged() noexcept;

    void WindowCreated(HWND window) noexcept;

    // IZoneWindowHost
    IFACEMETHODIMP_(void)
    MoveWindowsOnActiveZoneSetChange() noexcept;
    IFACEMETHODIMP_(COLORREF)
    GetZoneColor() noexcept
    {
        return (FancyZonesUtils::HexToRGB(m_settings->GetSettings()->zoneColor));
    }
    IFACEMETHODIMP_(COLORREF)
    GetZoneBorderColor() noexcept
    {
        return (FancyZonesUtils::HexToRGB(m_settings->GetSettings()->zoneBorderColor));
    }
    IFACEMETHODIMP_(COLORREF)
    GetZoneHighlightColor() noexcept
    {
        return (FancyZonesUtils::HexToRGB(m_settings->GetSettings()->zoneHighlightColor));
    }
    IFACEMETHODIMP_(int)
    GetZoneHighlightOpacity() noexcept
    {
        return m_settings->GetSettings()->zoneHighlightOpacity;
    }

    IFACEMETHODIMP_(bool)
    isMakeDraggedWindowTransparentActive() noexcept
    {
        return m_settings->GetSettings()->makeDraggedWindowTransparent;
    }

    IFACEMETHODIMP_(bool)
    InMoveSize() noexcept
    {
        std::shared_lock readLock(m_lock);
        return m_windowMoveHandler.InMoveSize();
    }

    LRESULT WndProc(HWND, UINT, WPARAM, LPARAM) noexcept;
    void OnDisplayChange(DisplayChangeType changeType) noexcept;
    void AddZoneWindow(HMONITOR monitor, PCWSTR deviceId) noexcept;

protected:
    static LRESULT CALLBACK s_WndProc(HWND, UINT, WPARAM, LPARAM) noexcept;

private:
    struct require_read_lock
    {
        template<typename T>
        require_read_lock(const std::shared_lock<T>& lock)
        {
            lock;
        }

        template<typename T>
        require_read_lock(const std::unique_lock<T>& lock)
        {
            lock;
        }
    };

    struct require_write_lock
    {
        template<typename T>
        require_write_lock(const std::unique_lock<T>& lock)
        {
            lock;
        }
    };

    void UpdateZoneWindows() noexcept;
    void UpdateWindowsPositions() noexcept;
    void CycleActiveZoneSet(DWORD vkCode) noexcept;
    bool OnSnapHotkeyBasedOnZoneNumber(HWND window, DWORD vkCode) noexcept;
    bool OnSnapHotkeyBasedOnPosition(HWND window, DWORD vkCode) noexcept;
    bool OnSnapHotkey(DWORD vkCode) noexcept;
    bool ProcessDirectedSnapHotkey(HWND window, DWORD vkCode, bool cycle, winrt::com_ptr<IZoneWindow> zoneWindow) noexcept;

    void RegisterVirtualDesktopUpdates(std::vector<GUID>& ids) noexcept;
    void UpdatePersistedData() noexcept;

    bool IsSplashScreen(HWND window);
    bool ShouldProcessNewWindow(HWND window) noexcept;
    std::vector<size_t> GetZoneIndexSetFromWorkAreaHistory(HWND window, winrt::com_ptr<IZoneWindow> workArea) noexcept;
    std::pair<winrt::com_ptr<IZoneWindow>, std::vector<size_t>> GetAppZoneHistoryInfo(HWND window, HMONITOR monitor, std::unordered_map<HMONITOR, winrt::com_ptr<IZoneWindow>>& workAreaMap) noexcept;
    std::pair<winrt::com_ptr<IZoneWindow>, std::vector<size_t>> GetAppZoneHistoryInfo(HWND window, HMONITOR monitor, bool isPrimaryMonitor) noexcept;
    void MoveWindowIntoZone(HWND window, winrt::com_ptr<IZoneWindow> zoneWindow, const std::vector<size_t>& zoneIndexSet) noexcept;

    void OnEditorExitEvent() noexcept;
    bool ShouldProcessSnapHotkey(DWORD vkCode) noexcept;

    std::vector<std::pair<HMONITOR, RECT>> GetRawMonitorData() noexcept;
    std::vector<HMONITOR> GetMonitorsSorted() noexcept;

    const HINSTANCE m_hinstance{};

    mutable std::shared_mutex m_lock;
    HWND m_window{};
    WindowMoveHandler m_windowMoveHandler;
    MonitorWorkAreaHandler m_workAreaHandler;

    winrt::com_ptr<IFancyZonesSettings> m_settings{};
    GUID m_previousDesktopId{}; // UUID of previously active virtual desktop.
    GUID m_currentDesktopId{}; // UUID of the current virtual desktop.
    wil::unique_handle m_terminateEditorEvent; // Handle of FancyZonesEditor.exe we launch and wait on
    wil::unique_handle m_terminateVirtualDesktopTrackerEvent;

    OnThreadExecutor m_dpiUnawareThread;
    OnThreadExecutor m_virtualDesktopTrackerThread;

    // If non-recoverable error occurs, trigger disabling of entire FancyZones.
    static std::function<void()> disableModuleCallback;

    static UINT WM_PRIV_VD_INIT; // Scheduled when FancyZones is initialized
    static UINT WM_PRIV_VD_SWITCH; // Scheduled when virtual desktop switch occurs
    static UINT WM_PRIV_VD_UPDATE; // Scheduled on virtual desktops update (creation/deletion)
    static UINT WM_PRIV_EDITOR; // Scheduled when the editor exits

    static UINT WM_PRIV_LOWLEVELKB; // Scheduled when we receive a key down press

    // Did we terminate the editor or was it closed cleanly?
    enum class EditorExitKind : byte
    {
        Exit,
        Terminate
    };
};

std::function<void()> FancyZones::disableModuleCallback = {};

UINT FancyZones::WM_PRIV_VD_INIT = RegisterWindowMessage(L"{469818a8-00fa-4069-b867-a1da484fcd9a}");
UINT FancyZones::WM_PRIV_VD_SWITCH = RegisterWindowMessage(L"{128c2cb0-6bdf-493e-abbe-f8705e04aa95}");
UINT FancyZones::WM_PRIV_VD_UPDATE = RegisterWindowMessage(L"{b8b72b46-f42f-4c26-9e20-29336cf2f22e}");
UINT FancyZones::WM_PRIV_EDITOR = RegisterWindowMessage(L"{87543824-7080-4e91-9d9c-0404642fc7b6}");
UINT FancyZones::WM_PRIV_LOWLEVELKB = RegisterWindowMessage(L"{763c03a3-03d9-4cde-8d71-f0358b0b4b52}");

// IFancyZones
IFACEMETHODIMP_(void)
FancyZones::Run() noexcept
{
    std::unique_lock writeLock(m_lock);

    WNDCLASSEXW wcex{};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.lpfnWndProc = s_WndProc;
    wcex.hInstance = m_hinstance;
    wcex.lpszClassName = NonLocalizable::ToolWindowClassName;
    RegisterClassExW(&wcex);

    BufferedPaintInit();

    m_window = CreateWindowExW(WS_EX_TOOLWINDOW, NonLocalizable::ToolWindowClassName, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, m_hinstance, this);
    if (!m_window)
    {
        return;
    }

    RegisterHotKey(m_window, 1, m_settings->GetSettings()->editorHotkey.get_modifiers(), m_settings->GetSettings()->editorHotkey.get_code());

    VirtualDesktopInitialize();

    m_dpiUnawareThread.submit(OnThreadExecutor::task_t{ [] {
                          SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE);
                          SetThreadDpiHostingBehavior(DPI_HOSTING_BEHAVIOR_MIXED);
                      } })
        .wait();

    m_terminateVirtualDesktopTrackerEvent.reset(CreateEvent(nullptr, FALSE, FALSE, nullptr));
    m_virtualDesktopTrackerThread.submit(OnThreadExecutor::task_t{ [&] { VirtualDesktopUtils::HandleVirtualDesktopUpdates(m_window, WM_PRIV_VD_UPDATE, m_terminateVirtualDesktopTrackerEvent.get()); } });
}

// IFancyZones
IFACEMETHODIMP_(void)
FancyZones::Destroy() noexcept
{
    std::unique_lock writeLock(m_lock);
    m_workAreaHandler.Clear();
    BufferedPaintUnInit();
    if (m_window)
    {
        DestroyWindow(m_window);
        m_window = nullptr;
    }
    if (m_terminateVirtualDesktopTrackerEvent)
    {
        SetEvent(m_terminateVirtualDesktopTrackerEvent.get());
    }
}

// IFancyZonesCallback
IFACEMETHODIMP_(void)
FancyZones::VirtualDesktopChanged() noexcept
{
    // VirtualDesktopChanged is called from a reentrant WinHookProc function, therefore we must postpone the actual logic
    // until we're in FancyZones::WndProc, which is not reentrant.
    PostMessage(m_window, WM_PRIV_VD_SWITCH, 0, 0);
}

// IFancyZonesCallback
IFACEMETHODIMP_(void)
FancyZones::VirtualDesktopInitialize() noexcept
{
    PostMessage(m_window, WM_PRIV_VD_INIT, 0, 0);
}

bool FancyZones::ShouldProcessNewWindow(HWND window) noexcept
{
    using namespace FancyZonesUtils;
    // Avoid processing splash screens, already stamped (zoned) windows, or those windows
    // that belong to excluded applications list.
    if (IsSplashScreen(window) ||
        (reinterpret_cast<size_t>(::GetProp(window, ZonedWindowProperties::PropertyMultipleZoneID)) != 0) ||
        !IsCandidateForLastKnownZone(window, m_settings->GetSettings()->excludedAppsArray))
    {
        return false;
    }
    return true;
}

std::vector<size_t> FancyZones::GetZoneIndexSetFromWorkAreaHistory(
    HWND window,
    winrt::com_ptr<IZoneWindow> workArea) noexcept
{
    const auto activeZoneSet = workArea->ActiveZoneSet();
    if (activeZoneSet)
    {
        wil::unique_cotaskmem_string zoneSetId;
        if (SUCCEEDED(StringFromCLSID(activeZoneSet->Id(), &zoneSetId)))
        {
            return FancyZonesDataInstance().GetAppLastZoneIndexSet(window, workArea->UniqueId(), zoneSetId.get());
        }
    }
    return {};
}

std::pair<winrt::com_ptr<IZoneWindow>, std::vector<size_t>> FancyZones::GetAppZoneHistoryInfo(
    HWND window,
    HMONITOR monitor,
    std::unordered_map<HMONITOR, winrt::com_ptr<IZoneWindow>>& workAreaMap) noexcept
{
    if (workAreaMap.contains(monitor))
    {
        auto workArea = workAreaMap[monitor];
        workAreaMap.erase(monitor); // monitor processed, remove entry from the map
        return { workArea, GetZoneIndexSetFromWorkAreaHistory(window, workArea) };
    }
    return { nullptr, {} };
}

std::pair<winrt::com_ptr<IZoneWindow>, std::vector<size_t>> FancyZones::GetAppZoneHistoryInfo(HWND window, HMONITOR monitor, bool isPrimaryMonitor) noexcept
{
    std::pair<winrt::com_ptr<IZoneWindow>, std::vector<size_t>> appZoneHistoryInfo{ nullptr, {} };
    auto workAreaMap = m_workAreaHandler.GetWorkAreasByDesktopId(m_currentDesktopId);

    // Search application history on currently active monitor.
    appZoneHistoryInfo = GetAppZoneHistoryInfo(window, monitor, workAreaMap);

    if (isPrimaryMonitor && appZoneHistoryInfo.second.empty())
    {
        // No application history on primary monitor, search on remaining monitors.
        for (const auto& [monitor, workArea] : workAreaMap)
        {
            auto zoneIndexSet = GetZoneIndexSetFromWorkAreaHistory(window, workArea);
            if (!zoneIndexSet.empty())
            {
                return { workArea, zoneIndexSet };
            }
        }
    }

    return appZoneHistoryInfo;
}

void FancyZones::MoveWindowIntoZone(HWND window, winrt::com_ptr<IZoneWindow> zoneWindow, const std::vector<size_t>& zoneIndexSet) noexcept
{
    auto& fancyZonesData = FancyZonesDataInstance();
    if (!fancyZonesData.IsAnotherWindowOfApplicationInstanceZoned(window, zoneWindow->UniqueId()))
    {
        m_windowMoveHandler.MoveWindowIntoZoneByIndexSet(window, zoneIndexSet, zoneWindow);
        fancyZonesData.UpdateProcessIdToHandleMap(window, zoneWindow->UniqueId());
    }
}

inline int RectWidth(const RECT& rect)
{
    return rect.right - rect.left;
}

inline int RectHeight(const RECT& rect)
{
    return rect.bottom - rect.top;
}

RECT FitOnScreen(const RECT& windowRect, const RECT& originMonitorRect, const RECT& destMonitorRect)
{
    // New window position on active monitor. If window fits the screen, this will be final position.
    int left = destMonitorRect.left + (windowRect.left - originMonitorRect.left);
    int top = destMonitorRect.top + (windowRect.top - originMonitorRect.top);
    int W = RectWidth(windowRect);
    int H = RectHeight(windowRect);

    if ((left < destMonitorRect.left) || (left + W > destMonitorRect.right))
    {
        // Set left window border to left border of screen (add padding). Resize window width if needed.
        left = destMonitorRect.left + CUSTOM_POSITIONING_LEFT_TOP_PADDING;
        W = min(W, RectWidth(destMonitorRect) - CUSTOM_POSITIONING_LEFT_TOP_PADDING);
    }
    if ((top < destMonitorRect.top) || (top + H > destMonitorRect.bottom))
    {
        // Set top window border to top border of screen (add padding). Resize window height if needed.
        top = destMonitorRect.top + CUSTOM_POSITIONING_LEFT_TOP_PADDING;
        H = min(H, RectHeight(destMonitorRect) - CUSTOM_POSITIONING_LEFT_TOP_PADDING);
    }

    return { .left = left,
             .top = top,
             .right = left + W,
             .bottom = top + H };
}

void OpenWindowOnActiveMonitor(HWND window, HMONITOR monitor) noexcept
{
    // By default Windows opens new window on primary monitor.
    // Try to preserve window width and height, adjust top-left corner if needed.
    HMONITOR origin = MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY);
    if (origin == monitor)
    {
        // Certain applications by design open in last known position, regardless of FancyZones.
        // If that position is on currently active monitor, skip custom positioning.
        return;
    }

    WINDOWPLACEMENT placement{};
    if (GetWindowPlacement(window, &placement))
    {
        MONITORINFOEX originMi;
        originMi.cbSize = sizeof(originMi);
        if (GetMonitorInfo(origin, &originMi))
        {
            MONITORINFOEX destMi;
            destMi.cbSize = sizeof(destMi);
            if (GetMonitorInfo(monitor, &destMi))
            {
                RECT newPosition = FitOnScreen(placement.rcNormalPosition, originMi.rcWork, destMi.rcWork);
                FancyZonesUtils::SizeWindowToRect(window, newPosition);
            }
        }
    }
}

// IFancyZonesCallback
IFACEMETHODIMP_(void)
FancyZones::WindowCreated(HWND window) noexcept
{
    std::shared_lock readLock(m_lock);
    GUID desktopId{};
    if (VirtualDesktopUtils::GetWindowDesktopId(window, &desktopId) && desktopId != m_currentDesktopId)
    {
        // Switch between virtual desktops results with posting same windows messages that also indicate
        // creation of new window. We need to check if window being processed is on currently active desktop.
        return;
    }
    const bool moveToAppLastZone = m_settings->GetSettings()->appLastZone_moveWindows;
    const bool openOnActiveMonitor = m_settings->GetSettings()->openWindowOnActiveMonitor;
    if ((moveToAppLastZone || openOnActiveMonitor) && ShouldProcessNewWindow(window))
    {
        HMONITOR primary = MonitorFromWindow(nullptr, MONITOR_DEFAULTTOPRIMARY);
        HMONITOR active = primary;

        POINT cursorPosition{};
        if (GetCursorPos(&cursorPosition))
        {
            active = MonitorFromPoint(cursorPosition, MONITOR_DEFAULTTOPRIMARY);
        }

        bool windowZoned{ false };
        if (moveToAppLastZone)
        {
            const bool primaryActive = (primary == active);
            std::pair<winrt::com_ptr<IZoneWindow>, std::vector<size_t>> appZoneHistoryInfo = GetAppZoneHistoryInfo(window, active, primaryActive);
            if (!appZoneHistoryInfo.second.empty())
            {
                MoveWindowIntoZone(window, appZoneHistoryInfo.first, appZoneHistoryInfo.second);
                windowZoned = true;
            }
        }
        if (!windowZoned && openOnActiveMonitor)
        {
            m_dpiUnawareThread.submit(OnThreadExecutor::task_t{ [&] { OpenWindowOnActiveMonitor(window, active); } }).wait();
        }
    }
}

// IFancyZonesCallback
IFACEMETHODIMP_(bool)
FancyZones::OnKeyDown(PKBDLLHOOKSTRUCT info) noexcept
{
    // Return true to swallow the keyboard event
    bool const shift = GetAsyncKeyState(VK_SHIFT) & 0x8000;
    bool const win = GetAsyncKeyState(VK_LWIN) & 0x8000 || GetAsyncKeyState(VK_RWIN) & 0x8000;
    bool const alt = GetAsyncKeyState(VK_MENU) & 0x8000;
    bool const ctrl = GetAsyncKeyState(VK_CONTROL) & 0x8000;
    if ((win && !shift && !ctrl) || (win && ctrl && alt))
    {
        // Temporarily disable Win+Ctrl+Number functionality
        // if (ctrl)
        // {
        //    if ((info->vkCode >= '0') && (info->vkCode <= '9'))
        //    {
        //        // Win+Ctrl+Number will cycle through ZoneSets
        //        Trace::FancyZones::OnKeyDown(info->vkCode, win, ctrl, false /*inMoveSize*/);
        //        CycleActiveZoneSet(info->vkCode);
        //        return true;
        //    }
        // }
        // else
        if ((info->vkCode == VK_RIGHT) || (info->vkCode == VK_LEFT) || (info->vkCode == VK_UP) || (info->vkCode == VK_DOWN))
        {
            if (ShouldProcessSnapHotkey(info->vkCode))
            {
                Trace::FancyZones::OnKeyDown(info->vkCode, win, ctrl, false /*inMoveSize*/);
                // Win+Left, Win+Right will cycle through Zones in the active ZoneSet when WM_PRIV_LOWLEVELKB's handled
                PostMessageW(m_window, WM_PRIV_LOWLEVELKB, 0, info->vkCode);
                return true;
            }
        }
    }
    // Temporarily disable Win+Ctrl+Number functionality
    //else if (m_inMoveSize && (info->vkCode >= '0') && (info->vkCode <= '9'))
    //{
    //    // This allows you to cycle through ZoneSets while dragging a window
    //    Trace::FancyZones::OnKeyDown(info->vkCode, win, false /*control*/, true /*inMoveSize*/);
    //    CycleActiveZoneSet(info->vkCode);
    //    return false;
    //}

    if (m_windowMoveHandler.IsDragEnabled() && shift)
    {
        return true;
    }
    return false;
}

// IFancyZonesCallback
void FancyZones::ToggleEditor() noexcept
{
    {
        std::shared_lock readLock(m_lock);
        if (m_terminateEditorEvent)
        {
            SetEvent(m_terminateEditorEvent.get());
            return;
        }
    }

    {
        std::unique_lock writeLock(m_lock);
        m_terminateEditorEvent.reset(CreateEvent(nullptr, true, false, nullptr));
    }

    HMONITOR monitor{};
    HWND foregroundWindow{};

    const bool use_cursorpos_editor_startupscreen = m_settings->GetSettings()->use_cursorpos_editor_startupscreen;
    POINT currentCursorPos{};
    if (use_cursorpos_editor_startupscreen)
    {
        GetCursorPos(&currentCursorPos);
        monitor = MonitorFromPoint(currentCursorPos, MONITOR_DEFAULTTOPRIMARY);
    }
    else
    {
        foregroundWindow = GetForegroundWindow();
        monitor = MonitorFromWindow(foregroundWindow, MONITOR_DEFAULTTOPRIMARY);
    }

    if (!monitor)
    {
        return;
    }

    winrt::com_ptr<IZoneWindow> zoneWindow;

    std::shared_lock readLock(m_lock);
    
    if (m_settings->GetSettings()->spanZonesAcrossMonitors)
    {
        zoneWindow = m_workAreaHandler.GetWorkArea(m_currentDesktopId, NULL);
    }
    else
    {
        zoneWindow = m_workAreaHandler.GetWorkArea(m_currentDesktopId, monitor);
    }
    
    if (!zoneWindow)
    {
        return;
    }

    std::wstring editorLocation;

    if (m_settings->GetSettings()->spanZonesAcrossMonitors)
    {
        std::vector<std::pair<HMONITOR, RECT>> allMonitors;

        m_dpiUnawareThread.submit(OnThreadExecutor::task_t{ [&] {
                              allMonitors = FancyZonesUtils::GetAllMonitorRects<&MONITORINFOEX::rcWork>();
            } }).wait();

        UINT currentDpi = 0;
        for (const auto& monitor : allMonitors)
        {
            UINT dpiX = 0;
            UINT dpiY = 0;
            if (GetDpiForMonitor(monitor.first, MDT_EFFECTIVE_DPI, &dpiX, &dpiY) == S_OK)
            {
                if (currentDpi == 0)
                {
                  currentDpi = dpiX;
                  continue;
                }
                if (currentDpi != dpiX)
                {
                    MessageBoxW(NULL,
                    GET_RESOURCE_STRING(IDS_SPAN_ACROSS_ZONES_WARNING).c_str(),
                    GET_RESOURCE_STRING(IDS_POWERTOYS_FANCYZONES).c_str(),
                    MB_OK | MB_ICONWARNING);
                    break;
                }
            }
        }

        for (auto& [monitor, workArea] : allMonitors)
        {
            const auto x = workArea.left;
            const auto y = workArea.top;
            const auto width = workArea.right - workArea.left;
            const auto height = workArea.bottom - workArea.top;
            std::wstring editorLocationPart =
                std::to_wstring(x) + L"_" +
                std::to_wstring(y) + L"_" +
                std::to_wstring(width) + L"_" +
                std::to_wstring(height);

            if (editorLocation.empty())
            {
                editorLocation = std::move(editorLocationPart);
            }
            else
            {
                editorLocation += L'/';
                editorLocation += editorLocationPart;
            }
        }
    }
    else
    {
        MONITORINFOEX mi;
        mi.cbSize = sizeof(mi);

        m_dpiUnawareThread.submit(OnThreadExecutor::task_t{ [&] {
            GetMonitorInfo(monitor, &mi);
            } }).wait();

        const auto x = mi.rcWork.left;
        const auto y = mi.rcWork.top;
        const auto width = mi.rcWork.right - mi.rcWork.left;
        const auto height = mi.rcWork.bottom - mi.rcWork.top;
        editorLocation =
            std::to_wstring(x) + L"_" +
            std::to_wstring(y) + L"_" +
            std::to_wstring(width) + L"_" +
            std::to_wstring(height);
    }

    const auto& fancyZonesData = FancyZonesDataInstance();

    if (!fancyZonesData.SerializeDeviceInfoToTmpFile(zoneWindow->UniqueId()))
    {
        return;
    }

    const std::wstring params =
        /*1*/ editorLocation + L" " +
        /*2*/ L"\"" + std::to_wstring(GetCurrentProcessId()) + L"\"";

    SHELLEXECUTEINFO sei{ sizeof(sei) };
    sei.fMask = { SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI };
    sei.lpFile = NonLocalizable::FZEditorExecutablePath;
    sei.lpParameters = params.c_str();
    sei.nShow = SW_SHOWNORMAL;
    ShellExecuteEx(&sei);
    Trace::FancyZones::EditorLaunched(1);

    // Launch the editor on a background thread
    // Wait for the editor's process to exit
    // Post back to the main thread to update
    std::thread waitForEditorThread([window = m_window, processHandle = sei.hProcess, terminateEditorEvent = m_terminateEditorEvent.get()]() {
        HANDLE waitEvents[2] = { processHandle, terminateEditorEvent };
        auto result = WaitForMultipleObjects(2, waitEvents, false, INFINITE);
        if (result == WAIT_OBJECT_0 + 0)
        {
            // Editor exited
            // Update any changes it may have made
            PostMessage(window, WM_PRIV_EDITOR, 0, static_cast<LPARAM>(EditorExitKind::Exit));
        }
        else if (result == WAIT_OBJECT_0 + 1)
        {
            // User hit Win+~ while editor is already running
            // Shut it down
            TerminateProcess(processHandle, 2);
            PostMessage(window, WM_PRIV_EDITOR, 0, static_cast<LPARAM>(EditorExitKind::Terminate));
        }
        CloseHandle(processHandle);
    });

    waitForEditorThread.detach();
}

void FancyZones::SettingsChanged() noexcept
{
    // Update the hotkey
    UnregisterHotKey(m_window, 1);
    RegisterHotKey(m_window, 1, m_settings->GetSettings()->editorHotkey.get_modifiers(), m_settings->GetSettings()->editorHotkey.get_code());

    // Needed if we toggled spanZonesAcrossMonitors
    m_workAreaHandler.Clear();
    OnDisplayChange(DisplayChangeType::Initialization);
}

// IZoneWindowHost
IFACEMETHODIMP_(void)
FancyZones::MoveWindowsOnActiveZoneSetChange() noexcept
{
    if (m_settings->GetSettings()->zoneSetChange_moveWindows)
    {
        UpdateWindowsPositions();
    }
}

LRESULT FancyZones::WndProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept
{
    switch (message)
    {
    case WM_HOTKEY:
    {
        if (wparam == 1)
        {
            ToggleEditor();
        }
    }
    break;

    case WM_SETTINGCHANGE:
    {
        if (wparam == SPI_SETWORKAREA)
        {
            // Changes in taskbar position resulted in different size of work area.
            // Invalidate cached work-areas so they can be recreated with latest information.
            m_workAreaHandler.Clear();
            OnDisplayChange(DisplayChangeType::WorkArea);
        }
    }
    break;

    case WM_DISPLAYCHANGE:
    {
        // Display resolution changed. Invalidate cached work-areas so they can be recreated with latest information.
        m_workAreaHandler.Clear();
        OnDisplayChange(DisplayChangeType::DisplayChange);
    }
    break;

    default:
    {
        POINT ptScreen;
        GetPhysicalCursorPos(&ptScreen);

        if (message == WM_PRIV_LOWLEVELKB)
        {
            OnSnapHotkey(static_cast<DWORD>(lparam));
        }
        else if (message == WM_PRIV_VD_INIT)
        {
            OnDisplayChange(DisplayChangeType::Initialization);
        }
        else if (message == WM_PRIV_VD_SWITCH)
        {
            OnDisplayChange(DisplayChangeType::VirtualDesktop);
        }
        else if (message == WM_PRIV_VD_UPDATE)
        {
            std::vector<GUID> ids{};
            if (VirtualDesktopUtils::GetVirtualDesktopIds(ids))
            {
                RegisterVirtualDesktopUpdates(ids);
            }
        }
        else if (message == WM_PRIV_EDITOR)
        {
            if (lparam == static_cast<LPARAM>(EditorExitKind::Exit))
            {
                OnEditorExitEvent();
            }

            {
                // Clean up the event either way
                std::unique_lock writeLock(m_lock);
                m_terminateEditorEvent.release();
            }
        }
        else if (message == WM_PRIV_MOVESIZESTART)
        {
            auto hwnd = reinterpret_cast<HWND>(wparam);
            if (auto monitor = MonitorFromPoint(ptScreen, MONITOR_DEFAULTTONULL))
            {
                MoveSizeStart(hwnd, monitor, ptScreen);
            }
        }
        else if (message == WM_PRIV_MOVESIZEEND)
        {
            auto hwnd = reinterpret_cast<HWND>(wparam);
            MoveSizeEnd(hwnd, ptScreen);
        }
        else if (message == WM_PRIV_LOCATIONCHANGE && InMoveSize())
        {
            if (auto monitor = MonitorFromPoint(ptScreen, MONITOR_DEFAULTTONULL))
            {
                MoveSizeUpdate(monitor, ptScreen);
            }
        }
        else if (message == WM_PRIV_WINDOWCREATED)
        {
            auto hwnd = reinterpret_cast<HWND>(wparam);
            WindowCreated(hwnd);
        }
        else
        {
            return DefWindowProc(window, message, wparam, lparam);
        }
    }
    break;
    }
    return 0;
}

void FancyZones::OnDisplayChange(DisplayChangeType changeType) noexcept
{
    if (changeType == DisplayChangeType::VirtualDesktop ||
        changeType == DisplayChangeType::Initialization)
    {
        m_previousDesktopId = m_currentDesktopId;
        GUID currentVirtualDesktopId{};
        if (VirtualDesktopUtils::GetCurrentVirtualDesktopId(&currentVirtualDesktopId))
        {
            m_currentDesktopId = currentVirtualDesktopId;
            if (m_previousDesktopId != GUID_NULL && m_currentDesktopId != m_previousDesktopId)
            {
                Trace::VirtualDesktopChanged();
            }
        }
        if (changeType == DisplayChangeType::Initialization)
        {
            UpdatePersistedData();
        }
    }

    UpdateZoneWindows();

    if ((changeType == DisplayChangeType::WorkArea) || (changeType == DisplayChangeType::DisplayChange))
    {
        if (m_settings->GetSettings()->displayChange_moveWindows)
        {
            UpdateWindowsPositions();
        }
    }
}

void FancyZones::AddZoneWindow(HMONITOR monitor, PCWSTR deviceId) noexcept
{
    std::unique_lock writeLock(m_lock);

    if (m_workAreaHandler.IsNewWorkArea(m_currentDesktopId, monitor))
    {
        wil::unique_cotaskmem_string virtualDesktopId;
        if (SUCCEEDED(StringFromCLSID(m_currentDesktopId, &virtualDesktopId)))
        {
            std::wstring uniqueId;

            if (monitor)
            {
                uniqueId = ZoneWindowUtils::GenerateUniqueId(monitor, deviceId, virtualDesktopId.get());
            }
            else
            {
                uniqueId = ZoneWindowUtils::GenerateUniqueIdAllMonitorsArea(virtualDesktopId.get());
            }

            // "Turning FLASHING_ZONE option off"
            //const bool flash = m_settings->GetSettings()->zoneSetChange_flashZones;
            const bool flash = false;

            std::wstring parentId{};
            auto parentArea = m_workAreaHandler.GetWorkArea(m_previousDesktopId, monitor);
            if (parentArea)
            {
                parentId = parentArea->UniqueId();
            }
            auto workArea = MakeZoneWindow(this, m_hinstance, monitor, uniqueId, parentId, flash);
            if (workArea)
            {
                m_workAreaHandler.AddWorkArea(m_currentDesktopId, monitor, workArea);
                FancyZonesDataInstance().SaveFancyZonesData();
            }
        }
    }
}

LRESULT CALLBACK FancyZones::s_WndProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept
{
    auto thisRef = reinterpret_cast<FancyZones*>(GetWindowLongPtr(window, GWLP_USERDATA));
    if (!thisRef && (message == WM_CREATE))
    {
        const auto createStruct = reinterpret_cast<LPCREATESTRUCT>(lparam);
        thisRef = reinterpret_cast<FancyZones*>(createStruct->lpCreateParams);
        SetWindowLongPtr(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(thisRef));
    }

    return thisRef ? thisRef->WndProc(window, message, wparam, lparam) :
                     DefWindowProc(window, message, wparam, lparam);
}

void FancyZones::UpdateZoneWindows() noexcept
{
    auto callback = [](HMONITOR monitor, HDC, RECT*, LPARAM data) -> BOOL {
        MONITORINFOEX mi;
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfo(monitor, &mi))
        {
            DISPLAY_DEVICE displayDevice = { sizeof(displayDevice) };
            PCWSTR deviceId = nullptr;

            bool validMonitor = true;
            if (EnumDisplayDevices(mi.szDevice, 0, &displayDevice, 1))
            {
                if (WI_IsFlagSet(displayDevice.StateFlags, DISPLAY_DEVICE_MIRRORING_DRIVER))
                {
                    validMonitor = FALSE;
                }
                else if (displayDevice.DeviceID[0] != L'\0')
                {
                    deviceId = displayDevice.DeviceID;
                }
            }

            if (validMonitor)
            {
                if (!deviceId)
                {
                    deviceId = GetSystemMetrics(SM_REMOTESESSION) ?
                                   L"\\\\?\\DISPLAY#REMOTEDISPLAY#" :
                                   L"\\\\?\\DISPLAY#LOCALDISPLAY#";
                }

                auto strongThis = reinterpret_cast<FancyZones*>(data);
                strongThis->AddZoneWindow(monitor, deviceId);
            }
        }
        return TRUE;
    };

    if (m_settings->GetSettings()->spanZonesAcrossMonitors)
    {
        AddZoneWindow(nullptr, NULL);
    }
    else
    {
        EnumDisplayMonitors(nullptr, nullptr, callback, reinterpret_cast<LPARAM>(this));
    }
}

void FancyZones::UpdateWindowsPositions() noexcept
{
    auto callback = [](HWND window, LPARAM data) -> BOOL {
        size_t bitmask = reinterpret_cast<size_t>(::GetProp(window, ZonedWindowProperties::PropertyMultipleZoneID));

        if (bitmask != 0)
        {
            std::vector<size_t> indexSet;
            for (int i = 0; i < std::numeric_limits<size_t>::digits; i++)
            {
                if ((1ull << i) & bitmask)
                {
                    indexSet.push_back(i);
                }
            }

            auto strongThis = reinterpret_cast<FancyZones*>(data);
            std::unique_lock writeLock(strongThis->m_lock);
            auto zoneWindow = strongThis->m_workAreaHandler.GetWorkArea(window);
            if (zoneWindow)
            {
                strongThis->m_windowMoveHandler.MoveWindowIntoZoneByIndexSet(window, indexSet, zoneWindow);
            }
        }
        return TRUE;
    };
    EnumWindows(callback, reinterpret_cast<LPARAM>(this));
}

void FancyZones::CycleActiveZoneSet(DWORD vkCode) noexcept
{
    auto window = GetForegroundWindow();
    if (FancyZonesUtils::IsCandidateForZoning(window, m_settings->GetSettings()->excludedAppsArray))
    {
        const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONULL);
        if (monitor)
        {
            std::shared_lock readLock(m_lock);

            auto zoneWindow = m_workAreaHandler.GetWorkArea(m_currentDesktopId, monitor);
            if (zoneWindow)
            {
                zoneWindow->CycleActiveZoneSet(vkCode);
            }
        }
    }
}

bool FancyZones::OnSnapHotkeyBasedOnZoneNumber(HWND window, DWORD vkCode) noexcept
{
    HMONITOR current;

    if (m_settings->GetSettings()->spanZonesAcrossMonitors)
    {
        current = NULL;
    }
    else
    {
        current = MonitorFromWindow(window, MONITOR_DEFAULTTONULL);
    }

    std::vector<HMONITOR> monitorInfo = GetMonitorsSorted();
    if (current && monitorInfo.size() > 1 && m_settings->GetSettings()->moveWindowAcrossMonitors)
    {
        // Multi monitor environment.
        auto currMonitorInfo = std::find(std::begin(monitorInfo), std::end(monitorInfo), current);
        do
        {
            std::unique_lock writeLock(m_lock);
            if (m_windowMoveHandler.MoveWindowIntoZoneByDirectionAndIndex(window, vkCode, false /* cycle through zones */, m_workAreaHandler.GetWorkArea(m_currentDesktopId, *currMonitorInfo)))
            {
                return true;
            }
            // We iterated through all zones in current monitor zone layout, move on to next one (or previous depending on direction).
            if (vkCode == VK_RIGHT)
            {
                currMonitorInfo = std::next(currMonitorInfo);
                if (currMonitorInfo == std::end(monitorInfo))
                {
                    currMonitorInfo = std::begin(monitorInfo);
                }
            }
            else if (vkCode == VK_LEFT)
            {
                if (currMonitorInfo == std::begin(monitorInfo))
                {
                    currMonitorInfo = std::end(monitorInfo);
                }
                currMonitorInfo = std::prev(currMonitorInfo);
            }
        } while (*currMonitorInfo != current);
    }
    else
    {
        // Single monitor environment, or combined multi-monitor environment.
        std::unique_lock writeLock(m_lock);
        if (m_settings->GetSettings()->restoreSize)
        {
            bool moved = m_windowMoveHandler.MoveWindowIntoZoneByDirectionAndIndex(window, vkCode, false /* cycle through zones */, m_workAreaHandler.GetWorkArea(m_currentDesktopId, current));
            if (!moved)
            {
                FancyZonesUtils::RestoreWindowOrigin(window);
                FancyZonesUtils::RestoreWindowSize(window);
            }
            return true;
        }
        else
        {
            return m_windowMoveHandler.MoveWindowIntoZoneByDirectionAndIndex(window, vkCode, true /* cycle through zones */, m_workAreaHandler.GetWorkArea(m_currentDesktopId, current));
        }
    }

    return false;
}

bool FancyZones::OnSnapHotkeyBasedOnPosition(HWND window, DWORD vkCode) noexcept
{
    HMONITOR current;

    if (m_settings->GetSettings()->spanZonesAcrossMonitors)
    {
        current = NULL;
    }
    else
    {
        current = MonitorFromWindow(window, MONITOR_DEFAULTTONULL);
    }

    auto allMonitors = FancyZonesUtils::GetAllMonitorRects<&MONITORINFOEX::rcWork>();

    if (current && allMonitors.size() > 1 && m_settings->GetSettings()->moveWindowAcrossMonitors)
    {
        // Multi monitor environment.
        // First, try to stay on the same monitor
        bool success = ProcessDirectedSnapHotkey(window, vkCode, false, m_workAreaHandler.GetWorkArea(m_currentDesktopId, current));
        if (success)
        {
            return true;
        }

        // If that didn't work, extract zones from all other monitors and target one of them
        std::vector<RECT> zoneRects;
        std::vector<std::pair<size_t, winrt::com_ptr<IZoneWindow>>> zoneRectsInfo;
        RECT currentMonitorRect{ .top = 0, .bottom = -1 };

        for (const auto& [monitor, monitorRect] : allMonitors)
        {
            if (monitor == current)
            {
                currentMonitorRect = monitorRect;
            }
            else
            {
                auto workArea = m_workAreaHandler.GetWorkArea(m_currentDesktopId, monitor);
                auto zoneSet = workArea->ActiveZoneSet();
                if (zoneSet)
                {
                    auto zones = zoneSet->GetZones();
                    for (size_t i = 0; i < zones.size(); i++)
                    {
                        const auto& zone = zones[i];
                        RECT zoneRect = zone->GetZoneRect();

                        zoneRect.left += monitorRect.left;
                        zoneRect.right += monitorRect.left;
                        zoneRect.top += monitorRect.top;
                        zoneRect.bottom += monitorRect.top;

                        zoneRects.emplace_back(zoneRect);
                        zoneRectsInfo.emplace_back(i, workArea);
                    }
                }
            }
        }

        // Ensure we can get the windowRect, if not, just quit
        RECT windowRect;
        if (!GetWindowRect(window, &windowRect))
        {
            return false;
        }

        size_t chosenIdx = FancyZonesUtils::ChooseNextZoneByPosition(vkCode, windowRect, zoneRects);

        if (chosenIdx < zoneRects.size())
        {
            // Moving to another monitor succeeded
            const auto& [trueZoneIdx, zoneWindow] = zoneRectsInfo[chosenIdx];
            m_windowMoveHandler.MoveWindowIntoZoneByIndexSet(window, { trueZoneIdx }, zoneWindow);
            return true;
        }

        // We reached the end of all monitors.
        // Try again, cycling on all monitors.
        // First, add zones from the origin monitor to zoneRects
        // Sanity check: the current monitor is valid
        if (currentMonitorRect.top <= currentMonitorRect.bottom)
        {
            auto workArea = m_workAreaHandler.GetWorkArea(m_currentDesktopId, current);
            auto zoneSet = workArea->ActiveZoneSet();
            if (zoneSet)
            {
                auto zones = zoneSet->GetZones();
                for (size_t i = 0; i < zones.size(); i++)
                {
                    const auto& zone = zones[i];
                    RECT zoneRect = zone->GetZoneRect();

                    zoneRect.left += currentMonitorRect.left;
                    zoneRect.right += currentMonitorRect.left;
                    zoneRect.top += currentMonitorRect.top;
                    zoneRect.bottom += currentMonitorRect.top;

                    zoneRects.emplace_back(zoneRect);
                    zoneRectsInfo.emplace_back(i, workArea);
                }
            }
        }
        else
        {
            return false;
        }

        RECT combinedRect = FancyZonesUtils::GetAllMonitorsCombinedRect<&MONITORINFOEX::rcWork>();
        windowRect = FancyZonesUtils::PrepareRectForCycling(windowRect, combinedRect, vkCode);
        chosenIdx = FancyZonesUtils::ChooseNextZoneByPosition(vkCode, windowRect, zoneRects);
        if (chosenIdx < zoneRects.size())
        {
            // Moving to another monitor succeeded
            const auto& [trueZoneIdx, zoneWindow] = zoneRectsInfo[chosenIdx];
            m_windowMoveHandler.MoveWindowIntoZoneByIndexSet(window, { trueZoneIdx }, zoneWindow);
            return true;
        }
        else
        {
            // Giving up
            return false;
        }
    }
    else
    {
        // Single monitor environment, or combined multi-monitor environment.
        return ProcessDirectedSnapHotkey(window, vkCode, true, m_workAreaHandler.GetWorkArea(m_currentDesktopId, current));
    }
}

bool FancyZones::OnSnapHotkey(DWORD vkCode) noexcept
{
    auto window = GetForegroundWindow();
    if (FancyZonesUtils::IsCandidateForZoning(window, m_settings->GetSettings()->excludedAppsArray))
    {
        if (m_settings->GetSettings()->moveWindowsBasedOnPosition)
        {
            return OnSnapHotkeyBasedOnPosition(window, vkCode);
        }
        else
        {
            return (vkCode == VK_LEFT || vkCode == VK_RIGHT) && OnSnapHotkeyBasedOnZoneNumber(window, vkCode);
        }
    }
    return false;
}

bool FancyZones::ProcessDirectedSnapHotkey(HWND window, DWORD vkCode, bool cycle, winrt::com_ptr<IZoneWindow> zoneWindow) noexcept
{
    // Check whether Alt is used in the shortcut key combination
    if (GetAsyncKeyState(VK_MENU) & 0x8000)
    {
        return m_windowMoveHandler.ExtendWindowByDirectionAndPosition(window, vkCode, zoneWindow);
    }
    else
    {
        return m_windowMoveHandler.MoveWindowIntoZoneByDirectionAndPosition(window, vkCode, cycle, zoneWindow);
    }
}

void FancyZones::RegisterVirtualDesktopUpdates(std::vector<GUID>& ids) noexcept
{
    std::unique_lock writeLock(m_lock);

    m_workAreaHandler.RegisterUpdates(ids);
    UpdatePersistedData();
}

void FancyZones::UpdatePersistedData() noexcept
{
    std::vector<std::wstring> active{};
    if (VirtualDesktopUtils::GetVirtualDesktopIds(active) && !active.empty())
    {
        auto& fancyZonesData = FancyZonesDataInstance();
        if (fancyZonesData.PrimaryDesktopHasZeroedGUID())
        {
            fancyZonesData.UpdatePrimaryDesktopData(active[0]);
        }
        fancyZonesData.RemoveDeletedDesktops(active);
    }
}

bool FancyZones::IsSplashScreen(HWND window)
{
    wchar_t className[MAX_PATH];
    if (GetClassName(window, className, MAX_PATH) == 0)
    {
        return false;
    }

    return wcscmp(NonLocalizable::SplashClassName, className) == 0;
}

void FancyZones::OnEditorExitEvent() noexcept
{
    // Collect information about changes in zone layout after editor exited.
    FancyZonesDataInstance().ParseDataFromTmpFiles();

    for (auto workArea : m_workAreaHandler.GetAllWorkAreas())
    {
        workArea->UpdateActiveZoneSet();
    }
    if (m_settings->GetSettings()->zoneSetChange_moveWindows)
    {
        UpdateWindowsPositions();
    }
}

bool FancyZones::ShouldProcessSnapHotkey(DWORD vkCode) noexcept
{
    if (m_settings->GetSettings()->overrideSnapHotkeys)
    {
        HMONITOR monitor;
        if (m_settings->GetSettings()->spanZonesAcrossMonitors)
        {
            monitor = NULL;
        }
        else
        {
            monitor = MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTONULL);
        }
        
        auto zoneWindow = m_workAreaHandler.GetWorkArea(m_currentDesktopId, monitor);
        if (zoneWindow->ActiveZoneSet() != nullptr)
        {
            if (vkCode == VK_UP || vkCode == VK_DOWN)
            {
                return m_settings->GetSettings()->moveWindowsBasedOnPosition;
            }
            else
            {
                return true;
            }
        }
    }
    return false;
}

std::vector<HMONITOR> FancyZones::GetMonitorsSorted() noexcept
{
    std::shared_lock readLock(m_lock);

    auto monitorInfo = GetRawMonitorData();
    FancyZonesUtils::OrderMonitors(monitorInfo);
    std::vector<HMONITOR> output;
    std::transform(std::begin(monitorInfo), std::end(monitorInfo), std::back_inserter(output), [](const auto& info) { return info.first; });
    return output;
}

std::vector<std::pair<HMONITOR, RECT>> FancyZones::GetRawMonitorData() noexcept
{
    std::shared_lock readLock(m_lock);

    std::vector<std::pair<HMONITOR, RECT>> monitorInfo;
    const auto& activeWorkAreaMap = m_workAreaHandler.GetWorkAreasByDesktopId(m_currentDesktopId);
    for (const auto& [monitor, workArea] : activeWorkAreaMap)
    {
        if (workArea->ActiveZoneSet() != nullptr)
        {
            MONITORINFOEX mi;
            mi.cbSize = sizeof(mi);
            GetMonitorInfo(monitor, &mi);
            monitorInfo.push_back({ monitor, mi.rcMonitor });
        }
    }
    return monitorInfo;
}

winrt::com_ptr<IFancyZones> MakeFancyZones(HINSTANCE hinstance,
                                           const winrt::com_ptr<IFancyZonesSettings>& settings,
                                           std::function<void()> disableCallback) noexcept
{
    if (!settings)
    {
        return nullptr;
    }

    return winrt::make_self<FancyZones>(hinstance, settings, disableCallback);
}
