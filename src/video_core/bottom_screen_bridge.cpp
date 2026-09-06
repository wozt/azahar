#include "video_core/bottom_screen_bridge.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "common/settings.h"
#include "core/frontend/emu_window.h"
#include "core/frontend/framebuffer_layout.h"

extern "C" {
#include "bs_mailbox.h"
#include "bs_protocol.h"
#include "bs_server.h"
#include "bs_source.h"
}

namespace BottomScreen {

namespace {
    BsSource* g_source = nullptr;
    BsServer* g_server = nullptr;
    bool      g_tried  = false;
    int       g_width  = 0;
    int       g_height = 0;
    std::vector<std::uint8_t> g_pixels;
    std::vector<std::uint8_t> g_rotated;
    bool      g_touching = false;
    std::uint32_t g_held = 0;      /* bit per Settings::NativeButton */
    float     g_pad_x = 0.0f;
    float     g_pad_y = 0.0f;


    /*
     * BsButton -> the 3DS pad bit. The console has no ZL/ZR on an Old
     * 3DS and no HOME the emulator exposes here, so those map to
     * nothing and are dropped rather than treated as an error -- a
     * shared protocol means clients will send them.
     */
    int PadBit(int bsButton) {
        using namespace Settings;
        switch (bsButton) {
        case BS_BTN_A:      return static_cast<int>(NativeButton::A);
        case BS_BTN_B:      return static_cast<int>(NativeButton::B);
        case BS_BTN_X:      return static_cast<int>(NativeButton::X);
        case BS_BTN_Y:      return static_cast<int>(NativeButton::Y);
        case BS_BTN_L:      return static_cast<int>(NativeButton::L);
        case BS_BTN_R:      return static_cast<int>(NativeButton::R);
        case BS_BTN_ZL:     return static_cast<int>(NativeButton::ZL);
        case BS_BTN_ZR:     return static_cast<int>(NativeButton::ZR);
        case BS_BTN_START:  return static_cast<int>(NativeButton::Start);
        case BS_BTN_SELECT: return static_cast<int>(NativeButton::Select);
        case BS_BTN_UP:     return static_cast<int>(NativeButton::Up);
        case BS_BTN_DOWN:   return static_cast<int>(NativeButton::Down);
        case BS_BTN_LEFT:   return static_cast<int>(NativeButton::Left);
        case BS_BTN_RIGHT:  return static_cast<int>(NativeButton::Right);
        default:            return -1;
        }
    }
}

void Start() {
    if (g_tried)
        return;
    g_tried = true;

    /* The variable wins over the setting: a scripted launch should be
     * able to turn this off without editing a config file somebody else
     * owns. Without one, the setting decides. */
    if (const char* off = std::getenv("BOTTOM_SCREEN"); off && !std::strcmp(off, "0"))
        return;
    if (!Settings::values.bottom_screen_enabled.GetValue())
        return;
    if (g_width <= 0 || g_height <= 0) {
        g_tried = false;   // nothing measured yet; wait for a frame
        return;
    }

    int port = Settings::values.bottom_screen_port.GetValue();
    if (const char* p = std::getenv("BOTTOM_SCREEN_PORT")) {
        const int v = std::atoi(p);
        if (v > 0 && v < 65536)
            port = v;
    }
    if (port <= 0 || port > 65535)
        port = BS_DEFAULT_PORT;

    /* The 3DS runs at 60 Hz and, unlike the Wii U, does not vary, so the
     * rate is not measured here. Sound is not wired yet: rate 0 tells
     * the client to draw no volume control rather than a dead one. */
    g_source = bs_mailbox_create(BS_CONSOLE_3DS, g_width, g_height, 60,
                                 BS_PIXFMT_RGBA, 32728, 2);
    if (!g_source) {
        std::fprintf(stderr, "bottom_screen: cannot create the frame mailbox\n");
        return;
    }

    BsServerConfig cfg;
    std::memset(&cfg, 0, sizeof(cfg));
    cfg.port = static_cast<std::uint16_t>(port);

    char err[256] = "";
    g_server = bs_server_create(g_source, &cfg, err, sizeof(err));
    if (!g_server) {
        std::fprintf(stderr, "bottom_screen: %s\n", err);
        g_source->destroy(g_source->self);
        std::free(g_source);
        g_source = nullptr;
    }
}

void Stop() {
    if (g_server) {
        bs_server_destroy(g_server);
        g_server = nullptr;
    }
    if (g_source) {
        g_source->destroy(g_source->self);
        std::free(g_source);
        g_source = nullptr;
    }
    g_tried = false;
    g_width = g_height = 0;
}

bool IsRunning() {
    return g_server != nullptr;
}

void SubmitBottomScreenGL(BsTextureHandle texture) {
    if (texture == 0)
        return;

    /*
     * Ask the texture its own size rather than trusting the width and
     * height beside it in ScreenInfo. Those describe the framebuffer the
     * console produced; the GL texture is allocated separately and need
     * not match. Sizing the readback from the wrong one writes past the
     * buffer, which is a heap corruption that shows up as a segfault
     * somewhere else entirely.
     */
    glBindTexture(GL_TEXTURE_2D, texture);
    GLint tw = 0, th = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th);
    if (tw <= 0 || th <= 0) {
        glBindTexture(GL_TEXTURE_2D, 0);
        return;
    }

