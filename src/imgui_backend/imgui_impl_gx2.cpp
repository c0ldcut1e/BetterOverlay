#include "imgui_impl_gx2.h"

#include <shaders/TexShader.h>
#include <utils/Logger.h>
#include <utils/Types.h>

#include <coreinit/cache.h>
#include <coreinit/fastmutex.h>
#include <coreinit/memory.h>
#include <gx2/draw.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/state.h>
#include <gx2/utils.h>
#include <whb/gfx.h>

#include <memory/mappedmemory.h>

#define BUFFER_COUNT 4

struct ImGui_ImplGX2_Data {
    ImGui_ImplGX2_Data() { OSBlockSet(this, 0, sizeof(*this)); }

    void *vtxBuffers[BUFFER_COUNT]{};
    void *idxBuffers[BUFFER_COUNT]{};
    uint32_t vtxBufferSizes[BUFFER_COUNT]{};
    uint32_t idxBufferSizes[BUFFER_COUNT]{};
    int32_t bufferIndex{};

    ImGui_ImplGX2_Texture *fontTex{};

    WHBGfxShaderGroup *shader{};
};

static OSFastMutex mutex;

static ImGui_ImplGX2_Data *ImGui_ImplGX2_GetBackendData() {
    return ImGui::GetCurrentContext() ? static_cast<ImGui_ImplGX2_Data *>(ImGui::GetIO().BackendRendererUserData) : nullptr;
}

bool ImGui_ImplGX2_Init() {
    ImGuiIO &io = ImGui::GetIO();
    IM_ASSERT(io.BackendRendererUserData == nullptr && "Already initialized a renderer backend!");

    auto *data = IM_NEW(ImGui_ImplGX2_Data)();
    OSFastMutex_Init(&mutex, "imgui_impl_gx2");
    io.BackendRendererUserData = data;
    io.BackendRendererName     = "imgui_impl_gx2";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

    for (int i = 0; i < BUFFER_COUNT; i++) {
        data->vtxBuffers[i]     = nullptr;
        data->idxBuffers[i]     = nullptr;
        data->vtxBufferSizes[i] = 0;
        data->idxBufferSizes[i] = 0;
    }
    data->bufferIndex = 0;

    return true;
}

void ImGui_ImplGX2_Shutdown() {
    OSFastMutex_Lock(&mutex);

    ImGui_ImplGX2_Data *data = ImGui_ImplGX2_GetBackendData();
    IM_ASSERT(data != nullptr && "No renderer backend to shutdown, or already shutdown?");

    ImGuiIO &io = ImGui::GetIO();

    ImGui_ImplGX2_DestroyDeviceObjects();
    io.BackendRendererName     = nullptr;
    io.BackendRendererUserData = nullptr;

    IM_DELETE(data);

    OSFastMutex_Unlock(&mutex);
}

void ImGui_ImplGX2_NewFrame() {
    OSFastMutex_Lock(&mutex);

    const ImGui_ImplGX2_Data *data = ImGui_ImplGX2_GetBackendData();
    IM_ASSERT(data != nullptr && "Did you call ImGui_ImplGX2_Init() ?");

    if (!data->shader) ImGui_ImplGX2_CreateDeviceObjects();

    OSFastMutex_Unlock(&mutex);
}

