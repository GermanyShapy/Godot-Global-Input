#pragma once
#include "../common.h"

using namespace godot;

class WindowsGlobalInput : public GlobalInputCommon {
public:
    WindowsGlobalInput(){}
    ~WindowsGlobalInput(){ stop();}

    // Start/ Stop Hook

    void start() {
        if (running) return;

        if (!OS::get_singleton()) {
            running = false;
            return;
        }
        if (OS::get_singleton()->has_feature("editor_hint")){
            running = false;
            return;
        }

        key_state.clear();
        key_just_pressed_frame.clear();
        key_just_released_frame.clear();

        mouse_state.clear();
        mouse_just_pressed_frame.clear();
        mouse_just_released_frame.clear();

        running = true;
        key_maps->get_platform_key_mapping(key_map);
        active_layout = GetKeyboardLayout(0);
        hook_thread = std::thread(&poll_input, this);
    }

    void stop(){
        if (running){
            std::lock_guard<std::recursive_mutex> lock(state_mutex);
            running = false;
            if (hook_thread.joinable())
                hook_thread.join();  
        }
    }

    // Polling Data

    void poll_data() override {
        std::lock_guard<std::recursive_mutex> lock(state_mutex);

        for (auto &it : key_just_pressed_frame) {
            if (it.second == 0) {
                it.second = current_frame;
            }
        }

        for (auto &it : key_just_released_frame) {
            if (it.second == 0) {
                it.second = current_frame;
            }
        }

        for (auto &it : mouse_just_pressed_frame) {
            if (it.second == 0) {
                it.second = current_frame;
            }
        }

        for (auto &it : mouse_just_released_frame) {
            if (it.second == 0) {
                it.second = current_frame;
            }
        }
    }

    void increment_frame() override{
        std::lock_guard<std::recursive_mutex> lock(state_mutex);
        current_frame++;    
    }

    // Basic Key Input

    bool is_key_pressed(int key) override{
        std::lock_guard<std::recursive_mutex> lock(state_mutex);
        auto it = key_state.find(key);
        return it != key_state.end() && it->second;
    }

    bool is_key_just_pressed(int key) override{
        std::lock_guard<std::recursive_mutex> lock(state_mutex);
        auto it = key_just_pressed_frame.find(key);
        return it != key_just_pressed_frame.end() &&
            it->second != 0 &&
            (current_frame - it->second) <= JUST_BUFFER_FRAMES;
    }

    bool is_key_just_released(int key) override{
        std::lock_guard<std::recursive_mutex> lock(state_mutex);
        auto it = key_just_released_frame.find(key);
        return it != key_just_released_frame.end() &&
            it->second != 0 &&
            (current_frame - it->second) <= JUST_BUFFER_FRAMES;
    }

    // Mouse Input

    bool is_mouse_pressed(int button) override{
        std::lock_guard<std::recursive_mutex> lock(state_mutex);
        auto it = mouse_state.find(button);
        return it != mouse_state.end() && it->second;
    }

    bool is_mouse_just_pressed(int button) override{
        std::lock_guard<std::recursive_mutex> lock(state_mutex);
        auto it = mouse_just_pressed_frame.find(button);
        return it != mouse_just_pressed_frame.end() &&
            it->second != 0 &&
            (current_frame - it->second) <= JUST_BUFFER_FRAMES;
    }

    bool is_mouse_just_released(int button) override{
        std::lock_guard<std::recursive_mutex> lock(state_mutex);
        auto it = mouse_just_released_frame.find(button);
        return it != mouse_just_released_frame.end() &&
            it->second != 0 &&
            (current_frame - it->second) <= JUST_BUFFER_FRAMES;
    }

    Vector2 get_mouse_position() override{
        std::lock_guard<std::recursive_mutex> lock(state_mutex);
        return mouse_position;
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
        std::lock_guard<std::recursive_mutex> lock(state_mutex);
        return keys_to_detailed(frame_keys);
    }

