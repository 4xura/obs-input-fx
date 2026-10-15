#pragma once
#include "obs-api.hpp"
#include <algorithm>
#include <dwmapi.h>
#include <string>
#include <vector>
#include <windows.h>

namespace cursor_map {
inline std::wstring wide(const std::string &text) {
    if (text.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    std::wstring out(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), out.data(), n);
    return out;
}
inline std::string decode(std::string text) {
    for (auto entry : {std::pair<const char *, const char *>("#3A", ":"), {"#22", "#"}}) {
        size_t p = 0;
        while ((p = text.find(entry.first, p)) != std::string::npos) {
            text.replace(p, 3, entry.second);
            p += 1;
        }
    }
    return text;
}
inline bool same(const std::wstring &a, const std::wstring &b) {
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}
struct WindowSearch {
    std::wstring title, cls, exe;
    HWND exact = nullptr, fallback = nullptr, foreground = nullptr;
};
inline BOOL CALLBACK window_visit(HWND window, LPARAM opaque) {
    auto &query = *reinterpret_cast<WindowSearch *>(opaque);
    if (!IsWindowVisible(window) || IsIconic(window))
        return TRUE;
    wchar_t cls[512]{}, title[2048]{}, exe[32768]{};
    GetClassNameW(window, cls, 512);
    GetWindowTextW(window, title, 2048);
    if (!query.cls.empty() && !same(query.cls, cls))
        return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    DWORD length = 32768;
    bool valid = p && QueryFullProcessImageNameW(p, 0, exe, &length);
    if (p)
        CloseHandle(p);
    std::wstring name = exe;
    size_t slash = name.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        name = name.substr(slash + 1);
    if (!valid || !same(query.exe, name))
        return TRUE;
    if (query.title == title) {
        query.exact = window;
        return FALSE;
    }
    if (window == GetAncestor(GetForegroundWindow(), GA_ROOT))
        query.foreground = window;
    if (!query.fallback)
        query.fallback = window;
    return TRUE;
}
inline bool window_rect(ObsApi &api, obs_source_t *source, obs_data_t *settings, RECT &rect, HWND &chosen) {
    std::string selector = api.obs_data_get_string(settings, "window");
    size_t last = selector.rfind(':');
    if (last == std::string::npos)
        return false;
    size_t middle = selector.rfind(':', last - 1);
    if (middle == std::string::npos)
        return false;
    WindowSearch query{wide(decode(selector.substr(0, middle))),
                       wide(decode(selector.substr(middle + 1, last - middle - 1))),
                       wide(decode(selector.substr(last + 1)))};
    EnumWindows(window_visit, reinterpret_cast<LPARAM>(&query));
    HWND window = query.exact ? query.exact : (query.foreground ? query.foreground : query.fallback);
    if (!window)
        return false;
    chosen = window;
    RECT client{}, frame{};
    if (!GetClientRect(window, &client))
        return false;
    POINT origin{0, 0};
    ClientToScreen(window, &origin);
    OffsetRect(&client, origin.x, origin.y);
    if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame))))
        GetWindowRect(window, &frame);
    auto error = [&](const RECT &r) {
        return std::abs(int(api.obs_source_get_width(source)) - (r.right - r.left)) +
               std::abs(int(api.obs_source_get_height(source)) - (r.bottom - r.top));
    };
    rect = error(client) <= error(frame) ? client : frame;
    return rect.right > rect.left && rect.bottom > rect.top;
}
struct MonitorSearch {
    std::wstring selector;
    int wanted = 0, index = 0;
    HMONITOR exact = nullptr, index_match = nullptr, primary = nullptr;
};
inline BOOL CALLBACK monitor_visit(HMONITOR monitor, HDC, LPRECT, LPARAM opaque) {
    auto &query = *reinterpret_cast<MonitorSearch *>(opaque);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(monitor, &info);
    if (info.dwFlags & MONITORINFOF_PRIMARY)
        query.primary = monitor;
    if (query.index++ == query.wanted)
        query.index_match = monitor;
    DISPLAY_DEVICEW device{};
    device.cb = sizeof(device);
    if (EnumDisplayDevicesW(info.szDevice, 0, &device, EDD_GET_DEVICE_INTERFACE_NAME) &&
        same(query.selector, device.DeviceID))
        query.exact = monitor;
    return TRUE;
}
inline bool monitor_rect(ObsApi &api, obs_data_t *settings, RECT &rect) {
    MonitorSearch query;
    query.selector = wide(api.obs_data_get_string(settings, "monitor_id"));
    if (query.selector.empty())
        query.selector = wide(api.obs_data_get_string(settings, "monitor_name"));
    query.wanted = int(api.obs_data_get_int(settings, "monitor"));
    EnumDisplayMonitors(nullptr, nullptr, monitor_visit, reinterpret_cast<LPARAM>(&query));
    auto monitor = query.exact ? query.exact : (query.index_match ? query.index_match : query.primary);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!monitor || !GetMonitorInfoW(monitor, &info))
        return false;
    rect = info.rcMonitor;
    return true;
}
struct Step {
    ObsMatrix4 matrix{};
    ObsCrop crop{};
    uint32_t width = 0, height = 0;
};
struct Capture {
    RECT physical{};
    HWND window = nullptr;
    std::vector<Step> steps;
    uint32_t width = 0, height = 0;
    std::string name;
};
struct Search {
    ObsApi *api;
    std::string target;
    std::vector<Step> parent;
    std::vector<Capture> captures;
    unsigned depth = 0;
};
inline bool visit(obs_scene_t *, obs_sceneitem_t *item, void *opaque) {
    auto &search = *static_cast<Search *>(opaque);
    auto &api = *search.api;
    if (!api.obs_sceneitem_visible(item) || search.depth > 10)
        return true;
    auto *source = api.obs_sceneitem_get_source(item);
    if (!source)
        return true;
    Step step;
    api.obs_sceneitem_get_draw_transform(item, &step.matrix);
    api.obs_sceneitem_get_crop(item, &step.crop);
    step.width = api.obs_source_get_width(source);
    step.height = api.obs_source_get_height(source);
    auto parent = search.parent;
    parent.push_back(step);
    auto *child = api.obs_scene_from_source(source);
    if (!child)
        child = api.obs_sceneitem_group_get_scene(item);
    if (child) {
        Search nested{&api, search.target, parent, {}, search.depth + 1};
        api.obs_scene_enum_items(child, visit, &nested);
        search.captures.insert(search.captures.end(), nested.captures.begin(), nested.captures.end());
        return true;
    }
    std::string name = api.obs_source_get_name(source);
    if (!search.target.empty() && search.target != name)
        return true;
    std::string kind = api.obs_source_get_unversioned_id(source);
    if (kind != "window_capture" && kind != "monitor_capture")
        return true;
    auto *settings = api.obs_source_get_settings(source);
    Capture capture;
    capture.steps = parent;
    capture.width = step.width;
    capture.height = step.height;
    capture.name = name;
    bool valid = kind == "window_capture"
                     ? window_rect(api, source, settings, capture.physical, capture.window)
                     : monitor_rect(api, settings, capture.physical);
    api.obs_data_release(settings);
    if (valid && capture.width && capture.height)
        search.captures.push_back(capture);
    return true;
}
inline std::vector<Capture> captures(ObsApi &api, const std::string &target) {
    std::vector<Capture> result;
    auto *output = api.obs_get_output_source(0);
    if (!output)
        return result;
    auto *source = output;
    auto *scene = api.obs_scene_from_source(source);
    if (!scene) {
        source = api.obs_transition_get_active_source(output);
        if (source)
            scene = api.obs_scene_from_source(source);
    }
    if (scene) {
        Search search{&api, target, {}, {}, 0};
        api.obs_scene_enum_items(scene, visit, &search);
        result = std::move(search.captures);
    }
    if (source && source != output)
        api.obs_source_release(source);
    api.obs_source_release(output);
    return result;
}
inline bool point(const std::vector<Capture> &captures, POINT physical, uint32_t canvas_width,
                  uint32_t canvas_height, float &x, float &y) {
    for (auto it = captures.rbegin(); it != captures.rend(); ++it) {
        const auto &cap = *it;
        if (!PtInRect(&cap.physical, physical))
            continue;
        if (cap.window && GetAncestor(WindowFromPoint(physical), GA_ROOT) != cap.window)
            continue;
        float px =
            float(physical.x - cap.physical.left) * cap.width / (cap.physical.right - cap.physical.left);
        float py =
            float(physical.y - cap.physical.top) * cap.height / (cap.physical.bottom - cap.physical.top);
        bool valid = true;
        for (auto step = cap.steps.rbegin(); step != cap.steps.rend(); ++step) {
            if (px < step->crop.left || py < step->crop.top || px > float(step->width) - step->crop.right ||
                py > float(step->height) - step->crop.bottom) {
                valid = false;
                break;
            }
            px -= step->crop.left;
            py -= step->crop.top;
            float nx = px * step->matrix.x.x + py * step->matrix.y.x + step->matrix.t.x;
            float ny = px * step->matrix.x.y + py * step->matrix.y.y + step->matrix.t.y;
            px = nx;
            py = ny;
        }
        if (valid && px >= 0 && py >= 0 && px < float(canvas_width) && py < float(canvas_height)) {
            x = px / canvas_width;
            y = py / canvas_height;
            return true;
        }
    }
    return false;
}
} // namespace cursor_map
