// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/color.h"
#include "core/core.h"
#include "video_core/gpu.h"
#include "video_core/pica/pica_core.h"
#include "video_core/renderer_software/renderer_software.h"
#ifdef BOTTOM_SCREEN_ENABLED
#include "core/frontend/emu_window.h"
#include "video_core/bottom_screen_bridge.h"
#endif

namespace SwRenderer {

RendererSoftware::RendererSoftware(Core::System& system, Pica::PicaCore& pica_,
                                   Frontend::EmuWindow& window)
    : VideoCore::RendererBase{system, window, nullptr}, memory{system.Memory()}, pica{pica_},
      rasterizer{memory, pica} {}

RendererSoftware::~RendererSoftware() = default;

void RendererSoftware::SwapBuffers() {
    system.perf_stats->StartSwap();
    PrepareRenderTarget();
    system.perf_stats->EndSwap();
    EndFrame();
}

void RendererSoftware::PrepareRenderTarget() {
    const auto& regs_lcd = pica.regs_lcd;
    for (u32 i = 0; i < 3; i++) {
        const u32 fb_id = i == 2 ? 1 : 0;

        const auto color_fill = fb_id == 0 ? regs_lcd.color_fill_top : regs_lcd.color_fill_bottom;
        LoadFBToScreenInfo(i, color_fill);
    }

#ifdef BOTTOM_SCREEN_ENABLED
    /*
     * screen_infos[2] is the bottom screen. This renderer decodes the
     * console's framebuffer straight into RGBA in RAM, and transposes as
     * it goes -- the inner loop writes (x * height + y) -- so what comes
     * out is already landscape and needs no rotation.
     */
    const auto& bottom = screen_infos[2];
    if (!bottom.pixels.empty()) {
        BottomScreen::SubmitBottomScreenRGBA(bottom.pixels.data(),
                                             static_cast<int>(bottom.height),
                                             static_cast<int>(bottom.width), false);
        BottomScreen::ApplyInput(render_window, secondary_window);
    }
    /* And the top screen, for a client that asked for it. Guarded
     * separately rather than nested: this renderer keeps both, and
     * whether the bottom one has pixels this frame says nothing about
     * the other. */
    const auto& top = screen_infos[0];
    if (BottomScreen::WantsTopScreen() && !top.pixels.empty()) {
        BottomScreen::SubmitTopScreenRGBA(top.pixels.data(),
                                          static_cast<int>(top.height),
                                          static_cast<int>(top.width), false);
    }
#endif
}

void RendererSoftware::LoadFBToScreenInfo(int i, const Pica::ColorFill& color_fill) {
    const u32 fb_id = i == 2 ? 1 : 0;
    const auto& framebuffer = pica.regs.framebuffer_config[fb_id];
    auto& info = screen_infos[i];

    const PAddr framebuffer_addr =
        framebuffer.active_fb == 0 ? framebuffer.address_left1 : framebuffer.address_left2;
    const s32 bpp = Pica::BytesPerPixel(framebuffer.color_format);
    const u8* framebuffer_data = memory.GetPhysicalPointer(framebuffer_addr);

    const s32 pixel_stride = framebuffer.stride / bpp;
    info.height = framebuffer.height;
    info.width = pixel_stride;
    info.pixels.resize(info.width * info.height * 4);

    for (u32 y = 0; y < info.height; y++) {
        for (u32 x = 0; x < info.width; x++) {
            const u8* pixel = framebuffer_data + (y * pixel_stride + pixel_stride - x) * bpp;
            Common::Vec4 color = [&] {
                if (color_fill.is_enabled) {
                    return Common::Vec4<u8>(color_fill.color_r, color_fill.color_g,
                                            color_fill.color_b, 255);
                }

                switch (framebuffer.color_format) {
                case Pica::PixelFormat::RGBA8:
                    return Common::Color::DecodeRGBA8(pixel);
                case Pica::PixelFormat::RGB8:
                    return Common::Color::DecodeRGB8(pixel);
                case Pica::PixelFormat::RGB565:
                    return Common::Color::DecodeRGB565(pixel);
                case Pica::PixelFormat::RGB5A1:
                    return Common::Color::DecodeRGB5A1(pixel);
                case Pica::PixelFormat::RGBA4:
                    return Common::Color::DecodeRGBA4(pixel);
                }
                UNREACHABLE();
            }();
            const u32 output_offset = (x * info.height + y) * 4;
            u8* dest = info.pixels.data() + output_offset;
            std::memcpy(dest, color.AsArray(), sizeof(color));
        }
    }
}

} // namespace SwRenderer
