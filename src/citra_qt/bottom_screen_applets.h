// Asking whoever is watching, instead of whoever is sitting here.
//
// A 3DS stops and asks for a name or a Mii, and Azahar answers that with
// a dialog on this desktop. Streamed to a phone in another room, that
// dialog is somewhere nobody can see while the game waits for ever.
//
// These wrap the Qt applets rather than replacing them: the question
// goes to the clients when there are any, and falls through to the
// dialog when there are not. Nobody watching is the ordinary case, and
// it should behave exactly as it did before this existed.

#pragma once

#include <memory>

namespace Frontend {
class MiiSelector;
class SoftwareKeyboard;
} // namespace Frontend

namespace BottomScreen {

std::shared_ptr<Frontend::MiiSelector> WrapMiiSelector(
    std::shared_ptr<Frontend::MiiSelector> inner);
std::shared_ptr<Frontend::SoftwareKeyboard> WrapKeyboard(
    std::shared_ptr<Frontend::SoftwareKeyboard> inner);

// Called once a frame from the bridge. An answer arrives on a client's
// own thread; this is where it is handed to the applet, on a thread the
// emulator already owns.
void PollApplets();

} // namespace BottomScreen
