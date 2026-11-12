// The game folder, and the setup that fills it: native message boxes and file dialogs, and a small
// SDL window for the progress (and for the questions where no message box is available).
#include "setup.hpp"

#include "install.hpp"

#include <SDL3/SDL.h>
#include <math.h>
#include <algorithm>
#include <stdlib.h>

using namespace setup;

namespace
{
const char kTitle[] = "GTA2 setup";

// Shown for "I don't have the game".
const char kNoGameText[] =
    "This port does not include GTA2's game files. To play, you need your own copy of GTA2 for "
    "Windows: the original CD, a Steam copy (no longer sold since 2013), or the installer Rockstar Games "
    "gave away for free in 2004 in its \"Rockstar Classics\" series.\n\n"
    "Rockstar no longer offers it, but the Internet Archive keeps a copy of the Rockstar Classics "
    "installers. Download GTA2.exe from there, then choose \"Install from a file\" and pick it.";
const char kNoGameUrl[] = "https://archive.org/details/rockstar-classics";

const SDL_DialogFileFilter kFileFilters[] = {
    {"GTA2 installer or disc image", "exe;iso;zip;7z"},
    {"All files", "*"},
};

#if !defined(_WIN32) && !defined(__APPLE__)
const unsigned char kLogoPng[] = {
#embed "../resources/reg2_256.png"
};

// The reG2 logo, 256 px.
SDL_Surface* LoadLogo()
{
    return SDL_LoadPNG_IO(SDL_IOFromConstMem(kLogoPng, sizeof(kLogoPng)), true);
}
#endif

bool EnvSet(const char* name)
{
    const char* v = SDL_getenv(name);
    return v && *v && *v != '0';
}

// Lines of at most width characters, cut at spaces (a longer word only if breakWords).
std::string Wrap(const std::string& text, size_t width, bool breakWords)
{
    std::string out, line;
    for (size_t pos = 0; pos <= text.size(); pos++)
    {
        const char c = pos < text.size() ? text[pos] : '\n';
        if (c != '\n')
        {
            line += c;
            if (line.size() <= width)
            {
                continue;
            }
            size_t cut = line.rfind(' ', width);
            cut = cut != std::string::npos && cut > 0 ? cut : breakWords ? width : line.find(' ', width);
            if (cut == std::string::npos)
            {
                continue;
            }
            out += line.substr(0, cut) + '\n';
            line.erase(0, cut + (line[cut] == ' '));
            continue;
        }
        out += line + (pos < text.size() ? "\n" : "");
        line.clear();
    }
    return out;
}

// A window drawn with SDL's renderer and its 8x8 font, in units of font pixels.
class SetupWindow
{
public:
    ~SetupWindow()
    {
        SDL_DestroyRenderer(mRenderer);
        SDL_DestroyWindow(mWindow);
        if (mVideo)
        {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
    }

    bool Open(int w, int h)
    {
        mVideo = SDL_InitSubSystem(SDL_INIT_VIDEO);
        const float content = mVideo ? SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay()) : 1.0f;
        mScale = fmaxf(2.0f, roundf(2.0f * (content > 0.0f ? content : 1.0f)));
        if (!mVideo || !SDL_CreateWindowAndRenderer(kTitle, (int)(w * mScale), (int)(h * mScale), 0, &mWindow, &mRenderer))
        {
            SDL_Log("Setup window: %s", SDL_GetError());
            return false;
        }
        port_SetWindowIcon(mWindow);
        SDL_SetRenderScale(mRenderer, mScale, mScale);
        SDL_SetRenderVSync(mRenderer, 1);
        return true;
    }

    void Color(Uint32 rgb) { SDL_SetRenderDrawColor(mRenderer, rgb >> 16, (rgb >> 8) & 255, rgb & 255, 255); }

    void Rect(float x, float y, float w, float h, Uint32 fill, Uint32 border)
    {
        const SDL_FRect r = {x, y, w, h};
        Color(fill);
        SDL_RenderFillRect(mRenderer, &r);
        Color(border);
        SDL_RenderRect(mRenderer, &r);
    }

    void Text(float x, float y, const std::string& s, Uint32 rgb)
    {
        Color(rgb);
        SDL_RenderDebugText(mRenderer, x, y, s.c_str());
    }

