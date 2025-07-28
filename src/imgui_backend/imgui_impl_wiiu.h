#pragma once

#include <imgui.h>

#include <padscore/wpad.h>
#include <vpad/input.h>

IMGUI_IMPL_API bool ImGui_ImplWiiU_Init();
IMGUI_IMPL_API void ImGui_ImplWiiU_Shutdown();
IMGUI_IMPL_API bool ImGui_ImplWiiU_ProcessVPADInput(const VPADStatus *input);
IMGUI_IMPL_API bool ImGui_ImplWiiU_ProcessWPADInput(const WPADStatusProController *input);

IMGUI_IMPL_API const VPADStatus *ImGui_ImplWiiU_GetLastVPADInput();
IMGUI_IMPL_API const WPADStatusProController *ImGui_ImplWiiU_GetLastWPADInput();
