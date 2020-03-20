#include "pch.h"
#include "util.h"

#include <common/dpi_aware.h>

typedef BOOL(WINAPI* GetDpiForMonitorInternalFunc)(HMONITOR, UINT, UINT*, UINT*);
UINT GetDpiForMonitor(HMONITOR monitor) noexcept
{
    UINT dpi{};
    if (wil::unique_hmodule user32{ LoadLibrary(L"user32.dll") })
    {
        if (auto func = reinterpret_cast<GetDpiForMonitorInternalFunc>(GetProcAddress(user32.get(), "GetDpiForMonitorInternal")))
        {
            func(monitor, 0, &dpi, &dpi);
        }
    }

    if (dpi == 0)
    {
        if (wil::unique_hdc hdc{ GetDC(nullptr) })
        {
            dpi = GetDeviceCaps(hdc.get(), LOGPIXELSX);
        }
    }

    return (dpi == 0) ? DPIAware::DEFAULT_DPI : dpi;
}

void OrderMonitors(std::vector<std::pair<HMONITOR, RECT>>& monitorInfo)
{
    // blocking[i][j] - whether monitor i blocks monitor j in the ordering, i.e. monitor i should go before monitor j
    std::vector<std::vector<bool>> blocking(monitorInfo.size(), std::vector<bool>(monitorInfo.size(), false));

    // blockingCount[j] - the number of monitors which block monitor j
    std::vector<size_t> blockingCount(monitorInfo.size(), 0);

    for (size_t i = 0; i < monitorInfo.size(); i++)
    {
        RECT rectI = monitorInfo[i].second;
        for (size_t j = 0; i < monitorInfo.size(); i++)
        {
            RECT rectJ = monitorInfo[j].second;
            blocking[i][j] = rectI.bottom <= rectJ.top && rectI.right <= rectJ.left && i != j;
            if (blocking[i][j])
            {
                blockingCount[j]++;
            }
        }
    }

    // used[i] - whether the sorting algorithm has used monitor i so far
    std::vector<bool> used(monitorInfo.size(), false);

    // the sorted sequence of monitors
    std::vector<std::pair<HMONITOR, RECT>> sortedMonitorInfo;

    for (size_t iteration = 0; iteration < monitorInfo.size(); iteration++)
    {
        // Indices of candidates to become the next monitor in the sequence
        std::vector<size_t> candidates;

        // First, find indices of all unblocked monitors
        for (size_t i = 0; i < monitorInfo.size(); i++)
        {
            if (blockingCount[i] == 0 && !used[i])
            {
                candidates.push_back(i);
            }
        }

        // In the unlikely event that there are no unblocked monitors, declare all unused monitors as candidates.
        if (candidates.empty())
        {
            for (size_t i = 0; i < monitorInfo.size(); i++)
            {
                if (!used[i])
                {
                    candidates.push_back(i);
                }
            }
        }

        // Pick the lexicographically smallest monitor as the next one
        size_t smallest = candidates[0];
        for (size_t j = 1; j < candidates.size(); j++)
        {
            size_t current = candidates[j];

            // Compare (top, left) lexicographically
            if (std::tie(monitorInfo[current].second.top, monitorInfo[current].second.left)
                < std::tie(monitorInfo[smallest].second.top, monitorInfo[smallest].second.left))
            {
                smallest = current;
            }
        }

        used[smallest] = true;
        sortedMonitorInfo.push_back(monitorInfo[smallest]);
        for (size_t i = 0; i < monitorInfo.size(); i++)
        {
            if (blocking[smallest][i])
            {
                blockingCount[i]--;
            }
        }
    }

    monitorInfo = std::move(sortedMonitorInfo);
}