    // GTA2_SETUP_CAPTURE=<folder>: frames written there as BMP, for tests without a screen.
    void Present(bool capture)
    {
        static int sCaptures;
        const char* dir = SDL_getenv("GTA2_SETUP_CAPTURE");
        SDL_Surface* s = capture && dir ? SDL_RenderReadPixels(mRenderer, NULL) : NULL;
        if (s)
        {
            SDL_SaveBMP(s, (std::string(dir) + "/setup_" + std::to_string(sCaptures++) + ".bmp").c_str());
            SDL_DestroySurface(s);
        }
        SDL_RenderPresent(mRenderer);
        Color(0xEEEEEE);
        SDL_RenderClear(mRenderer);
    }

    float mScale = 2.0f;

private:
    bool mVideo = false;
    SDL_Window* mWindow = NULL;
    SDL_Renderer* mRenderer = NULL;
};

// The question in a window of our own, without a native message box (GTA2_SETUP_DRAWN=1 forces
// it): a click or the button's number answers it, Escape or closing gives escapeButton.
int DrawnBox(const std::string& message, const std::vector<const char*>& buttons, int escapeButton)
{
    const int w = 340;
    const std::string text = Wrap(message, (w - 16) / 8, true);
    const int top = 18 + 10 * (int)std::count(text.begin(), text.end(), '\n'), h = top + 20 * (int)buttons.size() + 6;
    SetupWindow win;
    if (!win.Open(w, h))
    {
        return escapeButton;
    }
    for (bool first = true;; first = false)
    {
        float mx, my;
        SDL_GetMouseState(&mx, &my);
        const int hover = mx / win.mScale >= 8 && mx / win.mScale < w - 8 ? (int)floorf((my / win.mScale - top) / 20) : -1;
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ||
                (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE))
            {
                return escapeButton;
            }
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key >= SDLK_1 && e.key.key < SDLK_1 + (SDL_Keycode)buttons.size())
            {
                return (int)(e.key.key - SDLK_1);
            }
            if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && hover >= 0 && hover < (int)buttons.size())
            {
                return hover;
            }
        }
        size_t line = 0;
        for (size_t start = 0; start < text.size(); line++)
        {
            const size_t end = std::min(text.find('\n', start), text.size());
            win.Text(8, 10 + 10.0f * line, text.substr(start, end - start), 0x202020);
            start = end + 1;
        }
        for (size_t i = 0; i < buttons.size(); i++)
        {
            const std::string label = std::to_string(i + 1) + ". " + buttons[i];
            win.Rect(8, top + 20.0f * i, w - 16, 16, (int)i == hover ? 0xD6E6FA : 0xFFFFFF, 0x9A9A9A);
            win.Text(roundf((w - label.size() * 8.0f) / 2), top + 20.0f * i + 4, label, 0x202020);
        }
        win.Present(first);
        SDL_Delay(10);
    }
}

// A native message box; buttons are numbered from 0, closing it gives escapeButton.
int Ask(SDL_MessageBoxFlags flags, const std::string& message, const std::vector<const char*>& buttons,
        int returnButton, int escapeButton)
{
    if (EnvSet("GTA2_NO_MESSAGEBOX"))
    {
        SDL_Log("%s: %s", kTitle, message.c_str());
        return escapeButton;
    }
    if (!EnvSet("GTA2_SETUP_DRAWN"))
    {
        std::vector<SDL_MessageBoxButtonData> data;
        for (int i = 0; i < (int)buttons.size(); i++)
        {
            data.push_back({(i == returnButton ? SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT : 0u) |
                                (i == escapeButton ? SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT : 0u),
                            i, buttons[i]});
        }
#if defined(_WIN32) || defined(__APPLE__)
        const std::string text = message;
#else
        // zenity and SDL's X11 box do not wrap lines.
        const std::string text = Wrap(message, 72, false);
#endif
        const SDL_MessageBoxData box = {flags | SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT, NULL, kTitle, text.c_str(),
                                        (int)data.size(), data.data(), NULL};
        int id = -1;
        if (SDL_ShowMessageBox(&box, &id))
        {
            return id < 0 ? escapeButton : id;
        }
        SDL_Log("No message box (%s): drawing the question", SDL_GetError());
    }
    return DrawnBox(message, buttons, escapeButton);
}

