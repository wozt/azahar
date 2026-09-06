#pragma once

#include <cstdint>

/* GLuint, spelled out rather than included: this header is also pulled
 * in by the audio and input code, which has no business dragging in an
 * OpenGL loader to learn what an unsigned int is. */
using BsTextureHandle = unsigned int;

namespace Frontend {
class EmuWindow;
}
namespace Layout {
struct FramebufferLayout;
}

/*
 * The Azahar side of bottom_screen_server.
 *
 * Everything below this header is plain C, shared with the standalone
 * server and with the melonDS and Cemu backends. This file and its .cpp
 * are the only C++ in the path, and only because Azahar's API is C++.
 *
 * Two hooks, both in code that already runs every frame:
 *
 *   video  after each renderer has loaded its screens, where
 *          screen_infos[2] holds the bottom one -- fb_id 1, the one
 *          color_fill_bottom applies to.
 *
 *   input  EmuWindow's TouchPressed/TouchMoved/TouchReleased, the same
 *          calls the mouse makes, so the emulated console cannot tell
 *          the difference.
 *
 * Azahar has three renderers and the bridge belongs to none of them,
 * which is why it sits here rather than under renderer_opengl. Each one
 * gets the bottom screen into ordinary RGBA its own way and hands it
 * over; everything after that is shared.
 */

namespace BottomScreen {

/* Idempotent. Nothing opens until a frame has actually been submitted,
 * so a running Azahar with no game listens on nothing. */
void Start();
void Stop();
bool IsRunning();

/*
 * The OpenGL renderer's way in: the bottom screen is a GL texture, so
 * the readback happens here where the GL loader is already present.
 *
 * The 3DS bottom screen is 320x240; a resolution scale makes the texture
 * larger and the stream simply arrives sharper.
 */
void SubmitBottomScreenGL(BsTextureHandle texture);

/*
 * The way in for everybody else: plain RGBA the caller already holds.
 * The software renderer decodes the framebuffer into exactly this, and
 * the Vulkan renderer copies its image into a staging buffer to get it.
 *
 * rotate is for callers whose pixels are still in the console's own
 * portrait orientation -- the panels are physically mounted sideways, so
 * a 320x240 screen arrives 240 wide by 320 tall. The software renderer
 * transposes as it decodes and passes false; the other two pass true.
 */
void SubmitBottomScreenRGBA(const void* rgba, int width, int height, bool rotate);

/*
 * Pushes what clients have sent into the window, once per frame from the
 * same place the picture is taken so the two stay on one clock.
 *
 * The layout is needed because TouchPressed takes window coordinates and
 * works out the screen itself -- passing console pixels straight in
 * would land every tap in the wrong place.
 */
void ApplyInput(Frontend::EmuWindow& window, const Layout::FramebufferLayout& layout);

/*
 * True while a client holds the given Settings::NativeButton. Merged
 * with the local mapping rather than replacing it, so a pad or keyboard
 * on the host keeps working while someone plays from a phone.
 */
bool IsButtonHeld(int nativeButton);

/*
 * The circle pad a client is pushing, -1..1 with y positive upwards.
 * False when it is left centred, so the local mapping keeps the stick.
 */
bool GetCirclePad(float& x, float& y);

/*
 * Sound as the DSP produces it: interleaved stereo PCM16 at the 3DS's
 * own 32728 Hz, which is resampled on the way into Opus.
 */
void SubmitAudio(const short* samples, int frames);

}