    Dictionary get_keys_just_pressed_detailed() override{
        std::lock_guard<std::recursive_mutex> lock(state_mutex);
        return keys_to_detailed(keys_just_pressed);
    }

    Dictionary get_keys_just_released_detailed() override{
        std::lock_guard<std::recursive_mutex> lock(state_mutex);
        return keys_to_detailed(keys_just_released);
    }

    // Modifiers

    // NOTE: key_state / frame_keys are keyed by *Godot* keycodes (the values of
    // key_map), never by raw Windows VK codes. Mixing the two namespaces is not
    // just useless but actively harmful: VK_LWIN is 0x5B == 91 == KEY_BRACKETLEFT
    // and VK_RWIN is 0x5C == 92 == KEY_BACKSLASH, so pressing '[' used to make
    // the plugin believe Meta was held and broke every modifier match for it.
    // Both left and right variants of a modifier collapse onto the same Godot
    // keycode in key_map, so a single Godot-code lookup covers all of them.

    bool is_alt_pressed() override{
        #ifdef _WIN32
        return is_key_pressed(KEY_ALT);
        #endif
        return false;
    }

    bool is_ctrl_pressed() override{
        #ifdef _WIN32
        return is_key_pressed(KEY_CTRL);
        #endif
        return false;

    }

    bool is_shift_pressed() override{
        #ifdef _WIN32
        return is_key_pressed(KEY_SHIFT);
        #endif
        return false;
    }

    bool is_meta_pressed() override{
        #ifdef _WIN32
        return is_key_pressed(KEY_META);
        #endif
        return false;
    }

    // Misc

    void handle_input(const Ref<InputEvent> &event) override {};

    HKL active_layout = nullptr;
    int layout_check_counter = 0;

    void poll_input() {
        #ifdef _WIN32
            while (running) {
                {
                    if (!OS::get_singleton()) {
                        running = false;
                        return;
                    }
                    
                    std::lock_guard<std::recursive_mutex> lock(state_mutex);

                    std::unordered_map<int, bool> pressed_now;
                    for (const auto &[vk, godot_key] : key_map) {
                        SHORT state = GetAsyncKeyState(vk);
                        if ((state & 0x8000) != 0)
                            pressed_now[godot_key] = true;
                        else
                            pressed_now.emplace(godot_key, false);
                    }

                    for (const auto &[godot_key, pressed] : pressed_now) {
                        bool was_pressed = key_state[godot_key];

                        key_state[godot_key] = pressed;

                        if (pressed && !was_pressed)
                            key_just_pressed_frame[godot_key] = 0;

                        if (!pressed && was_pressed)
                            key_just_released_frame[godot_key] = 0;
                    }

                    if (++layout_check_counter >= 250) {
                        layout_check_counter = 0;
                        HKL layout = GetKeyboardLayout(0);
                        if (layout && layout != active_layout) {
                            active_layout = layout;
                            key_maps->get_platform_key_mapping(key_map);
                        }
                    }

                    POINT p;
                    if (GetCursorPos(&p)) {
                        mouse_position = Vector2(p.x, p.y);
                    }

                    int buttons[] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2 };
                    int godot_buttons[] = {
                        MOUSE_BUTTON_LEFT,
                        MOUSE_BUTTON_RIGHT,
                        MOUSE_BUTTON_MIDDLE,
                        MOUSE_BUTTON_XBUTTON1,
                        MOUSE_BUTTON_XBUTTON2
                    };

                    for (int i = 0; i < 5; i++) {
                        SHORT state = GetAsyncKeyState(buttons[i]);
                        bool pressed = (state & 0x8000) != 0;
                        bool was_pressed = mouse_state[godot_buttons[i]];

                        mouse_state[godot_buttons[i]] = pressed;

                        if (pressed && !was_pressed)
                            mouse_just_pressed_frame[godot_buttons[i]] = 0;

                        if (!pressed && was_pressed)
                            mouse_just_released_frame[godot_buttons[i]] = 0;
                    }
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        #endif
    }


};