    g_pixels.resize(static_cast<std::size_t>(tw) * th * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, g_pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    SubmitBottomScreenRGBA(g_pixels.data(), tw, th, true);
}

void SubmitBottomScreenRGBA(const void* rgba, int width, int height, bool rotate) {
    if (!rgba || width <= 0 || height <= 0)
        return;

    // Announced after any rotation: the stream is landscape whatever
    // orientation the renderer happened to hand over.
    const int out_w = rotate ? height : width;
    const int out_h = rotate ? width : height;

    if (!g_server) {
        g_width = out_w;
        g_height = out_h;
        Start();
        if (!g_server)
            return;
    } else if (out_w != g_width || out_h != g_height) {
        /*
         * The size changed: someone raised the internal resolution, a
         * title reconfigured its framebuffer while booting, or the
         * renderer itself was swapped. Either way the connection
         * survives -- the server renegotiates with whoever is watching
         * rather than dropping them over a setting.
         */
        if (bs_mailbox_resize(g_source, out_w, out_h)) {
            g_width = out_w;
            g_height = out_h;
        }
    }

    if (!rotate) {
        bs_mailbox_submit(g_source, rgba, width * 4);
        return;
    }

    /*
     * The 3DS stores its screens in portrait, the way the panels are
     * physically mounted, so the picture arrives 240 wide by 320 tall
     * for a screen that is 320 by 240. Rotating here rather than asking
     * every client to do it keeps the rotation in the one place that
     * knows why it exists.
     */
    const std::uint8_t* in = static_cast<const std::uint8_t*>(rgba);
    g_rotated.resize(static_cast<std::size_t>(width) * height * 4);
    for (int y = 0; y < height; y++) {
        const std::uint8_t* src = in + static_cast<std::size_t>(y) * width * 4;
        for (int x = 0; x < width; x++) {
            // (x, y) in the portrait picture becomes (y, width-1-x).
            const std::size_t dst = ((static_cast<std::size_t>(width - 1 - x) * height) + y) * 4;
            std::memcpy(&g_rotated[dst], src + static_cast<std::size_t>(x) * 4, 4);
        }
    }

    bs_mailbox_submit(g_source, g_rotated.data(), height * 4);
}

void ApplyInput(Frontend::EmuWindow& window, const Layout::FramebufferLayout& layout) {
    if (!g_server)
        return;

    BsInputState in;
    bs_mailbox_input(g_source, &in);

    /*
     * TouchPressed takes window coordinates and works out which screen
     * they fall on, so console pixels are mapped through the layout's
     * own bottom_screen rectangle. Passing them straight in would put
     * every tap in the wrong place, and further out the larger the
     * window.
     */
    if (in.touching) {
        const auto& r = layout.bottom_screen;
        /* Against the announced size, not the console's own: clients
         * work in the coordinate space the handshake gave them, and
         * that is the scaled one whenever the emulator renders larger.
         * Dividing by 320 here put every tap six times too close to the
         * top left. */
        const float fx = static_cast<float>(in.touch_x) / (g_width > 0 ? g_width : BS_3DS_WIDTH);
        const float fy = static_cast<float>(in.touch_y) / (g_height > 0 ? g_height : BS_3DS_HEIGHT);
        const unsigned x = r.left + static_cast<unsigned>(fx * r.GetWidth());
        const unsigned y = r.top + static_cast<unsigned>(fy * r.GetHeight());

        if (!g_touching) {
            window.TouchPressed(x, y);
            g_touching = true;
            static bool announced = false;
            if (!announced) {
                announced = true;
                std::fprintf(stderr, "bottom_screen: first touch from a client at %d,%d\n",
                             in.touch_x, in.touch_y);
            }
        } else {
            window.TouchMoved(x, y);
        }
    } else if (g_touching) {
        window.TouchReleased();
        g_touching = false;
    }

    std::uint32_t held = 0;
    for (int b = 1; b <= 15; b++) {
        if (!(in.buttons & (1u << (b - 1))))
            continue;
        const int bit = PadBit(b);
        if (bit >= 0 && bit < 32)
            held |= (1u << bit);
    }
    g_held = held;

    /* The circle pad, as a fraction of full deflection. */
    g_pad_x = static_cast<float>(in.axis[BS_AXIS_LEFT_X - 1]) / 32767.0f;
    g_pad_y = static_cast<float>(in.axis[BS_AXIS_LEFT_Y - 1]) / 32767.0f;
}

bool IsButtonHeld(int nativeButton) {
    if (!g_server || nativeButton < 0 || nativeButton >= 32)
        return false;
    const bool held = (g_held & (1u << nativeButton)) != 0;

    static bool announced = false;
    if (held && !announced) {
        announced = true;
        std::fprintf(stderr, "bottom_screen: first button from a client (id %d)\n",
                     nativeButton);
    }
    return held;
}

bool GetCirclePad(float& x, float& y) {
    if (!g_server)
        return false;
    if (g_pad_x == 0.0f && g_pad_y == 0.0f)
        return false;   // centred: leave the stick to the local mapping
    x = g_pad_x;
    y = g_pad_y;

    static bool announced = false;
    if (!announced) {
        announced = true;
        std::fprintf(stderr, "bottom_screen: first circle pad from a client at %.2f,%.2f\n",
                     x, y);
    }
    return true;
}

void SubmitAudio(const short* samples, int frames) {
    if (!g_server || !samples || frames <= 0)
        return;
    bs_mailbox_submit_audio(g_source, samples, frames);
}

}
