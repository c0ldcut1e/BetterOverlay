#include "imgui_impl_wiiu.h"

#include <cstring>
#include <stdlib.h>

#include <utils/Logger.h>

#include <coreinit/fastmutex.h>
#include <coreinit/memory.h>

struct ImGui_ImplWiiU_Data {
    ImGui_ImplWiiU_Data() { OSBlockSet((void *) this, 0, sizeof(*this)); }

    bool wasTouched   = false;
    uint32_t vpadHeld = 0;
    uint32_t wpadHeld = 0;
    uint16_t lastKeys = 0;

    VPADStatus lastVPAD{};
    WPADStatusProController lastWPAD{};
};

struct PadKey {
    ImGuiKey key;
    uint32_t vMask;
    uint32_t wMask;
};

static OSFastMutex mutex;

static ImGui_ImplWiiU_Data *ImGui_ImplWiiU_GetBackendData() {
    return ImGui::GetCurrentContext()
                   ? static_cast<ImGui_ImplWiiU_Data *>(
                             ImGui::GetIO().BackendPlatformUserData)
                   : nullptr;
}

bool ImGui_ImplWiiU_Init() {
    ImGuiIO &io = ImGui::GetIO();
    IM_ASSERT(io.BackendPlatformUserData == nullptr &&
              "Already initialized a platform backend!");

    ImGui_ImplWiiU_Data *data  = IM_NEW(ImGui_ImplWiiU_Data)();
    io.BackendPlatformUserData = data;
    io.BackendPlatformName     = "imgui_impl_wiiu";
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

    io.KeyRepeatDelay = 0.9f;
    io.KeyRepeatRate  = 0.3f;

    return true;
}

void ImGui_ImplWiiU_Shutdown() {
    OSFastMutex_Lock(&mutex);

    ImGui_ImplWiiU_Data *data = ImGui_ImplWiiU_GetBackendData();
    IM_ASSERT(data && "No platform backend to shutdown, or already shutdown?");

    ImGuiIO &io                = ImGui::GetIO();
    io.BackendPlatformName     = nullptr;
    io.BackendPlatformUserData = nullptr;

    IM_DELETE(data);

    OSFastMutex_Unlock(&mutex);
}

static bool ImGui_ImplWiiU_WantsInput() {
    const ImGuiIO &io = ImGui::GetIO();
    return io.WantCaptureMouse || io.WantCaptureKeyboard || io.WantTextInput ||
           ((io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) &&
            io.NavActive &&
            ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow));
}

bool ImGui_ImplWiiU_ProcessVPADInput(VPADStatus *input) {
    ImGui_ImplWiiU_Data *data = ImGui_ImplWiiU_GetBackendData();
    IM_ASSERT(data != nullptr && "Did you call ImGui_ImplWiiU_Init() ?");

    ImGuiIO &io = ImGui::GetIO();

    VPADTouchData touch;
    VPADGetTPCalibratedPoint(VPAD_CHAN_0, &touch, &input->tpNormal);
    if (touch.touched) {
        float w = io.DisplaySize.x > 0.0f ? io.DisplaySize.x : 854.0f;
        float h = io.DisplaySize.y > 0.0f ? io.DisplaySize.y : 480.0f;
        io.AddMousePosEvent(touch.x * (w / 1280.0f), touch.y * (h / 720.0f));
    }

    if (touch.touched != data->wasTouched) {
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, touch.touched);
        data->wasTouched = touch.touched;
    }

    if (ImGui_ImplWiiU_WantsInput()) {
        uint32_t held = input->hold;

        io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, held & VPAD_BUTTON_LEFT);
        io.AddKeyEvent(ImGuiKey_GamepadDpadRight, held & VPAD_BUTTON_RIGHT);
        io.AddKeyEvent(ImGuiKey_GamepadDpadUp, held & VPAD_BUTTON_UP);
        io.AddKeyEvent(ImGuiKey_GamepadDpadDown, held & VPAD_BUTTON_DOWN);

        io.AddKeyEvent(ImGuiKey_GamepadFaceLeft, held & VPAD_BUTTON_X);
        io.AddKeyEvent(ImGuiKey_GamepadFaceRight, held & VPAD_BUTTON_B);
        io.AddKeyEvent(ImGuiKey_GamepadFaceUp, held & VPAD_BUTTON_Y);
        io.AddKeyEvent(ImGuiKey_GamepadFaceDown, held & VPAD_BUTTON_A);

        io.AddKeyEvent(ImGuiKey_GamepadLStickLeft,
                       held & VPAD_STICK_L_EMULATION_LEFT);
        io.AddKeyEvent(ImGuiKey_GamepadLStickRight,
                       held & VPAD_STICK_L_EMULATION_RIGHT);
        io.AddKeyEvent(ImGuiKey_GamepadLStickUp,
                       held & VPAD_STICK_L_EMULATION_UP);
        io.AddKeyEvent(ImGuiKey_GamepadLStickDown,
                       held & VPAD_STICK_L_EMULATION_DOWN);
    }

    return ImGui_ImplWiiU_WantsInput();
}

bool ImGui_ImplWiiU_ProcessWPADInput(WPADStatusProController *input) {
    ImGui_ImplWiiU_Data *data = ImGui_ImplWiiU_GetBackendData();
    IM_ASSERT(data != nullptr && "Did you call ImGui_ImplWiiU_Init() ?");

    /* ImGuiIO &io = ImGui::GetIO();

    if (ImGui_ImplWiiU_WantsInput()) {
        uint32_t held = input->buttons;

        io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, held & WPAD_PRO_BUTTON_LEFT);
        io.AddKeyEvent(ImGuiKey_GamepadDpadRight, held & WPAD_PRO_BUTTON_RIGHT);
        io.AddKeyEvent(ImGuiKey_GamepadDpadUp, held & WPAD_PRO_BUTTON_UP);
        io.AddKeyEvent(ImGuiKey_GamepadDpadDown, held & WPAD_PRO_BUTTON_DOWN);

        io.AddKeyEvent(ImGuiKey_GamepadFaceLeft, held & WPAD_PRO_BUTTON_X);
        io.AddKeyEvent(ImGuiKey_GamepadFaceRight, held & WPAD_PRO_BUTTON_B);
        io.AddKeyEvent(ImGuiKey_GamepadFaceUp, held & WPAD_PRO_BUTTON_Y);
        io.AddKeyEvent(ImGuiKey_GamepadFaceDown, held & WPAD_PRO_BUTTON_A);

        io.AddKeyEvent(ImGuiKey_GamepadLStickLeft,
                       held & WPAD_PRO_STICK_L_EMULATION_LEFT);
        io.AddKeyEvent(ImGuiKey_GamepadLStickRight,
                       held & WPAD_PRO_STICK_L_EMULATION_RIGHT);
        io.AddKeyEvent(ImGuiKey_GamepadLStickUp,
                       held & WPAD_PRO_STICK_L_EMULATION_UP);
        io.AddKeyEvent(ImGuiKey_GamepadLStickDown,
                       held & WPAD_PRO_STICK_L_EMULATION_DOWN);
    } */

    return ImGui_ImplWiiU_WantsInput();
}