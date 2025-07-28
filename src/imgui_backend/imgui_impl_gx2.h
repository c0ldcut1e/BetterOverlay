#pragma once

#include <cstring>

#include <imgui.h>

#include <gx2/sampler.h>
#include <gx2/texture.h>

struct ImGui_ImplGX2_Texture {
    ImGui_ImplGX2_Texture() { memset(this, 0, sizeof(*this)); }

    GX2Texture *tex{};
    GX2Sampler *sampler{};
    int32_t loadFrame{};
};

IMGUI_IMPL_API bool ImGui_ImplGX2_Init();
IMGUI_IMPL_API void ImGui_ImplGX2_Shutdown();
IMGUI_IMPL_API void ImGui_ImplGX2_NewFrame();
IMGUI_IMPL_API void ImGui_ImplGX2_RenderDrawData(ImDrawData *data);

IMGUI_IMPL_API bool ImGui_ImplGX2_CreateFontsTexture();
IMGUI_IMPL_API void ImGui_ImplGX2_DestroyFontsTexture();
IMGUI_IMPL_API bool ImGui_ImplGX2_CreateDeviceObjects();
IMGUI_IMPL_API void ImGui_ImplGX2_DestroyDeviceObjects();
