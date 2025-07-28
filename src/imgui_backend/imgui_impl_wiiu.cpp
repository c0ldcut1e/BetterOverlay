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

    auto *data = IM_NEW(ImGui_ImplWiiU_Data)();
    OSFastMutex_Init(&mutex, "imgui_impl_wiiu");
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

static const PadKey kPadKeys[] = {
        {ImGuiKey_GamepadDpadLeft, VPAD_BUTTON_LEFT, WPAD_PRO_BUTTON_LEFT},
        {ImGuiKey_GamepadDpadRight, VPAD_BUTTON_RIGHT, WPAD_PRO_BUTTON_RIGHT},
        {ImGuiKey_GamepadDpadUp, VPAD_BUTTON_UP, WPAD_PRO_BUTTON_UP},
        {ImGuiKey_GamepadDpadDown, VPAD_BUTTON_DOWN, WPAD_PRO_BUTTON_DOWN},

        {ImGuiKey_GamepadFaceLeft, VPAD_BUTTON_X, WPAD_PRO_BUTTON_X},
        {ImGuiKey_GamepadFaceRight, VPAD_BUTTON_B, WPAD_PRO_BUTTON_B},
        {ImGuiKey_GamepadFaceUp, 0, WPAD_PRO_BUTTON_Y},
        {ImGuiKey_GamepadFaceDown, VPAD_BUTTON_A, WPAD_PRO_BUTTON_A},

        {ImGuiKey_GamepadLStickLeft, VPAD_STICK_L_EMULATION_LEFT,
         WPAD_PRO_STICK_L_EMULATION_LEFT},
        {ImGuiKey_GamepadLStickRight, VPAD_STICK_L_EMULATION_RIGHT,
         WPAD_PRO_STICK_L_EMULATION_RIGHT},
        {ImGuiKey_GamepadLStickUp, VPAD_STICK_L_EMULATION_UP,
         WPAD_PRO_STICK_L_EMULATION_UP},
        {ImGuiKey_GamepadLStickDown, VPAD_STICK_L_EMULATION_DOWN,
         WPAD_PRO_STICK_L_EMULATION_DOWN},

        {ImGuiKey_GamepadL1, VPAD_BUTTON_L, WPAD_PRO_BUTTON_L},
        {ImGuiKey_GamepadR1, VPAD_BUTTON_R, WPAD_PRO_BUTTON_R},
};

static constexpr size_t kKeyCount = sizeof(kPadKeys) / sizeof(kPadKeys[0]);

static void ImGui_ImplWiiU_ReportKeys(ImGui_ImplWiiU_Data *data) {
    OSFastMutex_Lock(&mutex);

    if (!ImGui_ImplWiiU_WantsInput()) {
        OSFastMutex_Unlock(&mutex);
        return;
    }

    ImGuiIO &io      = ImGui::GetIO();
    uint16_t newMask = 0;

    for (size_t i = 0; i < kKeyCount; ++i) {
        bool pressed = (data->vpadHeld & kPadKeys[i].vMask) ||
                       (data->wpadHeld & kPadKeys[i].wMask);
        if (pressed) newMask |= uint16_t(1 << i);

        if (((data->lastKeys ^ newMask) >> i) & 1)
            io.AddKeyEvent(kPadKeys[i].key, pressed);
    }

    data->lastKeys = newMask;

    OSFastMutex_Unlock(&mutex);
}

bool ImGui_ImplWiiU_ProcessVPADInput(const VPADStatus *input) {
    OSFastMutex_Lock(&mutex);

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

    data->vpadHeld = input->hold;
    ImGui_ImplWiiU_ReportKeys(data);

    data->lastVPAD = *input;

    OSFastMutex_Unlock(&mutex);
    return ImGui_ImplWiiU_WantsInput();
}

bool ImGui_ImplWiiU_ProcessWPADInput(const WPADStatusProController *input) {
    OSFastMutex_Lock(&mutex);

    ImGui_ImplWiiU_Data *data = ImGui_ImplWiiU_GetBackendData();
    IM_ASSERT(data != nullptr && "Did you call ImGui_ImplWiiU_Init() ?");

    data->wpadHeld = input->buttons;
    ImGui_ImplWiiU_ReportKeys(data);

    data->lastWPAD = *input;

    OSFastMutex_Unlock(&mutex);
    return ImGui_ImplWiiU_WantsInput();
}

const VPADStatus *ImGui_ImplWiiU_GetLastVPADInput() {
    OSFastMutex_Lock(&mutex);

    const ImGui_ImplWiiU_Data *data = ImGui_ImplWiiU_GetBackendData();
    IM_ASSERT(data != nullptr && "Did you call ImGui_ImplWiiU_Init() ?");

    OSFastMutex_Unlock(&mutex);
    return data ? &data->lastVPAD : nullptr;
}

const WPADStatusProController *ImGui_ImplWiiU_GetLastWPADInput() {
    OSFastMutex_Lock(&mutex);

    const ImGui_ImplWiiU_Data *data = ImGui_ImplWiiU_GetBackendData();
    IM_ASSERT(data != nullptr && "Did you call ImGui_ImplWiiU_Init() ?");

    OSFastMutex_Unlock(&mutex);
    return data ? &data->lastWPAD : nullptr;
}