static void ImGui_ImplGX2_SetupRenderState(const ImDrawData *data, const int32_t width, const int32_t height) {
    ImGui_ImplGX2_Data *backendData = ImGui_ImplGX2_GetBackendData();
    IM_ASSERT(backendData != nullptr && "No renderer backend to shutdown, or already shutdown?");

    GX2SetColorControl(GX2_LOGIC_OP_COPY, 0xFF, FALSE, TRUE);
    GX2SetBlendControl(GX2_RENDER_TARGET_0, GX2_BLEND_MODE_SRC_ALPHA, GX2_BLEND_MODE_INV_SRC_ALPHA, GX2_BLEND_COMBINE_MODE_ADD, TRUE, GX2_BLEND_MODE_ONE,
                       GX2_BLEND_MODE_INV_SRC_ALPHA, GX2_BLEND_COMBINE_MODE_ADD);
    GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, FALSE, FALSE);
    GX2SetDepthOnlyControl(FALSE, FALSE, GX2_COMPARE_FUNC_NEVER);

    GX2SetViewport(0.0f, 0.0f, static_cast<float32_t>(width), static_cast<float32_t>(height), 0.0f, 1.0f);

    GX2SetFetchShader(&backendData->shader->fetchShader);
    GX2SetVertexShader(backendData->shader->vertexShader);
    GX2SetPixelShader(backendData->shader->pixelShader);

    const float32_t l = data->DisplayPos.x;
    const float32_t r = data->DisplayPos.x + data->DisplaySize.x;
    const float32_t t = data->DisplayPos.y;
    const float32_t b = data->DisplayPos.y + data->DisplaySize.y;

    // clang-format off
    const float32_t ortho[4][4] = {
        { 2.0f / (r - l),    0.0f,               0.0f, 0.0f },
        { 0.0f,              2.0f / (t - b),     0.0f, 0.0f },
        { 0.0f,              0.0f,              -1.0f, 0.0f },
        { (r + l) / (l - r), (t + b) / (b - t),  0.0f, 1.0f }
    };
    // clang-format on

    GX2SetVertexUniformReg(0, sizeof(ortho) / sizeof(float32_t), &ortho[0][0]);
}

