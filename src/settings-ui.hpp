#pragma once
// Included after Config and renderer helpers; all callbacks run on the OBS UI thread.
static Config dialog_draft;
static COLORREF custom_colors[16] = {RGB(114, 237, 242), RGB(255, 128, 181), RGB(180, 140, 255),
                                     RGB(255, 196, 95),  RGB(145, 238, 144), RGB(255, 255, 255),
                                     RGB(75, 150, 255)};
static COLORREF rgb(const std::string &hex) {
    unsigned value = std::stoul(hex, nullptr, 16);
    return RGB((value >> 16) & 255, (value >> 8) & 255, value & 255);
}
static std::string hex_color(COLORREF color) {
    char out[7];
    std::snprintf(out, sizeof(out), "%02x%02x%02x", GetRValue(color), GetGValue(color), GetBValue(color));
    return out;
}
static std::string utf8_text(const std::wstring &text) {
    int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), out.data(), n, nullptr, nullptr);
    return out;
}
static void label_value(HWND dialog, int id, int value, const wchar_t *suffix) {
    wchar_t text[48];
    swprintf_s(text, L"%d%s", value, suffix);
    SetDlgItemTextW(dialog, id, text);
}
static int slider(HWND dialog, int id) {
    return int(SendDlgItemMessageW(dialog, id, TBM_GETPOS, 0, 0));
}
static void range(HWND dialog, int id, int low, int high, int value) {
    SendDlgItemMessageW(dialog, id, TBM_SETRANGE, TRUE, MAKELPARAM(low, high));
    SendDlgItemMessageW(dialog, id, TBM_SETPOS, TRUE, value);
}
static void update_labels(HWND dialog) {
    label_value(dialog, IDC_OPACITY_LABEL, slider(dialog, IDC_OPACITY), L"%");
    label_value(dialog, IDC_SCALE_LABEL, slider(dialog, IDC_SCALE), L"%");
    label_value(dialog, IDC_RING_SIZE_LABEL, slider(dialog, IDC_RING_SIZE), L" px");
    label_value(dialog, IDC_RING_WIDTH_LABEL, slider(dialog, IDC_RING_WIDTH), L" px");
    label_value(dialog, IDC_RING_DURATION_LABEL, slider(dialog, IDC_RING_DURATION), L" ms");
    label_value(dialog, IDC_RING_OPACITY_LABEL, slider(dialog, IDC_RING_OPACITY), L"%");
}
static bool enumerate_captures(void *opaque, obs_source_t *source) {
    HWND dialog = static_cast<HWND>(opaque);
    std::string kind = api.obs_source_get_unversioned_id(source);
    if (kind == "window_capture" || kind == "monitor_capture") {
        auto name = cursor_map::wide(api.obs_source_get_name(source));
        SendDlgItemMessageW(dialog, IDC_CAPTURE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
    }
    return true;
}
static void read_dialog(HWND dialog) {
    dialog_draft.enabled = IsDlgButtonChecked(dialog, IDC_ENABLED) == BST_CHECKED;
    dialog_draft.mouse = IsDlgButtonChecked(dialog, IDC_MOUSE) == BST_CHECKED;
    dialog_draft.all = IsDlgButtonChecked(dialog, IDC_ALL_KEYS) == BST_CHECKED;
    dialog_draft.ring = IsDlgButtonChecked(dialog, IDC_RING) == BST_CHECKED;
    dialog_draft.always = IsDlgButtonChecked(dialog, IDC_RING_ALWAYS) == BST_CHECKED;
    dialog_draft.opacity = 100 - slider(dialog, IDC_OPACITY);
    dialog_draft.scale = slider(dialog, IDC_SCALE);
    dialog_draft.duration = std::clamp(int(GetDlgItemInt(dialog, IDC_DURATION, nullptr, FALSE)), 400, 8000);
    dialog_draft.x = std::clamp(int(GetDlgItemInt(dialog, IDC_MARGIN_X, nullptr, FALSE)), 0, 600);
    dialog_draft.y = std::clamp(int(GetDlgItemInt(dialog, IDC_MARGIN_Y, nullptr, FALSE)), 0, 600);
    dialog_draft.anchor = std::clamp(int(SendDlgItemMessageW(dialog, IDC_ANCHOR, CB_GETCURSEL, 0, 0)), 0, 2);
    dialog_draft.ring_size = slider(dialog, IDC_RING_SIZE);
    dialog_draft.ring_width = slider(dialog, IDC_RING_WIDTH);
    dialog_draft.ring_duration = slider(dialog, IDC_RING_DURATION);
    dialog_draft.ring_opacity = 100 - slider(dialog, IDC_RING_OPACITY);
    int selected = int(SendDlgItemMessageW(dialog, IDC_CAPTURE, CB_GETCURSEL, 0, 0));
    dialog_draft.capture = "";
    if (selected > 0) {
        wchar_t text[1024]{};
        SendDlgItemMessageW(dialog, IDC_CAPTURE, CB_GETLBTEXT, selected, reinterpret_cast<LPARAM>(text));
        dialog_draft.capture = utf8_text(text);
    }
}
static INT_PTR CALLBACK dialog_proc(HWND dialog, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_INITDIALOG) {
        dialog_draft = snapshot();
        auto &c = dialog_draft;
        for (auto entry : {std::pair<int, bool>(IDC_ENABLED, c.enabled),
                           {IDC_MOUSE, c.mouse},
                           {IDC_ALL_KEYS, c.all},
                           {IDC_RING, c.ring},
                           {IDC_RING_ALWAYS, c.always}})
            CheckDlgButton(dialog, entry.first, entry.second ? BST_CHECKED : BST_UNCHECKED);
        range(dialog, IDC_OPACITY, 0, 100, 100 - c.opacity);
        range(dialog, IDC_SCALE, 50, 180, c.scale);
        range(dialog, IDC_RING_SIZE, 12, 120, c.ring_size);
        range(dialog, IDC_RING_WIDTH, 1, 10, c.ring_width);
        range(dialog, IDC_RING_DURATION, 150, 2000, c.ring_duration);
        range(dialog, IDC_RING_OPACITY, 0, 100, 100 - c.ring_opacity);
        SetDlgItemInt(dialog, IDC_DURATION, c.duration, FALSE);
        SetDlgItemInt(dialog, IDC_MARGIN_X, c.x, FALSE);
        SetDlgItemInt(dialog, IDC_MARGIN_Y, c.y, FALSE);
        for (auto name : {L"Left", L"Center", L"Right"})
            SendDlgItemMessageW(dialog, IDC_ANCHOR, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
        SendDlgItemMessageW(dialog, IDC_ANCHOR, CB_SETCURSEL, c.anchor, 0);
        SendDlgItemMessageW(dialog, IDC_CAPTURE, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(L"Auto (visible capture)"));
        api.obs_enum_sources(enumerate_captures, dialog);
        int selection = 0;
        if (!c.capture.empty()) {
            auto name = cursor_map::wide(c.capture);
            selection =
                int(SendDlgItemMessageW(dialog, IDC_CAPTURE, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
                                        reinterpret_cast<LPARAM>(name.c_str())));
            if (selection < 0)
                selection = 0;
        }
        SendDlgItemMessageW(dialog, IDC_CAPTURE, CB_SETCURSEL, selection, 0);
        update_labels(dialog);
        SetTimer(dialog, 1, 500, nullptr);
        return TRUE;
    }
    if (message == WM_TIMER) {
        if (wp == 2) {
            KillTimer(dialog, 2);
            enqueue("{\"event_type\":\"key_pressed\",\"rawcode\":67,\"mask\":2}");
            enqueue("{\"event_type\":\"cursor_click\",\"button\":1,\"x\":0.5,\"y\":0.5}");
            return TRUE;
        }
        wchar_t text[200];
        swprintf_s(text, L"Ready. Input events: %llu | Shortcuts shown by default",
                   static_cast<unsigned long long>(key_events.load() + mouse_events.load()));
        SetDlgItemTextW(dialog, IDC_STATUS, text);
        return TRUE;
    }
    if (message == WM_HSCROLL) {
        update_labels(dialog);
        return TRUE;
    }
    if (message == WM_DRAWITEM) {
        auto *item = reinterpret_cast<DRAWITEMSTRUCT *>(lp);
        if (item->CtlID != IDC_ACCENT && item->CtlID != IDC_RING_COLOR)
            return FALSE;
        FillRect(item->hDC, &item->rcItem, GetSysColorBrush(COLOR_BTNFACE));
        FrameRect(item->hDC, &item->rcItem, GetSysColorBrush(COLOR_BTNSHADOW));
        RECT swatch = item->rcItem;
        InflateRect(&swatch, -5, -5);
        swatch.right = swatch.left + 24;
        auto brush =
            CreateSolidBrush(rgb(item->CtlID == IDC_ACCENT ? dialog_draft.accent : dialog_draft.ring_color));
        FillRect(item->hDC, &swatch, brush);
        DeleteObject(brush);
        RECT text = item->rcItem;
        text.left += 38;
        SetBkMode(item->hDC, TRANSPARENT);
        SetTextColor(item->hDC, GetSysColor(COLOR_BTNTEXT));
        DrawTextW(item->hDC, L"Choose color...", -1, &text, DT_SINGLELINE | DT_VCENTER);
        if (item->itemState & ODS_FOCUS)
            DrawFocusRect(item->hDC, &item->rcItem);
        return TRUE;
    }
    if (message == WM_COMMAND) {
        int id = LOWORD(wp);
        if (id == IDC_ACCENT || id == IDC_RING_COLOR) {
            auto &color = id == IDC_ACCENT ? dialog_draft.accent : dialog_draft.ring_color;
            CHOOSECOLORW picker{};
            picker.lStructSize = sizeof(picker);
            picker.hwndOwner = dialog;
            picker.rgbResult = rgb(color);
            picker.lpCustColors = custom_colors;
            picker.Flags = CC_FULLOPEN | CC_RGBINIT;
            if (ChooseColorW(&picker)) {
                color = hex_color(picker.rgbResult);
                InvalidateRect(GetDlgItem(dialog, id), nullptr, TRUE);
            }
            return TRUE;
        }
        if (id == IDOK || id == IDC_TEST) {
            read_dialog(dialog);
            {
                std::lock_guard<std::mutex> l(config_mutex);
                config = dialog_draft;
            }
            publish();
            verify_demo = false;
            update_renderer();
            save_config();
            if (id == IDC_TEST) {
                SetTimer(dialog, 2, 500, nullptr);
            } else {
                KillTimer(dialog, 1);
                EndDialog(dialog, IDOK);
            }
            return TRUE;
        }
        if (id == IDCANCEL) {
            KillTimer(dialog, 1);
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
    }
    if (message == WM_CLOSE) {
        KillTimer(dialog, 1);
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}