struct Pick
{
    std::atomic<int> mState{0}; // 1 done (mPath empty if cancelled), 2 no dialog
    std::string mPath, mError;
};

// A folder or a file from the system's dialog; empty if cancelled.
std::string PickPath(bool folder)
{
    Pick pick;
    const SDL_DialogFileCallback done = [](void* pUser, const char* const* pList, int) {
        Pick* p = static_cast<Pick*>(pUser);
        p->mError = pList ? "" : SDL_GetError();
        p->mPath = pList && pList[0] ? pList[0] : "";
        p->mState = pList ? 1 : 2;
    };
    if (folder)
    {
        SDL_ShowOpenFolderDialog(done, &pick, NULL, NULL, false);
    }
    else
    {
        SDL_ShowOpenFileDialog(done, &pick, NULL, kFileFilters, SDL_arraysize(kFileFilters), NULL, false);
    }
    while (pick.mState == 0)
    {
        SDL_PumpEvents();
        SDL_Delay(15);
    }
    if (pick.mState == 2)
    {
        Ask(SDL_MESSAGEBOX_ERROR,
            "No file dialog could be opened (" + pick.mError +
                ").\n\nOn Linux this needs xdg-desktop-portal or zenity. The path can also be given in the "
                "GTA2_SETUP_PATH environment variable.",
            {"OK"}, 0, 0);
    }
    return pick.mPath;
}

struct Job
{
    fs::path mSource;
    Progress mProgress;
    InstallResult mResult;
    std::atomic<bool> mDone{false};
};

// The copy runs in a thread while the window shows its progress; closing the window cancels it.
InstallResult RunInstall(const fs::path& source)
{
    Job job;
    job.mSource = source;
    SDL_Thread* thread = SDL_CreateThread(
        [](void* p) {
            Job* job = static_cast<Job*>(p);
            job->mResult = Install(job->mSource, job->mProgress);
            job->mDone = true;
            return 0;
        },
        "gta2 setup", &job);
    const int w = 300, h = 66;
    SetupWindow win;
    const bool shown = thread && win.Open(w, h);
    for (Uint64 lastCapture = 0; thread && !job.mDone;)
    {
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            job.mProgress.mCancel = job.mProgress.mCancel || e.type == SDL_EVENT_QUIT ||
                                    e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ||
                                    (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE);
        }
        const uint64_t done = job.mProgress.mDone >> 20, total = job.mProgress.mTotal >> 20;
        const std::string amount = total ? std::to_string(done) + " / " + std::to_string(total) + " MB" : "";
        std::string status = job.mProgress.Status();
        const size_t room = (w - 16) / 8 - amount.size() - 2;
        status = status.size() > room ? "..." + status.substr(status.size() - room + 3) : status;
        if (shown)
        {
            win.Text(8, 8, job.mProgress.mCancel ? "Cancelling..." : "Copying GTA2's game files...", 0x202020);
            win.Rect(8, 24, w - 16, 14, 0xFFFFFF, 0x9A9A9A);
            win.Rect(9, 25, total ? (w - 18) * (float)done / total : 0, 12, 0x3584E4, 0x3584E4);
            win.Text(8, 48, status, 0x505050);
            win.Text(w - 8 - amount.size() * 8.0f, 48, amount, 0x505050);
            const bool capture = lastCapture == 0 || SDL_GetTicks() - lastCapture >= 1000;
            lastCapture = capture ? SDL_GetTicks() : lastCapture;
            win.Present(capture);
        }
        SDL_Delay(10);
    }
    if (thread)
    {
        SDL_WaitThread(thread, NULL);
    }
    else
    {
        job.mResult = Install(source, job.mProgress);
    }
    return job.mResult;
}

