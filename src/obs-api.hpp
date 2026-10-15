#pragma once
#include <cstddef>
#include <cstdint>
#include <windows.h>

// Minimal C ABI declarations from OBS Studio 32.2.2's published headers.
// Functions are resolved from OBS itself, keeping this plugin independent of
// a particular compiler's OBS import libraries and of Qt.
struct obs_data;
struct obs_data_array;
struct obs_source;
struct obs_module;
struct obs_scene;
struct obs_scene_item;
using obs_scene_t = obs_scene;
using obs_sceneitem_t = obs_scene_item;
struct alignas(16) ObsVec4 {
    float x, y, z, w;
};
struct alignas(16) ObsMatrix4 {
    ObsVec4 x, y, z, t;
};
struct ObsCrop {
    int left, top, right, bottom;
};
using obs_data_t = obs_data;
using obs_data_array_t = obs_data_array;
using obs_source_t = obs_source;
using obs_module_t = obs_module;
using obs_hotkey_id = size_t;
using draw_fn = void (*)(void *, uint32_t, uint32_t);
using hotkey_fn = void (*)(void *, obs_hotkey_id, void *, bool);
#define OBS_FUNCTIONS(X)                                                                                     \
    X(obs_data_t *, obs_data_create, ())                                                                     \
    X(obs_data_t *, obs_data_create_from_json_file_safe, (const char *, const char *))                       \
    X(void, obs_data_release, (obs_data_t *))                                                                \
    X(bool, obs_data_save_json_safe, (obs_data_t *, const char *, const char *, const char *))               \
    X(void, obs_data_set_string, (obs_data_t *, const char *, const char *))                                 \
    X(void, obs_data_set_int, (obs_data_t *, const char *, long long))                                       \
    X(void, obs_data_set_bool, (obs_data_t *, const char *, bool))                                           \
    X(void, obs_data_set_array, (obs_data_t *, const char *, obs_data_array_t *))                            \
    X(void, obs_data_set_default_string, (obs_data_t *, const char *, const char *))                         \
    X(void, obs_data_set_default_int, (obs_data_t *, const char *, long long))                               \
    X(void, obs_data_set_default_bool, (obs_data_t *, const char *, bool))                                   \
    X(const char *, obs_data_get_string, (obs_data_t *, const char *))                                       \
    X(long long, obs_data_get_int, (obs_data_t *, const char *))                                             \
    X(bool, obs_data_get_bool, (obs_data_t *, const char *))                                                 \
    X(obs_data_array_t *, obs_data_get_array, (obs_data_t *, const char *))                                  \
    X(void, obs_data_array_release, (obs_data_array_t *))                                                    \
    X(obs_source_t *, obs_source_create_private, (const char *, const char *, obs_data_t *))                 \
    X(void, obs_source_release, (obs_source_t *))                                                            \
    X(void, obs_source_update, (obs_source_t *, obs_data_t *))                                               \
    X(void, obs_source_inc_active, (obs_source_t *))                                                         \
    X(void, obs_source_dec_active, (obs_source_t *))                                                         \
    X(void, obs_source_inc_showing, (obs_source_t *))                                                        \
    X(void, obs_source_dec_showing, (obs_source_t *))                                                        \
    X(void, obs_source_video_render, (obs_source_t *))                                                       \
    X(void, obs_add_main_render_callback, (draw_fn, void *))                                                 \
    X(void, obs_remove_main_render_callback, (draw_fn, void *))                                              \
    X(void, obs_add_main_rendered_callback, (void (*)(void *), void *))                                      \
    X(void, obs_remove_main_rendered_callback, (void (*)(void *), void *))                                   \
    X(obs_source_t *, obs_get_output_source, (uint32_t))                                                     \
    X(obs_source_t *, obs_transition_get_active_source, (obs_source_t *))                                    \
    X(obs_scene_t *, obs_scene_from_source, (const obs_source_t *))                                          \
    X(void, obs_scene_enum_items,                                                                            \
      (obs_scene_t *, bool (*)(obs_scene_t *, obs_sceneitem_t *, void *), void *))                           \
    X(bool, obs_sceneitem_visible, (const obs_sceneitem_t *))                                                \
    X(obs_source_t *, obs_sceneitem_get_source, (const obs_sceneitem_t *))                                   \
    X(obs_scene_t *, obs_sceneitem_group_get_scene, (const obs_sceneitem_t *))                               \
    X(void, obs_sceneitem_get_draw_transform, (const obs_sceneitem_t *, ObsMatrix4 *))                       \
    X(void, obs_sceneitem_get_crop, (const obs_sceneitem_t *, ObsCrop *))                                    \
    X(uint32_t, obs_source_get_width, (obs_source_t *))                                                      \
    X(uint32_t, obs_source_get_height, (obs_source_t *))                                                     \
    X(obs_data_t *, obs_source_get_settings, (const obs_source_t *))                                         \
    X(const char *, obs_source_get_name, (const obs_source_t *))                                             \
    X(const char *, obs_source_get_unversioned_id, (const obs_source_t *))                                   \
    X(void, obs_enum_sources, (bool (*)(void *, obs_source_t *), void *))                                    \
    X(char *, obs_module_get_config_path, (obs_module_t *, const char *))                                    \
    X(void, bfree, (void *))                                                                                 \
    X(void, gs_matrix_push, ())                                                                              \
    X(void, gs_matrix_pop, ())                                                                               \
    X(void, gs_matrix_translate3f, (float, float, float))                                                    \
    X(void, gs_matrix_scale3f, (float, float, float))                                                        \
    X(void, gs_blend_state_push, ())                                                                         \
    X(void, gs_blend_state_pop, ())                                                                          \
    X(void, gs_blend_function, (int, int))                                                                   \
    X(obs_hotkey_id, obs_hotkey_register_frontend, (const char *, const char *, hotkey_fn, void *))          \
    X(void, obs_hotkey_unregister, (obs_hotkey_id))                                                          \
    X(void, obs_hotkey_load, (obs_hotkey_id, obs_data_array_t *))                                            \
    X(obs_data_array_t *, obs_hotkey_save, (obs_hotkey_id))
