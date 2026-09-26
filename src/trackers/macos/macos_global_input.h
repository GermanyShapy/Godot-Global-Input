#pragma once
#include "../common.h"

#include <vector>

#ifdef __APPLE__
#include <ApplicationServices/ApplicationServices.h>
#endif

using namespace godot;

// Polls the HID-level key state through CGEventSourceKeyState instead of asking
// Godot's Input singleton. The latter only reports keys that reached the window,
// so it silently stops working the moment the app loses focus -- which makes
// global hotkeys useless, and this is exactly what PNGTube-Remix hotkeys are
// for. CGEventSourceKeyState reads the combined session state directly, so it
// keeps answering while another app has focus.
class MacOSGlobalInput : public GlobalInputCommon {
public:
    MacOSGlobalInput(){}
    ~MacOSGlobalInput(){
        stop();
    }

    // Start/ Stop Hook

    void start() {
        if (running) return;

        if (!OS::get_singleton()) {
            running = false;
            return;
        }
        if (OS::get_singleton()->has_feature("editor_hint")) {
            running = false;
            return;
        }

        key_state.clear();
        key_just_pressed_frame.clear();
        key_just_released_frame.clear();

        mouse_state.clear();
        mouse_just_pressed_frame.clear();
        mouse_just_released_frame.clear();

        #ifdef __APPLE__
        // macOS can gate these queries behind Privacy & Security > Input
        // Monitoring. Without an explicit request the user never sees a prompt
        // and every query below just returns false, so ask here rather than
        // failing silently.
        if (!CGRequestListenEventAccess()) {
            godot::print_line(
                "Global Input: input monitoring not granted, hotkeys only work while focused. "
                "Allow this app under System Settings > Privacy & Security > Input Monitoring.");
        }
        #endif

        // The dummy version of this backend never filled the map at all.
        key_maps->get_platform_key_mapping(key_map);
        build_reverse_key_map();

        running = true;
    }

    void stop(){
        if (!running) return;
        running = false;
    }

    // Polling Data

    void poll_data() override {}

    void increment_frame(){
        current_frame++;
    }

    // Basic Key Input

    bool is_key_pressed(int key) override{
        check_key(key);
        return key_state[key];
    }

    bool is_key_just_pressed(int key) override{
        check_key(key);

        auto it = key_just_pressed_frame.find(key);
        return it != key_just_pressed_frame.end() &&
            it->second != 0 &&
            (current_frame - it->second) <= JUST_BUFFER_FRAMES;
    }

    bool is_key_just_released(int key) override{
        check_key(key);

        auto it = key_just_released_frame.find(key);
        return it != key_just_released_frame.end() &&
            it->second != 0 &&
            (current_frame - it->second) <= JUST_BUFFER_FRAMES;
    }

    // Mouse Input

    bool is_mouse_pressed(int button) override{
        check_mouse(button);
        return mouse_state[button];
    }

    bool is_mouse_just_pressed(int button) override{
        check_mouse(button);
        auto it = mouse_just_pressed_frame.find(button);

        return it != mouse_just_pressed_frame.end() &&
            it->second != 0 &&
            (current_frame - it->second) <= JUST_BUFFER_FRAMES;
    }

    bool is_mouse_just_released(int button) override{
        check_mouse(button);
        auto it = mouse_just_released_frame.find(button);
        return it != mouse_just_released_frame.end() &&
            it->second != 0 &&
            (current_frame - it->second) <= JUST_BUFFER_FRAMES;
    }

    Vector2 get_mouse_position() override{
        // DisplayServer reports the global cursor position even when unfocused,
        // so it is a better fit here than CGEventGetLocation (which needs an
        // event to interrogate).
        return DisplayServer::get_singleton()->mouse_get_position();
    }

    // Godot InputMap Action Detection

    bool is_action_pressed(const String &action, bool inclusive) override{
        return any_action_event_matches(action, inclusive, 0);
    }

    bool is_action_just_pressed(const String &action, bool inclusive) override{
        return any_action_event_matches(action, inclusive, 1);
    }

