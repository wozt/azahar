#pragma once

#include <cstdint>
#include "video_core/renderer_opengl/gl_resource_manager.h"

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
 *   video  after PrepareRendertarget, where screen_infos[2] holds the
 *          bottom screen -- fb_id 1, the one color_fill_bottom applies
 *          to. Read back the way Azahar's own frame dumper does it.
 *
 *   input  EmuWindow's TouchPressed/TouchMoved/TouchReleased, the same
 *          calls the mouse makes, so the emulated console cannot tell
 *          the difference.
 */

namespace BottomScreen {

/* Idempotent. Nothing opens until a frame has actually been submitted,
 * so a running Azahar with no game listens on nothing. */
void Start();
void Stop();
bool IsRunning();

/*
 * Reads the bottom screen back off the GPU and hands it to the server.
 * The 3DS bottom screen is 320x240; a resolution scale makes the texture
 * larger and the stream simply arrives sharper.
 */
void SubmitBottomScreen(GLuint texture, int width, int height);

/*
 * Pushes what clients have sent into the window, once per frame from the
 * same place the picture is taken so the two stay on one clock.
 *
 * The layout is needed because TouchPressed takes window coordinates and
 * works out the screen itself -- passing console pixels straight in
 * would land every tap in the wrong place.
 */
void ApplyInput(Frontend::EmuWindow& window, const Layout::FramebufferLayout& layout);

}
