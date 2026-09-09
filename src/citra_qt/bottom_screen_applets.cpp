#include "citra_qt/bottom_screen_applets.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "common/string_util.h"
#include "core/frontend/applets/mii_selector.h"
#include "core/frontend/applets/swkbd.h"
#include "core/hle/applets/mii_selector.h"
#include "video_core/bottom_screen_bridge.h"

/* C, and it says so nowhere itself: without this the names come out
 * mangled and the link fails on symbols that are plainly there. */
extern "C" {
#include "bs_protocol.h"
#include "bs_server.h"
}

namespace BottomScreen {
namespace {

/*
 * The one question in flight, and what to do with the answer.
 *
 * One at a time because a 3DS shows one applet at a time -- it is not
 * asking for a name and a Mii at once. Guarded because the answer
 * arrives on a client's receiving thread and is acted on from the
 * render thread, which is the one PollApplets runs on.
 */
std::mutex g_lock;
std::function<void(bool cancelled, int choice, const std::string& text)> g_finish;
std::uint16_t g_id = 0;

void Pending(std::uint16_t id,
             std::function<void(bool, int, const std::string&)> finish) {
    std::lock_guard lock{g_lock};
    g_id = id;
    g_finish = std::move(finish);
}

class Keyboard final : public Frontend::SoftwareKeyboard {
public:
    explicit Keyboard(std::shared_ptr<Frontend::SoftwareKeyboard> inner_)
        : inner{std::move(inner_)} {}

    void Execute(const Frontend::KeyboardConfig& config_) override {
        Frontend::SoftwareKeyboard::Execute(config_);

        // The game's own hint where it wrote one, because "Enter a name"
        // is worth more than "the 3DS wants something".
        const std::string title =
            config.hint_text.empty() ? "The game is asking for some text" : config.hint_text;

        BsServer* srv = Server();
        const std::uint16_t id =
            srv ? bs_server_prompt(srv, BS_PROMPT_TEXT, title.c_str(), nullptr, 0,
                                   config.max_text_length, config.multiline_mode)
                : 0;
        if (id == 0) {
            // Nobody watching: the dialog on this desktop, exactly as
            // before.
            inner->Execute(config_);
            return;
        }
        Pending(id, [this](bool cancelled, int, const std::string& text) {
            // Button 1 is the middle of three and the plain "OK" of two,
            // which is what a keyboard closing normally reports.
            Finalize(cancelled ? std::string{} : text, cancelled ? 0 : 1);
        });
    }

    void ShowError(const std::string& error) override { inner->ShowError(error); }

private:
    std::shared_ptr<Frontend::SoftwareKeyboard> inner;
};

class Selector final : public Frontend::MiiSelector {
public:
    explicit Selector(std::shared_ptr<Frontend::MiiSelector> inner_)
        : inner{std::move(inner_)} {}

    void Setup(const Frontend::MiiSelectorConfig& config_) override {
        Frontend::MiiSelector::Setup(config_);

        // The same list the dialog shows, in the same order, so an index
        // means the same thing either way.
        miis.clear();
        labels.clear();
        miis.push_back(HLE::Applets::MiiSelector::GetStandardMiiResult().selected_mii_data);
        labels.emplace_back("Standard Mii");
        for (const auto& mii : Frontend::LoadMiis()) {
            miis.push_back(mii);
            labels.push_back(Common::UTF16BufferToUTF8(mii.mii_name));
        }

        std::vector<const char*> ptrs;
        ptrs.reserve(labels.size());
        for (const auto& l : labels) {
            ptrs.push_back(l.c_str());
        }

        const std::string title =
            (config.title.empty() || config.title.at(0) == '\0') ? "Choose a Mii" : config.title;

        BsServer* srv = Server();
        const std::uint16_t id =
            srv ? bs_server_prompt(srv, BS_PROMPT_CHOICE, title.c_str(), ptrs.data(),
                                   static_cast<int>(ptrs.size()), 0, 0)
                : 0;
        if (id == 0) {
            inner->Setup(config_);
            return;
        }
        Pending(id, [this](bool cancelled, int choice, const std::string&) {
            if (cancelled || choice < 0 || choice >= static_cast<int>(miis.size())) {
                Finalize(1, Mii::MiiData{});
            } else {
                Finalize(0, miis[static_cast<std::size_t>(choice)]);
            }
        });
    }

private:
    std::shared_ptr<Frontend::MiiSelector> inner;
    std::vector<Mii::MiiData> miis;
    std::vector<std::string> labels;
};

} // namespace

std::shared_ptr<Frontend::MiiSelector> WrapMiiSelector(
    std::shared_ptr<Frontend::MiiSelector> inner) {
    return std::make_shared<Selector>(std::move(inner));
}

std::shared_ptr<Frontend::SoftwareKeyboard> WrapKeyboard(
    std::shared_ptr<Frontend::SoftwareKeyboard> inner) {
    return std::make_shared<Keyboard>(std::move(inner));
}

void PollApplets() {
    std::uint16_t id;
    {
        std::lock_guard lock{g_lock};
        id = g_id;
    }
    if (id == 0) {
        return;
    }

    BsServer* srv = Server();
    if (!srv) {
        return;
    }

    char text[512] = "";
    int choice = 0;
    const int state = bs_server_prompt_poll(srv, id, text, sizeof(text), &choice);
    if (state == 0) {
        return; // still being answered
    }

    std::function<void(bool, int, const std::string&)> finish;
    {
        std::lock_guard lock{g_lock};
        if (g_id != id) {
            return; // somebody got there first
        }
        finish = std::move(g_finish);
        g_id = 0;
        g_finish = nullptr;
    }
    if (finish) {
        finish(state != 1, choice, std::string{text});
    }
}

} // namespace BottomScreen