struct ObsApi {
#define DECL(ret, name, args) ret(*name) args = nullptr;
    OBS_FUNCTIONS(DECL)
#undef DECL
    void (*obs_frontend_add_tools_menu_item)(const char *, void (*)(void *), void *) = nullptr;
    void (*obs_frontend_take_screenshot)() = nullptr;
    void (*obs_frontend_add_event_callback)(void (*)(int, void *), void *) = nullptr;
    void (*obs_frontend_remove_event_callback)(void (*)(int, void *), void *) = nullptr;
    void (*blog)(int, const char *, ...) = nullptr;
    bool bind() {
        HMODULE core = GetModuleHandleW(L"obs.dll"), front = GetModuleHandleW(L"obs-frontend-api.dll");
        if (!core || !front)
            return false;
        blog = reinterpret_cast<decltype(blog)>(GetProcAddress(core, "blog"));
#define BIND(ret, name, args)                                                                                \
    name = reinterpret_cast<decltype(name)>(GetProcAddress(core, #name));                                    \
    if (!name) {                                                                                             \
        if (blog)                                                                                            \
            blog(100, "[InputFX] Missing OBS API: %s", #name);                                               \
        return false;                                                                                        \
    }
        OBS_FUNCTIONS(BIND)
#undef BIND
        obs_frontend_add_tools_menu_item = reinterpret_cast<decltype(obs_frontend_add_tools_menu_item)>(
            GetProcAddress(front, "obs_frontend_add_tools_menu_item"));
        obs_frontend_take_screenshot = reinterpret_cast<decltype(obs_frontend_take_screenshot)>(
            GetProcAddress(front, "obs_frontend_take_screenshot"));
        obs_frontend_add_event_callback = reinterpret_cast<decltype(obs_frontend_add_event_callback)>(
            GetProcAddress(front, "obs_frontend_add_event_callback"));
        obs_frontend_remove_event_callback = reinterpret_cast<decltype(obs_frontend_remove_event_callback)>(
            GetProcAddress(front, "obs_frontend_remove_event_callback"));
        return obs_frontend_add_tools_menu_item && obs_frontend_take_screenshot &&
               obs_frontend_add_event_callback && obs_frontend_remove_event_callback;
    }
};