void ImGui_ImplGX2_RenderDrawData(ImDrawData *data) {
    OSFastMutex_Lock(&mutex);

    const auto width  = static_cast<int32_t>(data->DisplaySize.x * data->FramebufferScale.x);
    const auto height = static_cast<int32_t>(data->DisplaySize.y * data->FramebufferScale.y);
    if (width <= 0 || height <= 0) {
        OSFastMutex_Unlock(&mutex);
        return;
    }

    ImGui_ImplGX2_SetupRenderState(data, width, height);

    const ImVec2 clipOff   = data->DisplayPos;
    const ImVec2 clipScale = data->FramebufferScale;

    const uint32_t vtxBufferSize = data->TotalVtxCount * sizeof(ImDrawVert);
    const uint32_t idxBufferSize = data->TotalIdxCount * sizeof(ImDrawIdx);

    ImGui_ImplGX2_Data *backendData = ImGui_ImplGX2_GetBackendData();

    const int32_t bufferIdx  = backendData->bufferIndex;
    backendData->bufferIndex = (bufferIdx + 1) % BUFFER_COUNT;

    if (backendData->vtxBufferSizes[bufferIdx] < vtxBufferSize) {
        if (backendData->vtxBuffers[bufferIdx]) MEMFreeToMappedMemory(backendData->vtxBuffers[bufferIdx]);

        backendData->vtxBufferSizes[bufferIdx] = vtxBufferSize;
        backendData->vtxBuffers[bufferIdx]     = MEMAllocFromMappedMemoryForGX2Ex(vtxBufferSize, GX2_VERTEX_BUFFER_ALIGNMENT);
    }

    if (backendData->idxBufferSizes[bufferIdx] < idxBufferSize) {
        if (backendData->idxBuffers[bufferIdx]) MEMFreeToMappedMemory(backendData->idxBuffers[bufferIdx]);

        backendData->idxBufferSizes[bufferIdx] = idxBufferSize;
        backendData->idxBuffers[bufferIdx]     = MEMAllocFromMappedMemoryForGX2Ex(idxBufferSize, GX2_INDEX_BUFFER_ALIGNMENT);
    }

    auto *vtxBase    = static_cast<uint8_t *>(backendData->vtxBuffers[bufferIdx]);
    auto *idxBase    = static_cast<uint8_t *>(backendData->idxBuffers[bufferIdx]);
    uint8_t *vtxDest = vtxBase;
    uint8_t *idxDest = idxBase;

    for (int32_t i = 0; i < data->CmdListsCount; i++) {
        const ImDrawList *cmdList = data->CmdLists[i];

        OSBlockMove(vtxDest, cmdList->VtxBuffer.Data, cmdList->VtxBuffer.Size * sizeof(ImDrawVert), false);
        vtxDest += cmdList->VtxBuffer.Size * sizeof(ImDrawVert);

        OSBlockMove(idxDest, cmdList->IdxBuffer.Data, cmdList->IdxBuffer.Size * sizeof(ImDrawIdx), false);
        idxDest += cmdList->IdxBuffer.Size * sizeof(ImDrawIdx);
    }

    DCFlushRange(vtxBase, vtxBufferSize);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, vtxBase, vtxBufferSize);

    DCFlushRange(idxBase, idxBufferSize);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, idxBase, idxBufferSize);

    GX2SetAttribBuffer(0, vtxBufferSize, sizeof(ImDrawVert), vtxBase);
    GX2SetAttribBuffer(1, vtxBufferSize, sizeof(ImDrawVert), vtxBase + 8);
    GX2SetAttribBuffer(2, vtxBufferSize, sizeof(ImDrawVert), vtxBase + 16);

    int32_t globalVtxOffset = 0;
    int32_t globalIdxOffset = 0;
    for (int32_t i = 0; i < data->CmdListsCount; i++) {
        const ImDrawList *cmdList = data->CmdLists[i];

        for (int32_t j = 0; j < cmdList->CmdBuffer.Size; j++) {
            if (const ImDrawCmd *pcmd = &cmdList->CmdBuffer[j]; pcmd->UserCallback != nullptr) {
                if (pcmd->UserCallback == ImDrawCallback_ResetRenderState) ImGui_ImplGX2_SetupRenderState(data, width, height);
                else
                    pcmd->UserCallback(cmdList, pcmd);
            } else {
                const ImVec2 clipMin((pcmd->ClipRect.x - clipOff.x) * clipScale.x, (pcmd->ClipRect.y - clipOff.y) * clipScale.y);
                const ImVec2 clipMax((pcmd->ClipRect.z - clipOff.x) * clipScale.x, (pcmd->ClipRect.w - clipOff.y) * clipScale.y);
                if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y) continue;
                if (clipMin.x < 0.0f || clipMin.y < 0.0f || clipMax.x > static_cast<float32_t>(width) || clipMax.y > static_cast<float32_t>(height) ||
                    !pcmd->ElemCount)
                    continue;

                GX2SetScissor(static_cast<uint32_t>(clipMin.x), static_cast<uint32_t>(clipMin.y), static_cast<uint32_t>(clipMax.x - clipMin.x),
                              static_cast<uint32_t>(clipMax.y - clipMin.y));

                const auto *tex = static_cast<ImGui_ImplGX2_Texture *>(pcmd->GetTexID());
                IM_ASSERT(tex && "tex_id cannot be nullptr");

                GX2SetPixelTexture(tex->tex, 0);
                GX2SetPixelSampler(tex->sampler, 0);

                GX2DrawIndexedEx(GX2_PRIMITIVE_MODE_TRIANGLES, pcmd->ElemCount, sizeof(ImDrawIdx) == 2 ? GX2_INDEX_TYPE_U16 : GX2_INDEX_TYPE_U32,
                                 idxBase + (pcmd->IdxOffset + globalIdxOffset) * sizeof(ImDrawIdx), globalVtxOffset + pcmd->VtxOffset, 1);
            }
        }

        globalVtxOffset += cmdList->VtxBuffer.Size;
        globalIdxOffset += cmdList->IdxBuffer.Size;
    }

    GX2Flush();

    OSFastMutex_Unlock(&mutex);
}