// The questions until the game has its data: false to quit.
bool SetupLoop(const std::string& gameDir)
{
    // GTA2_SETUP_PATH: the folder or file to install from, instead of the first question and the dialog.
    const char* envPath = SDL_getenv("GTA2_SETUP_PATH");
    std::string path = envPath ? envPath : "";
    for (bool fromEnv = !path.empty();; fromEnv = false, path.clear())
    {
        const DataCheck check = CheckGameData(".");
        if (path.empty() && !check.mMissing.empty())
        {
            const int choice = Ask(SDL_MESSAGEBOX_INFORMATION,
                                   "GTA2's game files were not found.\n\nThis port does not include them: they come "
                                   "from your own copy of GTA2, an installed game folder, the game's CD or its "
                                   "installer. They will be copied to:\n" + gameDir,
                                   {"Use an existing install...", "Install from a file...", "I don't have the game",
                                    "Quit"},
                                   0, 3);
            if (choice < 0 || choice == 3)
            {
                return false;
            }
            if (choice == 2)
            {
                if (Ask(SDL_MESSAGEBOX_INFORMATION, kNoGameText, {"Download the GTA2 freeware from archive.org", "Back"}, 1, 1) == 0)
                {
                    SDL_OpenURL(kNoGameUrl);
                }
                continue;
            }
            path = PickPath(choice == 0);
        }
        else if (path.empty())
        {
            const bool music = check.mMusicFound >= check.mMusicTotal;
            const int choice = Ask(
                music ? SDL_MESSAGEBOX_INFORMATION : SDL_MESSAGEBOX_WARNING,
                "GTA2's game files are installed in:\n" + gameDir + "\n\n" +
                    (music ? std::string("Choose a folder or a file to replace them.")
                           : "The music was not found (" + std::to_string(check.mMusicFound) + " of " +
                                 std::to_string(check.mMusicTotal) +
                                 " tracks): the game runs without music and radio. The music is in the GTAudio "
                                 "folder of the original CD, or in the installer: choose one to add it."),
                {"Use a folder...", "Use a file...", "Play"}, 2, 2);
            if (choice == 2)
            {
                return true;
            }
            path = PickPath(choice == 0);
        }
        if (path.empty())
        {
            continue;
        }
        const InstallResult result = RunInstall(PathFromUtf8(path));
        if (!result.mOk && !result.mCancelled)
        {
            Ask(SDL_MESSAGEBOX_ERROR, result.mError, {"OK"}, 0, 0);
        }
        if (fromEnv && !result.mOk)
        {
            return false;
        }
        if (result.mOk && result.mCheck.mMusicFound >= result.mCheck.mMusicTotal)
        {
            return true;
        }
    }
}

bool HasGameData(const fs::path& dir)
{
    fs::path found;
    return FindNoCase(dir, "data/fstyle.sty", &found);
}

// $GTA2_DIR, else the current folder or the executable's when it holds the game's data, else the
// per-user folder (empty if none).
fs::path GameFolder(bool* pPerUser)
{
    *pPerUser = false;
    if (const char* dir = SDL_getenv("GTA2_DIR"))
    {
        return PathFromUtf8(dir);
    }
    std::error_code ec;
    const char* base = SDL_GetBasePath();
    if (HasGameData(fs::current_path(ec)) || !base)
    {
        return fs::current_path(ec);
    }
    if (HasGameData(PathFromUtf8(base)))
    {
        return PathFromUtf8(base);
    }
    char* pref = SDL_GetPrefPath("", "reG2");
    const fs::path dir = PathFromUtf8(pref ? pref : "");
    SDL_free(pref);
    *pPerUser = true;
    return dir;
}
} // namespace

void port_SetWindowIcon(SDL_Window* pWindow)
{
#if !defined(_WIN32) && !defined(__APPLE__)
    if (SDL_Surface* pLogo = LoadLogo())
    {
        SDL_SetWindowIcon(pWindow, pLogo);
        SDL_DestroySurface(pLogo);
    }
#endif
}

bool port_PrepareGameFolder(bool forceSetup)
{
    bool perUser;
    const fs::path dir = GameFolder(&perUser);
    std::error_code ec;
    fs::current_path(dir, ec);
    if (ec || dir.empty())
    {
        SDL_Log("Cannot enter the game folder %s: %s", PathToUtf8(dir).c_str(), ec ? ec.message().c_str() : SDL_GetError());
        return false;
    }
    // Folders the game writes to and does not create.
    fs::path found;
    for (const char* sub : {"player", "test"})
    {
        if (!FindNoCase(".", sub, &found))
        {
            fs::create_directories(sub, ec);
        }
    }
    if (!perUser || (!forceSetup && CheckGameData(".").mMissing.empty()))
    {
        return true;
    }
    SDL_InitSubSystem(SDL_INIT_EVENTS);
    const bool ok = SetupLoop(PathToUtf8(dir));
    SDL_QuitSubSystem(SDL_INIT_EVENTS);
    return ok;
}