    bool is_action_just_released(const String &action, bool inclusive) override{
        return any_action_event_matches(action, inclusive, 2);
    }

    // Debug Returns

    Dictionary get_keys_pressed_detailed() override{
        Dictionary dict;
        for (const auto &[key, down] : key_state){
            if (!down) continue;
            String name = "Unknown";
            if (OS::get_singleton() && key >= 0 && key <= KEY_MENU)
                name = OS::get_singleton()->get_keycode_string((Key)key);
            dict[name] = true;
            dict["os"] = "MacOS";
        }
        return dict;
    }

    Dictionary get_keys_just_pressed_detailed() override{
        Dictionary dict;
        for (const auto &[key, frame] : key_just_pressed_frame) {
            if ((current_frame - frame) > 1) continue;
            String name = "Unknown";
            if (OS::get_singleton() && key >= 0 && key <= KEY_MENU)
                name = OS::get_singleton()->get_keycode_string((Key)key);
            dict[name] = true;
            dict["os"] = "MacOS";
        }
        return dict;
    }

    Dictionary get_keys_just_released_detailed() override{
        Dictionary dict;
        for (const auto &[key, frame] : key_just_released_frame) {
            if ((current_frame - frame) > 1) continue;
            String name = "Unknown";
            if (OS::get_singleton() && key >= 0 && key <= KEY_MENU)
                name = OS::get_singleton()->get_keycode_string((Key)key);
            dict[name] = true;
            dict["os"] = "MacOS";
        }
        return dict;
    }

    // Misc

    void handle_input(const Ref<InputEvent> &event) override {
        // Unused: state comes from polling the event source, not from events.
        // Kept so an InputEventKey arriving while focused cannot double-count
        // against the polled edges.
        (void)event;
    }

    void check_key(int keycode) {
        bool now = false;

        #ifdef __APPLE__
        auto it = reverse_key_map.find(keycode);
        if (it != reverse_key_map.end()) {
            for (int platform_code : it->second) {
                if (CGEventSourceKeyState(
                        kCGEventSourceStateCombinedSessionState, (CGKeyCode)platform_code) != 0) {
                    now = true;
                    break;
                }
            }
        }
        #endif

        bool prev = key_state[keycode];

        if (now && !prev) key_just_pressed_frame[keycode] = current_frame;
        if (!now && prev) key_just_released_frame[keycode] = current_frame;

        key_state[keycode] = now;
    }

    void check_mouse(int button) {
        bool now = false;

        #ifdef __APPLE__
        // CGEventSourceButtonState numbers buttons from 0 (left), unlike Godot's
        // MOUSE_BUTTON_LEFT = 1.
        auto it = reverse_mouse_map.find(button);
        if (it != reverse_mouse_map.end()) {
            now = CGEventSourceButtonState(
                kCGEventSourceStateCombinedSessionState, (CGMouseButton)it->second) != 0;
        }
        #endif

        bool prev = mouse_state[button];

        if (now && !prev) mouse_just_pressed_frame[button] = current_frame;
        if (!now && prev) mouse_just_released_frame[button] = current_frame;

        mouse_state[button] = now;
    }

private:
    // key_map is platform code -> Godot keycode; queries arrive as Godot
    // keycodes, so keep the inverse. Several platform codes can fold onto one
    // Godot keycode (kVK_ANSI_Backslash and kVK_ISO_Section both map to
    // KEY_BACKSLASH), so store every candidate and treat the key as down if any
    // of them is.
    std::unordered_map<int, std::vector<int>> reverse_key_map;

    std::unordered_map<int, int> reverse_mouse_map = {
        {MOUSE_BUTTON_LEFT, 0},
        {MOUSE_BUTTON_RIGHT, 1},
        {MOUSE_BUTTON_MIDDLE, 2},
    };

    void build_reverse_key_map() {
        reverse_key_map.clear();
        for (const auto &[platform_code, godot_key] : key_map) {
            reverse_key_map[godot_key].push_back(platform_code);
        }
    }
};