bool ImGui_ImplGX2_CreateFontsTexture() {
    OSFastMutex_Lock(&mutex);

    ImGui_ImplGX2_Data *data = ImGui_ImplGX2_GetBackendData();
    IM_ASSERT(data != nullptr && "Did you call ImGui_ImplGX2_Init() ?");

    if (data->fontTex) {
        OSFastMutex_Unlock(&mutex);
        return false;
    }

    uint8_t *srcPixels;
    int32_t width;
    int32_t height;

    ImGuiIO &io = ImGui::GetIO();
    io.Fonts->GetTexDataAsRGBA32(&srcPixels, &width, &height);

    data->fontTex = IM_NEW(ImGui_ImplGX2_Texture)();

    auto *tex = IM_NEW(GX2Texture)();
    OSBlockSet(tex, 0, sizeof(GX2Texture));
    data->fontTex->tex = tex;

    tex->surface.dim       = GX2_SURFACE_DIM_TEXTURE_2D;
    tex->surface.use       = GX2_SURFACE_USE_TEXTURE;
    tex->surface.width     = width;
    tex->surface.height    = height;
    tex->surface.depth     = 1;
    tex->surface.mipLevels = 1;
    tex->surface.format    = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    tex->surface.aa        = GX2_AA_MODE1X;
    tex->surface.tileMode  = GX2_TILE_MODE_LINEAR_ALIGNED;
    tex->viewNumSlices     = 1;
    tex->viewNumMips       = 1;
    tex->compMap           = GX2_COMP_MAP(GX2_SQ_SEL_A, GX2_SQ_SEL_B, GX2_SQ_SEL_G, GX2_SQ_SEL_R);

    GX2CalcSurfaceSizeAndAlignment(&tex->surface);
    GX2InitTextureRegs(tex);

    tex->surface.image = MEMAllocFromMappedMemoryForGX2Ex(tex->surface.imageSize, (int32_t) tex->surface.alignment);

    auto *destPixels = (uint8_t *) tex->surface.image;

    for (int i = 0; i < height; i++) OSBlockMove(destPixels + (i * tex->surface.pitch * 4), srcPixels + (i * width * 4), width * 4, false);

    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, destPixels, tex->surface.imageSize);

    data->fontTex->sampler = IM_NEW(GX2Sampler)();
    GX2InitSampler(data->fontTex->sampler, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_POINT);

    io.Fonts->SetTexID(data->fontTex);

    OSFastMutex_Unlock(&mutex);
    return true;
}

void ImGui_ImplGX2_DestroyFontsTexture() {
    OSFastMutex_Lock(&mutex);

    ImGui_ImplGX2_Data *data = ImGui_ImplGX2_GetBackendData();
    IM_ASSERT(data != nullptr && "Did you call ImGui_ImplGX2_Init() ?");

    if (data->fontTex) {
        MEMFreeToMappedMemory(data->fontTex->tex->surface.image);

        const ImGuiIO &io = ImGui::GetIO();
        io.Fonts->SetTexID(nullptr);
        IM_DELETE(data->fontTex->tex);
        IM_DELETE(data->fontTex->sampler);
        IM_DELETE(data->fontTex);
        data->fontTex = nullptr;
    }

    OSFastMutex_Unlock(&mutex);
}

bool ImGui_ImplGX2_CreateDeviceObjects() {
    OSFastMutex_Lock(&mutex);

    ImGui_ImplGX2_Data *data = ImGui_ImplGX2_GetBackendData();
    IM_ASSERT(data != nullptr && "Did you call ImGui_ImplGX2_Init() ?");

    data->shader = IM_NEW(WHBGfxShaderGroup)();

    IM_ASSERT(loadTexShader(*data->shader) && "loadTexShader() failed");
    initTexShaderAttrs(*data->shader);

    ImGui_ImplGX2_CreateFontsTexture();

    OSFastMutex_Unlock(&mutex);
    return true;
}

void ImGui_ImplGX2_DestroyDeviceObjects() {
    OSFastMutex_Lock(&mutex);

    ImGui_ImplGX2_Data *data = ImGui_ImplGX2_GetBackendData();
    IM_ASSERT(data != nullptr && "Did you call ImGui_ImplGX2_Init() ?");

    for (int i = 0; i < BUFFER_COUNT; i++) {
        if (data->vtxBuffers[i]) {
            MEMFreeToMappedMemory(data->vtxBuffers[i]);
            data->vtxBuffers[i] = nullptr;
        }

        if (data->idxBuffers[i]) {
            MEMFreeToMappedMemory(data->idxBuffers[i]);
            data->idxBuffers[i] = nullptr;
        }
    }

    unloadTexShader(*data->shader);
    IM_DELETE(data->shader);
    data->shader = nullptr;

    ImGui_ImplGX2_DestroyFontsTexture();

    OSFastMutex_Unlock(&mutex);
}
