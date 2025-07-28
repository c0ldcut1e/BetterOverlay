#include <cstring>

#include <Globals.h>
#include <utils/Logger.h>
#include <utils/Types.h>

#include <imgui_backend/imgui_impl_wiiu.h>

#include <gx2/context.h>
#include <gx2/enum.h>
#include <gx2/mem.h>
#include <gx2/surface.h>
#include <vpad/input.h>

#include <wups.h>

GX2SurfaceFormat tvSurfaceFmt     = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
GX2SurfaceFormat drcSurfaceFmt    = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
GX2ContextState *originalCtxState = nullptr;
GX2ColorBuffer lastTVColBuffer;
GX2ColorBuffer lastDRCColBuffer;
bool init = false;

DECL_FUNCTION(void, GX2GetCurrentScanBuffer, GX2ScanTarget scanTgt, GX2ColorBuffer *colBuffer) {
    real_GX2GetCurrentScanBuffer(scanTgt, colBuffer);

    if (scanTgt == GX2_SCAN_TARGET_TV) memcpy(&lastTVColBuffer, colBuffer, sizeof(GX2ColorBuffer));
    else
        memcpy(&lastDRCColBuffer, colBuffer, sizeof(GX2ColorBuffer));
}

DECL_FUNCTION(void, GX2SetContextState, GX2ContextState *ctx) {
    real_GX2SetContextState(ctx);

    originalCtxState = ctx;
}

void drawIntoColorBuffer(const GX2ColorBuffer *colBuffer) {
    real_GX2SetContextState(gCtxState);
    GX2SetColorBuffer(colBuffer, GX2_RENDER_TARGET_0);

    gOverlay->draw(colBuffer->surface.width, colBuffer->surface.height);

    real_GX2SetContextState(originalCtxState);
}

DECL_FUNCTION(void, GX2SetupContextStateEx, GX2ContextState *ctx, BOOL unk1) {
    real_GX2SetupContextStateEx(ctx, unk1);

    originalCtxState = ctx;
}

DECL_FUNCTION(void, GX2SetTVBuffer, void *buffer, uint32_t bufferSize, int32_t renderMode, GX2SurfaceFormat surfaceFmt, GX2BufferingMode bufferingMode) {
    tvSurfaceFmt = surfaceFmt;

    return real_GX2SetTVBuffer(buffer, bufferSize, renderMode, surfaceFmt, bufferingMode);
}

DECL_FUNCTION(void, GX2SetDRCBuffer, void *buffer, uint32_t bufferSize, int32_t renderMode, GX2SurfaceFormat surfaceFmt, GX2BufferingMode bufferingMode) {
    drcSurfaceFmt = surfaceFmt;

    return real_GX2SetDRCBuffer(buffer, bufferSize, renderMode, surfaceFmt, bufferingMode);
}

DECL_FUNCTION(void, GX2CopyColorBufferToScanBuffer, const GX2ColorBuffer *colBuffer, GX2ScanTarget scanTgt) {
    gDrawAllowed = true;

    const GX2ColorBuffer _colBuffer = *colBuffer;
    drawIntoColorBuffer(&_colBuffer);

    real_GX2CopyColorBufferToScanBuffer(&_colBuffer, scanTgt);
}

DECL_FUNCTION(void, GX2Init, uint32_t attrs) {
    real_GX2Init(attrs);

    if (!init) {
        if (!gCtxState) OSFatal("Failed to alloc gCtxState");
        real_GX2SetupContextStateEx(gCtxState, GX2_TRUE);

        GX2Invalidate(GX2_INVALIDATE_MODE_CPU, gCtxState, sizeof(GX2ContextState));

        init = true;
    }
}

DECL_FUNCTION(void, GX2MarkScanBufferCopied, GX2ScanTarget scanTgt) {
    gDrawAllowed = true;

    const GX2ColorBuffer *colBuffer = (scanTgt == GX2_SCAN_TARGET_TV) ? &lastTVColBuffer : &lastDRCColBuffer;
    drawIntoColorBuffer(colBuffer);

    real_GX2MarkScanBufferCopied(scanTgt);
}

DECL_FUNCTION(void, GX2SwapScanBuffers) {
    real_GX2SwapScanBuffers();

    gOverlay->update();
}

DECL_FUNCTION(int32_t, VPADRead, VPADChan chan, VPADStatus *input, uint32_t count, VPADReadError *err) {
    VPADReadError realErr;
    const int32_t res = real_VPADRead(chan, input, count, &realErr);
    if (res > 0 && realErr == VPAD_READ_SUCCESS)
        if (ImGui_ImplWiiU_ProcessVPADInput(&input[0]))
            for (uint32_t i = 0; i < count; i++) memset(&input[i], 0, sizeof(VPADStatus));

    if (err) *err = realErr;

    return res;
}

DECL_FUNCTION(void, WPADRead, WPADChan chan, WPADStatus *input) {
    real_WPADRead(chan, input);

    auto *pro = reinterpret_cast<WPADStatusProController *>(input);
    if (pro->core.extensionType == WPAD_EXT_PRO_CONTROLLER)
        if (ImGui_ImplWiiU_ProcessWPADInput(pro)) memset(&pro->buttons, 0, sizeof(pro->buttons));
}

WUPS_MUST_REPLACE(GX2GetCurrentScanBuffer, WUPS_LOADER_LIBRARY_GX2, GX2GetCurrentScanBuffer);
WUPS_MUST_REPLACE(GX2SetContextState, WUPS_LOADER_LIBRARY_GX2, GX2SetContextState);
WUPS_MUST_REPLACE(GX2SetupContextStateEx, WUPS_LOADER_LIBRARY_GX2, GX2SetupContextStateEx);
WUPS_MUST_REPLACE(GX2SetTVBuffer, WUPS_LOADER_LIBRARY_GX2, GX2SetTVBuffer);
WUPS_MUST_REPLACE(GX2SetDRCBuffer, WUPS_LOADER_LIBRARY_GX2, GX2SetDRCBuffer);
WUPS_MUST_REPLACE(GX2CopyColorBufferToScanBuffer, WUPS_LOADER_LIBRARY_GX2, GX2CopyColorBufferToScanBuffer);
WUPS_MUST_REPLACE(GX2Init, WUPS_LOADER_LIBRARY_GX2, GX2Init);
WUPS_MUST_REPLACE(GX2MarkScanBufferCopied, WUPS_LOADER_LIBRARY_GX2, GX2MarkScanBufferCopied);
WUPS_MUST_REPLACE(GX2SwapScanBuffers, WUPS_LOADER_LIBRARY_GX2, GX2SwapScanBuffers);

WUPS_MUST_REPLACE(VPADRead, WUPS_LOADER_LIBRARY_VPAD, VPADRead);
WUPS_MUST_REPLACE(WPADRead, WUPS_LOADER_LIBRARY_PADSCORE, WPADRead);
