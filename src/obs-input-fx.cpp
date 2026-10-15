#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "cursor-map.hpp"
#include "obs-api.hpp"
#include "resource.h"
#include <algorithm>
#include <atomic>
#include <bcrypt.h>
#include <cctype>
#include <chrono>
#include <commctrl.h>
#include <commdlg.h>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <wincrypt.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

namespace fs = std::filesystem;
static HINSTANCE instance;
static ObsApi api;
static obs_module_t *module = nullptr;
static obs_source_t *renderer = nullptr;
static obs_source_t *cursor_renderer = nullptr;
static obs_hotkey_id hotkey = ~size_t(0);
static std::string config_path, page_path, token;
static std::mutex config_mutex, queue_mutex;
static std::deque<std::string> packets;
static std::atomic<bool> running{false}, show{true}, show_mouse{true}, all_keys{false};
static std::atomic<uint64_t> key_events{0}, mouse_events{0}, frames{0};
static std::thread server_thread, input_thread;
static std::atomic<DWORD> input_thread_id{0};
static SOCKET listener = INVALID_SOCKET;
static unsigned short port = 0;
static bool render_registered = false, verify_demo = false;
static UINT_PTR verification_timer = 0;
static bool frontend_registered = false;
static UINT_PTR cursor_timer = 0;
static std::atomic<uint32_t> canvas_width{1920}, canvas_height{1080};
static std::atomic<bool> rings_enabled{true};
struct Click {
    POINT point;
    unsigned button;
    ULONGLONG at;
};
static std::mutex click_mutex;
static std::deque<Click> clicks;
struct Config {
    bool enabled = true, mouse = true, all = false, ring = true, always = false;
    int opacity = 62, scale = 100, duration = 1800, x = 12, y = 24, anchor = 0, ring_size = 44,
        ring_width = 2, ring_duration = 550, ring_opacity = 80;
    std::string accent = "72edf2", ring_color = "72edf2", capture;
};
static Config config;
static Config snapshot() {
    std::lock_guard<std::mutex> l(config_mutex);
    return config;
}
static void publish() {
    auto c = snapshot();
    show = c.enabled;
    show_mouse = c.mouse;
    all_keys = c.all;
    rings_enabled = c.ring;
}
static std::string uri(const std::string &path) {
    std::string out = "file:///";
    const char *h = "0123456789ABCDEF";
    for (unsigned char c : path) {
        if (c == '\\')
            out += '/';
        else if (std::isalnum(c) || c == '/' || c == ':' || c == '-' || c == '_' || c == '.' || c == '~')
            out += char(c);
        else {
            out += '%';
            out += h[c >> 4];
            out += h[c & 15];
        }
    }
    return out;
}
static void defaults(obs_data_t *d) {
    api.obs_data_set_default_bool(d, "enabled", true);
    api.obs_data_set_default_bool(d, "mouse", true);
    api.obs_data_set_default_bool(d, "all_keys", false);
    api.obs_data_set_default_int(d, "opacity", 62);
    api.obs_data_set_default_int(d, "scale", 100);
    api.obs_data_set_default_int(d, "duration", 1800);
    api.obs_data_set_default_int(d, "margin_x", 12);
    api.obs_data_set_default_int(d, "margin_y", 24);
    api.obs_data_set_default_int(d, "anchor", 0);
    api.obs_data_set_default_string(d, "accent", "72edf2");
    api.obs_data_set_default_bool(d, "ring", true);
    api.obs_data_set_default_bool(d, "ring_always", false);
    api.obs_data_set_default_int(d, "ring_size", 44);
    api.obs_data_set_default_int(d, "ring_width", 2);
    api.obs_data_set_default_int(d, "ring_duration", 550);
    api.obs_data_set_default_int(d, "ring_opacity", 80);
    api.obs_data_set_default_string(d, "ring_color", "72edf2");
    api.obs_data_set_default_string(d, "capture", "");
}
static obs_data_t *read_config() {
    auto *d = api.obs_data_create_from_json_file_safe(config_path.c_str(), "bak");
    if (!d)
        d = api.obs_data_create();
    defaults(d);
    return d;
}
static bool valid_color(const std::string &value) {
    return value.size() == 6 &&
           std::all_of(value.begin(), value.end(), [](unsigned char ch) { return std::isxdigit(ch) != 0; });
}
static void load_config() {
    auto *d = read_config();
    Config c;
    c.enabled = api.obs_data_get_bool(d, "enabled");
    c.mouse = api.obs_data_get_bool(d, "mouse");
    c.all = api.obs_data_get_bool(d, "all_keys");
    auto integer = [&](const char *key, int low, int high) {
        return std::clamp(int(api.obs_data_get_int(d, key)), low, high);
    };
    c.opacity = integer("opacity", 0, 100);
    c.scale = integer("scale", 50, 180);
    c.duration = integer("duration", 400, 8000);
    c.x = integer("margin_x", 0, 600);
    c.y = integer("margin_y", 0, 600);
    c.anchor = integer("anchor", 0, 2);
    c.accent = api.obs_data_get_string(d, "accent");
    if (!valid_color(c.accent))
        c.accent = "72edf2";
    c.ring = api.obs_data_get_bool(d, "ring");
    c.always = api.obs_data_get_bool(d, "ring_always");
    c.ring_size = integer("ring_size", 12, 120);
    c.ring_width = integer("ring_width", 1, 10);
    c.ring_duration = integer("ring_duration", 150, 2000);
    c.ring_opacity = integer("ring_opacity", 0, 100);
    c.ring_color = api.obs_data_get_string(d, "ring_color");
    if (!valid_color(c.ring_color))
        c.ring_color = c.accent;
    c.capture = api.obs_data_get_string(d, "capture");
    verify_demo = api.obs_data_get_bool(d, "verification_demo");
    {
        std::lock_guard<std::mutex> l(config_mutex);
        config = c;
    }
    publish();
    api.obs_data_release(d);
}
static void save_config() {
    auto c = snapshot();
    auto *d = api.obs_data_create();
    api.obs_data_set_bool(d, "enabled", c.enabled);
    api.obs_data_set_bool(d, "mouse", c.mouse);
    api.obs_data_set_bool(d, "all_keys", c.all);
    api.obs_data_set_bool(d, "verification_demo", false);
    api.obs_data_set_int(d, "opacity", c.opacity);
    api.obs_data_set_int(d, "scale", c.scale);
    api.obs_data_set_int(d, "duration", c.duration);
    api.obs_data_set_int(d, "margin_x", c.x);
    api.obs_data_set_int(d, "margin_y", c.y);
    api.obs_data_set_int(d, "anchor", c.anchor);
    api.obs_data_set_string(d, "accent", c.accent.c_str());
    api.obs_data_set_bool(d, "ring", c.ring);
    api.obs_data_set_bool(d, "ring_always", c.always);
    api.obs_data_set_int(d, "ring_size", c.ring_size);
    api.obs_data_set_int(d, "ring_width", c.ring_width);
    api.obs_data_set_int(d, "ring_duration", c.ring_duration);
    api.obs_data_set_int(d, "ring_opacity", c.ring_opacity);
    api.obs_data_set_string(d, "ring_color", c.ring_color.c_str());
    api.obs_data_set_string(d, "capture", c.capture.c_str());
    if (hotkey != ~size_t(0)) {
        auto *a = api.obs_hotkey_save(hotkey);
        api.obs_data_set_array(d, "toggle_hotkey", a);
        api.obs_data_array_release(a);
    }
    api.obs_data_save_json_safe(d, config_path.c_str(), "tmp", "bak");
    api.obs_data_release(d);
}

static std::string base64(const unsigned char *bytes, DWORD length) {
    DWORD n = 0;
    CryptBinaryToStringA(bytes, length, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &n);
    std::string result(n, 0);
    CryptBinaryToStringA(bytes, length, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, result.data(), &n);
    while (!result.empty() && !result.back())
        result.pop_back();
    return result;
}
static std::string accept_key(const std::string &key) {
    HCRYPTPROV p = 0;
    HCRYPTHASH h = 0;
    unsigned char hash[20];
    DWORD n = 20;
    std::string input = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", result;
    if (CryptAcquireContextW(&p, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT) &&
        CryptCreateHash(p, CALG_SHA1, 0, 0, &h) &&
        CryptHashData(h, reinterpret_cast<const BYTE *>(input.data()), DWORD(input.size()), 0) &&
        CryptGetHashParam(h, HP_HASHVAL, hash, &n, 0))
        result = base64(hash, n);
    if (h)
        CryptDestroyHash(h);
    if (p)
        CryptReleaseContext(p, 0);
    return result;
}
static bool send_all(SOCKET s, const std::string &data) {
    size_t pos = 0;
    while (pos < data.size()) {
        int n = send(s, data.data() + pos, int(data.size() - pos), 0);
        if (n <= 0)
            return false;
        pos += n;
    }
    return true;
}
static std::string frame(const std::string &text, unsigned char opcode = 1) {
    std::string out;
    out += char(0x80 | opcode);
    size_t n = text.size();
    if (n < 126)
        out += char(n);
    else {
        out += char(126);
        out += char((n >> 8) & 255);
        out += char(n & 255);
    }
    out += text;
    return out;
}
static std::string header(const std::string &text, const std::string &wanted) {
    size_t pos = text.find("\r\n") + 2;
    while (pos < text.size()) {
        size_t end = text.find("\r\n", pos);
        if (end == std::string::npos || end == pos)
            break;
        auto line = text.substr(pos, end - pos);
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            auto name = line.substr(0, colon);
            std::transform(name.begin(), name.end(), name.begin(),
                           [](unsigned char c) { return char(std::tolower(c)); });
            if (name == wanted) {
                auto value = line.substr(colon + 1);
                size_t start = value.find_first_not_of(" \t");
                return start == std::string::npos ? "" : value.substr(start);
            }
        }
        pos = end + 2;
    }
    return {};
}
static bool handshake(SOCKET s) {
    DWORD timeout = 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));
    std::string req;
    char buf[2048];
    while (req.find("\r\n\r\n") == std::string::npos && req.size() < 8192) {
        int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0)
            return false;
        req.append(buf, n);
    }
    auto first = "GET /events/" + token + " HTTP/1.1\r\n";
    if (req.rfind(first, 0) != 0 || header(req, "sec-websocket-version") != "13" ||
        header(req, "sec-websocket-key").empty()) {
        send_all(s, "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n");
        return false;
    }
    auto response = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: "
                    "Upgrade\r\nSec-WebSocket-Accept: " +
                    accept_key(header(req, "sec-websocket-key")) + "\r\n\r\n";
    return send_all(s, response);
}
struct Client {
    SOCKET socket;
    std::string buffer;
};
static bool consume(Client &c) {
    char buffer[2048];
    int n = recv(c.socket, buffer, sizeof(buffer), 0);
    if (n == 0)
        return false;
    if (n < 0)
        return WSAGetLastError() == WSAEWOULDBLOCK;
    c.buffer.append(buffer, n);
    while (c.buffer.size() >= 2) {
        const auto *p = reinterpret_cast<const unsigned char *>(c.buffer.data());
        unsigned opcode = p[0] & 15;
        bool masked = (p[1] & 128) != 0;
        uint64_t len = p[1] & 127;
        size_t off = 2;
        if (len == 126) {
            if (c.buffer.size() < 4)
                break;
            len = (uint64_t(p[2]) << 8) | p[3];
            off = 4;
        } else if (len == 127)
            return false;
        if (len > 4096)
            return false;
        size_t mask_off = off;
        if (masked)
            off += 4;
        if (c.buffer.size() < off + len)
            break;
        std::string payload = c.buffer.substr(off, size_t(len));
        if (masked)
            for (size_t i = 0; i < len; i++)
                payload[i] = char(static_cast<unsigned char>(payload[i]) ^ p[mask_off + i % 4]);
        if (opcode == 8)
            return false;
        if (opcode == 9 && !send_all(c.socket, frame(payload, 10)))
            return false;
        c.buffer.erase(0, off + size_t(len));
    }
    return true;
}
static void server_loop() {
    std::vector<Client> clients;
    while (running) {
        fd_set reads;
        FD_ZERO(&reads);
        FD_SET(listener, &reads);
        for (auto &c : clients)
            FD_SET(c.socket, &reads);
        timeval wait{0, 16000};
        int selected = select(0, &reads, nullptr, nullptr, &wait);
        if (!running)
            break;
        if (selected < 0)
            break;
        if (FD_ISSET(listener, &reads)) {
            SOCKET s = accept(listener, nullptr, nullptr);
            if (s != INVALID_SOCKET) {
                if (clients.size() < 4 && handshake(s)) {
                    u_long mode = 1;
                    ioctlsocket(s, FIONBIO, &mode);
                    clients.push_back({s, {}});
                } else
                    closesocket(s);
            }
        }
        for (size_t i = 0; i < clients.size();) {
            auto &c = clients[i];
            if (FD_ISSET(c.socket, &reads) && !consume(c)) {
                closesocket(c.socket);
                clients.erase(clients.begin() + i);
            } else
                ++i;
        }
        std::deque<std::string> outgoing;
        {
            std::lock_guard<std::mutex> l(queue_mutex);
            outgoing.swap(packets);
        }
        for (const auto &packet : outgoing) {
            auto data = frame(packet);
            for (size_t i = 0; i < clients.size();) {
                if (!send_all(clients[i].socket, data)) {
                    closesocket(clients[i].socket);
                    clients.erase(clients.begin() + i);
                } else
                    ++i;
            }
        }
    }
    for (auto &c : clients)
        closesocket(c.socket);
}
static bool start_server() {
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data))
        return false;
    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) || listen(listener, 4)) {
        closesocket(listener);
        listener = INVALID_SOCKET;
        WSACleanup();
        return false;
    }
    int length = sizeof(address);
    getsockname(listener, reinterpret_cast<sockaddr *>(&address), &length);
    port = ntohs(address.sin_port);
    unsigned char random[24];
    if (BCryptGenRandom(nullptr, random, sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        closesocket(listener);
        listener = INVALID_SOCKET;
        WSACleanup();
        return false;
    }
    const char *hex = "0123456789abcdef";
    for (auto c : random) {
        token += hex[c >> 4];
        token += hex[c & 15];
    }
    running = true;
    server_thread = std::thread(server_loop);
    return true;
}
static void enqueue(const std::string &packet) {
    if (!show || !running)
        return;
    std::lock_guard<std::mutex> l(queue_mutex);
    if (packets.size() < 64)
        packets.push_back(packet);
}
static unsigned modifiers() {
    unsigned mask = 0;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
        mask |= 1;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
        mask |= 2;
    if ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000)
        mask |= 4;
    if (GetAsyncKeyState(VK_MENU) & 0x8000)
        mask |= 8;
    return mask;
}
static bool is_modifier(DWORD vk) {
    return vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN ||
           (vk >= VK_LSHIFT && vk <= VK_RMENU);
}
static LRESULT CALLBACK keyboard_hook(int code, WPARAM message, LPARAM param) {
    if (code == HC_ACTION && (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && show) {
        const auto *k = reinterpret_cast<const KBDLLHOOKSTRUCT *>(param);
        unsigned mask = modifiers();
        if (!is_modifier(k->vkCode) &&
            (all_keys || mask || k->vkCode < 48 || (k->vkCode >= VK_F1 && k->vkCode <= VK_F24))) {
            char packet[160];
            std::snprintf(packet, sizeof(packet),
                          "{\"event_type\":\"key_pressed\",\"rawcode\":%lu,\"mask\":%u}", k->vkCode, mask);
            enqueue(packet);
            key_events++;
        }
    }
    return CallNextHookEx(nullptr, code, message, param);
}
static LRESULT CALLBACK mouse_hook(int code, WPARAM message, LPARAM param) {
    if (code == HC_ACTION && show) {
        const auto *m = reinterpret_cast<const MSLLHOOKSTRUCT *>(param);
        unsigned button = 0;
        if (message == WM_LBUTTONDOWN)
            button = 1;
        else if (message == WM_RBUTTONDOWN)
            button = 2;
        else if (message == WM_MBUTTONDOWN)
            button = 3;
        else if (message == WM_XBUTTONDOWN)
            button = HIWORD(m->mouseData) == XBUTTON1 ? 4 : 5;
        if (button && rings_enabled) {
            std::lock_guard<std::mutex> l(click_mutex);
            if (clicks.size() < 16)
                clicks.push_back({m->pt, button, GetTickCount64()});
        }
        char packet[160];
        if (button && show_mouse) {
            std::snprintf(packet, sizeof(packet),
                          "{\"event_type\":\"mouse_pressed\",\"button\":%u,\"mask\":%u}", button,
                          modifiers());
            enqueue(packet);
            mouse_events++;
        } else if (message == WM_MOUSEWHEEL && show_mouse) {
            int delta = static_cast<short>(HIWORD(m->mouseData));
            std::snprintf(packet, sizeof(packet),
                          "{\"event_type\":\"mouse_wheel\",\"rotation\":%d,\"mask\":%u}", delta > 0 ? -1 : 1,
                          modifiers());
            enqueue(packet);
            mouse_events++;
        }
    }
    return CallNextHookEx(nullptr, code, message, param);
}
static void input_loop() {
    MSG message;
    PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    input_thread_id = GetCurrentThreadId();
    HHOOK keys = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard_hook, instance, 0),
          mouse = SetWindowsHookExW(WH_MOUSE_LL, mouse_hook, instance, 0);
    if (api.blog)
        api.blog(keys && mouse ? 300 : 100, "[InputFX] Native input hooks: keyboard=%s mouse=%s",
                 keys ? "ready" : "failed", mouse ? "ready" : "failed");
    while (running && GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (keys)
        UnhookWindowsHookEx(keys);
    if (mouse)
        UnhookWindowsHookEx(mouse);
    input_thread_id = 0;
}

static std::string renderer_url(bool cursor = false) {
    auto c = snapshot();
    return uri(page_path) + "?port=" + std::to_string(port) + "&token=" + token +
           "&opacity=" + std::to_string(c.opacity) + "&accent=" + c.accent +
           "&duration=" + std::to_string(c.duration) + "&mouse=" + (c.mouse ? "1" : "0") +
           "&all=" + (c.all ? "1" : "0") + "&mode=" + (cursor ? "cursor" : "keys") +
           "&ring=" + (c.ring ? "1" : "0") + "&always=" + (c.always ? "1" : "0") +
           "&ringColor=" + c.ring_color + "&ringSize=" + std::to_string(c.ring_size) +
           "&ringWidth=" + std::to_string(c.ring_width) + "&ringDuration=" + std::to_string(c.ring_duration) +
           "&ringOpacity=" + std::to_string(c.ring_opacity);
}
static void update_renderer() {
    for (auto pair : {std::pair<obs_source_t *, bool>(renderer, false), {cursor_renderer, true}}) {
        if (!pair.first)
            continue;
        auto *d = api.obs_data_create();
        auto url = renderer_url(pair.second);
        api.obs_data_set_string(d, "url", url.c_str());
        api.obs_source_update(pair.first, d);
        api.obs_data_release(d);
    }
}
static void before_render(void *, uint32_t width, uint32_t height) {
    canvas_width = width;
    canvas_height = height;
}
static void draw(void *, uint32_t width, uint32_t height) {
    if (!renderer || !show)
        return;
    auto c = snapshot();
    float unit = width / 1920.f;
    float scale = std::min({c.scale / 100.f * unit, width / 1100.f, height / 160.f});
    float x = c.x * unit;
    if (c.anchor == 1)
        x = (width - 1100 * scale) / 2;
    else if (c.anchor == 2)
        x = width - 1100 * scale - c.x * unit;
    x = std::max(0.f, std::min(x, width - 1100 * scale));
    float y = std::max(0.f, height - 160 * scale - c.y * unit);
    api.gs_blend_state_push();
    api.gs_blend_function(1, 5);
    if (cursor_renderer && c.ring) {
        api.gs_matrix_push();
        api.obs_source_video_render(cursor_renderer);
        api.gs_matrix_pop();
    }
    api.gs_matrix_push();
    api.gs_matrix_translate3f(x, y, 0);
    api.gs_matrix_scale3f(scale, scale, 1);
    api.obs_source_video_render(renderer);
    api.gs_matrix_pop();
    api.gs_blend_state_pop();
    frames++;
}
static void after_render(void *) {
    draw(nullptr, canvas_width, canvas_height);
}
static void CALLBACK cursor_tick(HWND, UINT, UINT_PTR, DWORD) {
    if (!cursor_renderer || !show)
        return;
    static uint32_t last_width = 0, last_height = 0;
    auto width = canvas_width.load(), height = canvas_height.load();
    if (width != last_width || height != last_height) {
        auto *d = api.obs_data_create();
        api.obs_data_set_int(d, "width", width);
        api.obs_data_set_int(d, "height", height);
        api.obs_source_update(cursor_renderer, d);
        api.obs_data_release(d);
        last_width = width;
        last_height = height;
    }
    auto c = snapshot();
    if (!c.ring)
        return;
    auto captures = cursor_map::captures(api, c.capture);
    POINT physical{};
    GetCursorPos(&physical);
    float x = 0, y = 0;
    bool visible = cursor_map::point(captures, physical, width, height, x, y);
    char packet[192];
    std::snprintf(packet, sizeof(packet),
                  "{\"event_type\":\"cursor_position\",\"visible\":%s,\"x\":%.6f,\"y\":%.6f}",
                  visible ? "true" : "false", x, y);
    enqueue(packet);
    std::deque<Click> pending;
    {
        std::lock_guard<std::mutex> l(click_mutex);
        pending.swap(clicks);
    }
    for (auto &click : pending) {
        if (GetTickCount64() - click.at > 1200)
            continue;
        if (cursor_map::point(captures, click.point, width, height, x, y)) {
            std::snprintf(packet, sizeof(packet),
                          "{\"event_type\":\"cursor_click\",\"button\":%u,\"x\":%.6f,\"y\":%.6f}",
                          click.button, x, y);
            enqueue(packet);
        }
    }
}
static void toggle(void *, obs_hotkey_id, void *, bool pressed) {
    if (!pressed)
        return;
    {
        std::lock_guard<std::mutex> l(config_mutex);
        config.enabled = !config.enabled;
    }
    publish();
    save_config();
}
#include "settings-ui.hpp"

static void settings_menu(void *) {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);
    HWND parent = GetAncestor(GetForegroundWindow(), GA_ROOT);
    DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SETTINGS), parent, dialog_proc, 0);
}
static void CALLBACK verification_tick(HWND, UINT, UINT_PTR timer, DWORD) {
    static unsigned step = 0;
    ++step;
    if (!renderer)
        return;
    if (step == 1 || step == 4 || step == 8)
        enqueue("{\"event_type\":\"key_pressed\",\"rawcode\":90,\"mask\":3}");
    if (step == 2)
        enqueue("{\"event_type\":\"mouse_pressed\",\"button\":2,\"mask\":0}");
    if (step == 5 || step == 9) {
        api.obs_frontend_take_screenshot();
        if (api.blog)
            api.blog(300, "[InputFX] Verification: final-canvas screenshot %u requested; frames=%llu",
                     step == 5 ? 1u : 2u, static_cast<unsigned long long>(frames.load()));
    }
    if (step >= 10) {
        KillTimer(nullptr, timer);
        verification_timer = 0;
        verify_demo = false;
        save_config();
        if (api.blog)
            api.blog(300, "[InputFX] Verification finished; live input only");
    }
}
static void shutdown_resources() {
    if (cursor_timer) {
        KillTimer(nullptr, cursor_timer);
        cursor_timer = 0;
    }
    if (verification_timer) {
        KillTimer(nullptr, verification_timer);
        verification_timer = 0;
    }
    if (render_registered) {
        api.obs_remove_main_render_callback(before_render, nullptr);
        api.obs_remove_main_rendered_callback(after_render, nullptr);
        render_registered = false;
    }
    if (cursor_renderer) {
        api.obs_source_dec_showing(cursor_renderer);
        api.obs_source_dec_active(cursor_renderer);
        api.obs_source_release(cursor_renderer);
        cursor_renderer = nullptr;
    }
    if (renderer) {
        api.obs_source_dec_showing(renderer);
        api.obs_source_dec_active(renderer);
        api.obs_source_release(renderer);
        renderer = nullptr;
    }
    if (hotkey != ~size_t(0)) {
        save_config();
        api.obs_hotkey_unregister(hotkey);
        hotkey = ~size_t(0);
    }
    bool was_running = running.exchange(false);
    if (input_thread_id)
        PostThreadMessageW(input_thread_id, WM_QUIT, 0, 0);
    if (listener != INVALID_SOCKET) {
        shutdown(listener, SD_BOTH);
        closesocket(listener);
        listener = INVALID_SOCKET;
    }
    if (server_thread.joinable())
        server_thread.join();
    if (input_thread.joinable())
        input_thread.join();
    if (was_running)
        WSACleanup();
}
static void frontend_event(int event, void *) {
    if (event == 17) {
        shutdown_resources();
        frontend_registered = false;
        if (api.blog)
            api.blog(300, "[InputFX] Released renderer and input hooks before OBS browser shutdown");
    }
}

extern "C" __declspec(dllexport) void obs_module_set_pointer(obs_module_t *value) {
    module = value;
}
extern "C" __declspec(dllexport) uint32_t obs_module_ver() {
    return 0x20000000;
}
extern "C" __declspec(dllexport) const char *obs_module_name() {
    return "InputFX";
}
extern "C" __declspec(dllexport) const char *obs_module_description() {
    return "Clean cyberpunk input effects, automatically composited over every scene.";
}
extern "C" __declspec(dllexport) const char *obs_module_author() {
    return "Axura";
}
extern "C" __declspec(dllexport) bool obs_module_load() {
    if (!api.bind())
        return false;
    char *folder = api.obs_module_get_config_path(module, "");
    if (!folder)
        return false;
    fs::path dir = fs::u8path(folder);
    api.bfree(folder);
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec)
        return false;
    config_path =
        (dir / "settings.json").u8string(); /* Preserve settings from the pre-publication plugin name. */
    const auto legacy = dir.parent_path() / "cyber-input" / "settings.json";
    if (!fs::exists(fs::u8path(config_path)) && fs::exists(legacy)) {
        fs::copy_file(legacy, fs::u8path(config_path), fs::copy_options::none, ec);
        if (ec && api.blog)
            api.blog(200, "[InputFX] Previous settings could not be migrated; using defaults");
    }
    page_path = (dir / "overlay.html").u8string();
    HRSRC res = FindResourceW(instance, MAKEINTRESOURCEW(IDR_OVERLAY), RT_RCDATA);
    if (!res)
        return false;
    HGLOBAL resource = LoadResource(instance, res);
    const char *bytes = reinterpret_cast<const char *>(LockResource(resource));
    std::ofstream page(fs::u8path(page_path), std::ios::binary | std::ios::trunc);
    page.write(bytes, SizeofResource(instance, res));
    page.close();
    load_config();
    if (!start_server()) {
        if (api.blog)
            api.blog(100, "[InputFX] Unable to start private input transport");
        return false;
    }
    input_thread = std::thread(input_loop);
    hotkey =
        api.obs_hotkey_register_frontend("cyber_input.toggle" /* retained for existing hotkey bindings */,
                                         "InputFX: show/hide overlay", toggle, nullptr);
    auto *d = read_config();
    auto *a = api.obs_data_get_array(d, "toggle_hotkey");
    api.obs_hotkey_load(hotkey, a);
    api.obs_data_array_release(a);
    api.obs_data_release(d);
    api.obs_frontend_add_tools_menu_item("InputFX...", settings_menu, nullptr);
    api.obs_frontend_add_event_callback(frontend_event, nullptr);
    frontend_registered = true;
    if (api.blog)
        api.blog(300, "[InputFX] Loaded native plugin 1.1.0; independent input capture and global rendering");
    return true;
}
extern "C" __declspec(dllexport) void obs_module_post_load() {
    if (!running)
        return;
    auto *d = api.obs_data_create();
    auto url = renderer_url();
    api.obs_data_set_bool(d, "is_local_file", false);
    api.obs_data_set_string(d, "url", url.c_str());
    api.obs_data_set_int(d, "width", 1100);
    api.obs_data_set_int(d, "height", 160);
    api.obs_data_set_int(d, "fps", 60);
    api.obs_data_set_bool(d, "shutdown", false);
    renderer = api.obs_source_create_private("browser_source", "InputFX internal renderer", d);
    api.obs_data_release(d);
    if (renderer) {
        api.obs_source_inc_active(renderer);
        api.obs_source_inc_showing(renderer);
        api.obs_add_main_render_callback(before_render, nullptr);
        api.obs_add_main_rendered_callback(after_render, nullptr);
        render_registered = true;
        auto *cursor_settings = api.obs_data_create();
        auto cursor_url = renderer_url(true);
        api.obs_data_set_bool(cursor_settings, "is_local_file", false);
        api.obs_data_set_string(cursor_settings, "url", cursor_url.c_str());
        api.obs_data_set_int(cursor_settings, "width", canvas_width);
        api.obs_data_set_int(cursor_settings, "height", canvas_height);
        api.obs_data_set_int(cursor_settings, "fps", 60);
        api.obs_data_set_bool(cursor_settings, "shutdown", false);
        cursor_renderer =
            api.obs_source_create_private("browser_source", "InputFX cursor renderer", cursor_settings);
        api.obs_data_release(cursor_settings);
        if (cursor_renderer) {
            api.obs_source_inc_active(cursor_renderer);
            api.obs_source_inc_showing(cursor_renderer);
            cursor_timer = SetTimer(nullptr, 0, 33, cursor_tick);
        }
        if (api.blog)
            api.blog(300, "[InputFX] Global renderer ready; no scene sources required");
        if (verify_demo)
            verification_timer = SetTimer(nullptr, 0, 1200, verification_tick);
    } else if (api.blog)
        api.blog(100, "[InputFX] OBS Browser renderer could not be created");
}
extern "C" __declspec(dllexport) void obs_module_unload() {
    if (frontend_registered) {
        api.obs_frontend_remove_event_callback(frontend_event, nullptr);
        frontend_registered = false;
    }
    shutdown_resources();
    if (api.blog)
        api.blog(300, "[InputFX] Unloaded cleanly; rendered frames=%llu input events=%llu/%llu",
                 static_cast<unsigned long long>(frames.load()),
                 static_cast<unsigned long long>(key_events.load()),
                 static_cast<unsigned long long>(mouse_events.load()));
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        instance = h;
        DisableThreadLibraryCalls(h);
    }
    return TRUE;
}
