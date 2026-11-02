// The setup screens: native message boxes and file dialogs, and a small SDL window for the progress
// (and for the questions where no message box is available).
#include "setup.hpp"

#include "install.hpp"

#include <SDL3/SDL.h>
#include <atomic>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <system_error>
#include <vector>

using namespace setup;

namespace
{
const char kTitle[] = "GTA2 setup";

// Shown for "I don't have the game".
const char kNoGameText[] =
    "This port does not include GTA2's game files. To play, you need your own copy of GTA2 for "
    "Windows: the original CD, or the version Rockstar Games gave away for free in 2004 in its "
    "\"Rockstar Classics\" series.\n\n"
    "Rockstar's download page is no longer online. The Internet Archive keeps a copy of it:\n"
    "https://web.archive.org/web/20071024135959/http://www.rockstargames.com/classics/";
const char kNoGameUrl[] = "https://web.archive.org/web/20071024135959/http://www.rockstargames.com/classics/";

const char kPrefOrg[] = "";
const char kPrefApp[] = "gta2_re";

const SDL_DialogFileFilter kFileFilters[] = {
    {"GTA2 installer, archive or disc image", "exe;zip;7z;iso;bin"},
    {"All files", "*"},
};

fs::path sGameDir;
bool sPerUser;

bool EnvSet(const char* name)
{
    const char* v = SDL_getenv(name);
    return v && *v && *v != '0';
}

// A window drawn with SDL's renderer and its built-in 8x8 font, in units of font pixels.
class SetupWindow
{
public:
    ~SetupWindow()
    {
        if (mRenderer)
        {
            SDL_DestroyRenderer(mRenderer);
        }
        if (mWindow)
        {
            SDL_DestroyWindow(mWindow);
        }
        if (mVideo)
        {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
    }

    bool Open(int w, int h)
    {
        mVideo = SDL_InitSubSystem(SDL_INIT_VIDEO);
        if (!mVideo)
        {
            SDL_Log("Setup window: %s", SDL_GetError());
            return false;
        }
        const float content = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
        mScale = fmaxf(2.0f, roundf(2.0f * (content > 0.0f ? content : 1.0f)));
        mW = w;
        mH = h;
        if (!SDL_CreateWindowAndRenderer(kTitle, (int)(w * mScale), (int)(h * mScale), 0, &mWindow, &mRenderer))
        {
            SDL_Log("Setup window: %s", SDL_GetError());
            return false;
        }
        SDL_SetRenderScale(mRenderer, mScale, mScale);
        SDL_SetRenderVSync(mRenderer, 1);
        return true;
    }

    void Clear()
    {
        Color(0xEE, 0xEE, 0xEE);
        SDL_RenderClear(mRenderer);
    }

    void Color(Uint8 r, Uint8 g, Uint8 b) { SDL_SetRenderDrawColor(mRenderer, r, g, b, 255); }

    void Fill(float x, float y, float w, float h)
    {
        const SDL_FRect rect = {x, y, w, h};
        SDL_RenderFillRect(mRenderer, &rect);
    }

    void Frame(float x, float y, float w, float h)
    {
        const SDL_FRect rect = {x, y, w, h};
        SDL_RenderRect(mRenderer, &rect);
    }

    void Text(float x, float y, const std::string& s) { SDL_RenderDebugText(mRenderer, x, y, s.c_str()); }

    // GTA2_SETUP_CAPTURE=<folder>: frames written there as BMP, for tests without a screen.
    void Present(bool capture)
    {
        const char* dir = SDL_getenv("GTA2_SETUP_CAPTURE");
        if (capture && dir)
        {
            if (SDL_Surface* s = SDL_RenderReadPixels(mRenderer, NULL))
            {
                char path[1024];
                snprintf(path, sizeof(path), "%s/setup_%03d.bmp", dir, sCaptures++);
                SDL_SaveBMP(s, path);
                SDL_DestroySurface(s);
            }
        }
        SDL_RenderPresent(mRenderer);
    }

    float mScale = 2.0f;
    int mW = 0, mH = 0;
    SDL_Window* mWindow = NULL;
    SDL_Renderer* mRenderer = NULL;

private:
    bool mVideo = false;
    static int sCaptures;
};
int SetupWindow::sCaptures;

// Lines of at most width characters, cut at spaces; a longer word is cut only if breakWords.
std::vector<std::string> Wrap(const std::string& text, size_t width, bool breakWords)
{
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size())
    {
        size_t end = text.find('\n', start);
        if (end == std::string::npos)
        {
            end = text.size();
        }
        std::string para = text.substr(start, end - start);
        start = end + 1;
        while (para.size() > width)
        {
            size_t cut = para.rfind(' ', width);
            if (cut == std::string::npos || cut == 0)
            {
                cut = breakWords ? width : para.find(' ', width);
                if (cut == std::string::npos)
                {
                    break;
                }
            }
            lines.push_back(para.substr(0, cut));
            para.erase(0, cut + (para[cut] == ' ' ? 1 : 0));
        }
        lines.push_back(para);
    }
    return lines;
}

std::string Join(const std::vector<std::string>& lines)
{
    std::string s;
    for (const std::string& line : lines)
    {
        s += (s.empty() ? "" : "\n") + line;
    }
    return s;
}

// The question in a window of our own: no native message box (GTA2_SETUP_DRAWN=1 forces it).
int DrawnBox(const char* message, const std::vector<const char*>& buttons, int escapeButton)
{
    const int w = 340;
    const std::vector<std::string> lines = Wrap(message, (w - 16) / 8, true);
    const int buttonsTop = 10 + (int)lines.size() * 10 + 8;
    const int h = buttonsTop + (int)buttons.size() * 20 + 6;
    SetupWindow win;
    if (!win.Open(w, h))
    {
        return escapeButton;
    }
    int hover = 0;
    bool captured = false;
    for (;;)
    {
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
            {
                return escapeButton;
            }
            if (e.type == SDL_EVENT_KEY_DOWN)
            {
                if (e.key.key == SDLK_ESCAPE)
                {
                    return escapeButton;
                }
                if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER || e.key.key == SDLK_SPACE)
                {
                    return hover;
                }
                if (e.key.key == SDLK_DOWN || e.key.key == SDLK_TAB)
                {
                    hover = (hover + 1) % (int)buttons.size();
                }
                if (e.key.key == SDLK_UP)
                {
                    hover = (hover + (int)buttons.size() - 1) % (int)buttons.size();
                }
                if (e.key.key >= SDLK_1 && e.key.key < SDLK_1 + (SDL_Keycode)buttons.size())
                {
                    return (int)(e.key.key - SDLK_1);
                }
            }
            if (e.type == SDL_EVENT_MOUSE_MOTION || e.type == SDL_EVENT_MOUSE_BUTTON_UP)
            {
                const float x = (e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.x : e.button.x) / win.mScale;
                const float y = (e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.y : e.button.y) / win.mScale;
                const int i = (int)floorf((y - buttonsTop) / 20.0f);
                const bool on = x >= 8 && x < w - 8 && y >= buttonsTop && i < (int)buttons.size() &&
                                y - buttonsTop - i * 20.0f < 16.0f;
                if (on)
                {
                    hover = i;
                    if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT)
                    {
                        return i;
                    }
                }
            }
        }
        win.Clear();
        win.Color(0x20, 0x20, 0x20);
        for (size_t i = 0; i < lines.size(); i++)
        {
            win.Text(8, 10 + i * 10.0f, lines[i]);
        }
        for (size_t i = 0; i < buttons.size(); i++)
        {
            const float y = buttonsTop + i * 20.0f;
            if ((int)i == hover)
            {
                win.Color(0x35, 0x84, 0xE4);
                win.Fill(8, y, w - 16, 16);
                win.Color(0xFF, 0xFF, 0xFF);
            }
            else
            {
                win.Color(0xFF, 0xFF, 0xFF);
                win.Fill(8, y, w - 16, 16);
                win.Color(0x9A, 0x9A, 0x9A);
                win.Frame(8, y, w - 16, 16);
                win.Color(0x20, 0x20, 0x20);
            }
            const std::string label = std::to_string(i + 1) + ". " + buttons[i];
            win.Text(roundf((w - label.size() * 8.0f) / 2), y + 4, label);
        }
        win.Present(!captured);
        captured = true;
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
        for (size_t i = 0; i < buttons.size(); i++)
        {
            SDL_MessageBoxButtonFlags f = 0;
            f |= (int)i == returnButton ? SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT : 0;
            f |= (int)i == escapeButton ? SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT : 0;
            data.push_back({f, (int)i, buttons[i]});
        }
#if defined(_WIN32) || defined(__APPLE__)
        const std::string& text = message;
#else
        // zenity and SDL's X11 box do not wrap lines.
        const std::string text = Join(Wrap(message, 72, false));
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
    return DrawnBox(message.c_str(), buttons, escapeButton);
}

void Tell(SDL_MessageBoxFlags flags, const std::string& message)
{
    Ask(flags, message, {"OK"}, 0, 0);
}

struct Pick
{
    std::atomic<int> mState{0}; // 1 chosen, 2 dialog failed, 3 cancelled
    std::string mPath, mError;
};

void SDLCALL OnPicked(void* pUser, const char* const* pList, int)
{
    Pick* p = static_cast<Pick*>(pUser);
    if (!pList)
    {
        p->mError = SDL_GetError();
        p->mState = 2;
    }
    else if (!pList[0])
    {
        p->mState = 3;
    }
    else
    {
        p->mPath = pList[0];
        p->mState = 1;
    }
}

// A folder or a file from the system's dialog; empty if cancelled.
std::string PickPath(bool folder)
{
    Pick pick;
    if (folder)
    {
        SDL_ShowOpenFolderDialog(OnPicked, &pick, NULL, NULL, false);
    }
    else
    {
        SDL_ShowOpenFileDialog(OnPicked, &pick, NULL, kFileFilters, SDL_arraysize(kFileFilters), NULL, false);
    }
    while (pick.mState == 0)
    {
        SDL_PumpEvents();
        SDL_Delay(15);
    }
    if (pick.mState == 2)
    {
        Tell(SDL_MESSAGEBOX_ERROR, "No file dialog could be opened (" + pick.mError +
                                       ").\n\nOn Linux this needs xdg-desktop-portal or zenity. The path can also be "
                                       "given in the GTA2_SETUP_PATH environment variable.");
    }
    return pick.mPath;
}

struct Job
{
    fs::path mSource, mDest;
    Progress mProgress;
    InstallResult mResult;
    std::atomic<bool> mDone{false};
};

int SDLCALL InstallThread(void* pUser)
{
    Job* job = static_cast<Job*>(pUser);
    job->mResult = Install(job->mSource, job->mDest, job->mProgress);
    job->mDone = true;
    return 0;
}

std::string Megabytes(uint64_t bytes)
{
    return std::to_string((unsigned)((bytes + (1 << 19)) >> 20)) + " MB";
}

// The copy runs in a thread while the window shows its progress; closing the window cancels it.
InstallResult RunInstall(const fs::path& source, const fs::path& dest)
{
    Job job;
    job.mSource = source;
    job.mDest = dest;
    SDL_Thread* thread = SDL_CreateThread(InstallThread, "gta2 setup", &job);
    if (!thread)
    {
        InstallThread(&job);
        return job.mResult;
    }
    const int w = 300, h = 66;
    SetupWindow win;
    const bool shown = win.Open(w, h);
    Uint64 lastCapture = 0;
    bool captured = false;
    while (!job.mDone)
    {
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ||
                (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE))
            {
                job.mProgress.mCancel = true;
            }
        }
        if (!shown)
        {
            SDL_Delay(20);
            continue;
        }
        const uint64_t done = job.mProgress.mDone, total = job.mProgress.mTotal;
        const float frac = total ? (float)((double)done / (double)total) : 0.0f;
        std::string status = job.mProgress.Status();
        const std::string amount = total == 0                   ? ""
                                   : job.mProgress.mCountsFiles ? std::to_string(done) + " / " + std::to_string(total) + " files"
                                                                : Megabytes(done) + " / " + Megabytes(total);
        const size_t room = (w - 16) / 8 - amount.size() - 2;
        if (status.size() > room)
        {
            status = "..." + status.substr(status.size() - (room - 3));
        }
        win.Clear();
        win.Color(0x20, 0x20, 0x20);
        win.Text(8, 8, job.mProgress.mCancel ? "Cancelling..." : "Copying GTA2's game files...");
        win.Color(0xFF, 0xFF, 0xFF);
        win.Fill(8, 24, w - 16, 14);
        win.Color(0x35, 0x84, 0xE4);
        win.Fill(9, 25, (w - 18) * frac, 12);
        win.Color(0x9A, 0x9A, 0x9A);
        win.Frame(8, 24, w - 16, 14);
        win.Color(0x50, 0x50, 0x50);
        win.Text(8, 48, status);
        win.Text(w - 8 - amount.size() * 8.0f, 48, amount);
        const Uint64 now = SDL_GetTicks();
        const bool capture = !captured || now - lastCapture >= 1000;
        lastCapture = capture ? now : lastCapture;
        captured = true;
        win.Present(capture);
        SDL_Delay(10);
    }
    SDL_WaitThread(thread, NULL);
    return job.mResult;
}

std::string MusicMessage(const DataCheck& check)
{
    return "GTA2 is installed, but its music was not found (" + std::to_string(check.mMusicFound) + " of " +
           std::to_string(check.mMusicTotal) +
           " tracks): the game runs without music and radio.\n\n"
           "The music is on the original CD, in its GTAudio folder. Choose that folder (or the CD), or a disc "
           "image or archive that holds it, to add it now.";
}

bool SetupLoop(bool force)
{
    // GTA2_SETUP_PATH: the folder or file to install from, instead of the first question and the dialog.
    const char* env = SDL_getenv("GTA2_SETUP_PATH");
    std::string envPath = env ? env : "";
    const std::string destText = PathToUtf8(sGameDir);
    bool haveData = CheckGameData(sGameDir).mMissing.empty();
    bool musicStep = false;
    for (;;)
    {
        std::string path;
        const bool fromEnv = !envPath.empty();
        if (fromEnv)
        {
            path = envPath;
            envPath.clear();
        }
        else if (musicStep)
        {
            const int choice = Ask(SDL_MESSAGEBOX_WARNING, MusicMessage(CheckGameData(sGameDir)),
                                   {"Folder...", "Disc image or archive...", "Play without music"}, 2, 2);
            if (choice == 2)
            {
                return true;
            }
            path = PickPath(choice == 0);
        }
        else
        {
            const std::string message =
                haveData ? "GTA2's game files are installed in:\n" + destText +
                               "\n\nChoose a source to replace them, or to add the music."
                         : "GTA2's game files were not found.\n\nThis port does not include them: they come from "
                           "your own copy of GTA2, either an installed game folder, or an installer, archive (.zip, "
                           ".7z) or disc image (.iso) of the game. They will be copied to:\n" +
                               destText;
            const int choice = Ask(SDL_MESSAGEBOX_INFORMATION, message,
                                   {"Use an existing install...", "Install from a file...", "I don't have the game"},
                                   0, -1);
            if (choice < 0)
            {
                return haveData && force;
            }
            if (choice == 2)
            {
                if (Ask(SDL_MESSAGEBOX_INFORMATION, kNoGameText, {"Open the archived page", "Back"}, 1, 1) == 0)
                {
                    SDL_OpenURL(kNoGameUrl);
                }
                continue;
            }
            path = PickPath(choice == 0);
        }
        if (path.empty())
        {
            continue;
        }

        const InstallResult result = RunInstall(PathFromUtf8(path), sGameDir);
        if (!result.mOk)
        {
            if (!result.mCancelled)
            {
                Tell(SDL_MESSAGEBOX_ERROR, result.mError);
            }
            if (fromEnv)
            {
                return false;
            }
            continue;
        }
        haveData = true;
        if (result.mCheck.mMusicFound >= result.mCheck.mMusicTotal)
        {
            return true;
        }
        musicStep = true;
    }
}

bool HasGameData(const fs::path& dir)
{
    fs::path found;
    return FindNoCase(dir, "data/fstyle.sty", &found);
}
} // namespace

namespace
{
// Folders the game writes to and does not create.
void MakeGameFolders()
{
    std::error_code ec;
    fs::path found;
    for (const char* dir : {"player", "test"})
    {
        if (!FindNoCase(fs::current_path(ec), dir, &found))
        {
            fs::create_directories(dir, ec);
        }
    }
}

void EnterFolder()
{
    if (const char* dir = SDL_getenv("GTA2_DIR"))
    {
        if (!SetWorkingDir(PathFromUtf8(dir)))
        {
            fprintf(stderr, "Cannot enter GTA2_DIR %s\n", dir);
            exit(1);
        }
        return;
    }
    std::error_code ec;
    if (HasGameData(fs::current_path(ec)))
    {
        return;
    }
    if (const char* base = SDL_GetBasePath())
    {
        const fs::path exeDir = PathFromUtf8(base);
        if (HasGameData(exeDir))
        {
            SetWorkingDir(exeDir);
            return;
        }
    }
    char* pref = SDL_GetPrefPath(kPrefOrg, kPrefApp);
    if (!pref)
    {
        fprintf(stderr, "No per-user folder: %s\n", SDL_GetError());
        return;
    }
    sGameDir = PathFromUtf8(pref);
    SDL_free(pref);
    sPerUser = true;
    SetWorkingDir(sGameDir);
}
} // namespace

void port_EnterGameFolder()
{
    EnterFolder();
    MakeGameFolders();
}

bool port_RunSetup(bool force)
{
    if (!sPerUser)
    {
        if (force)
        {
            SDL_Log("--setup: the game's data is already in the game folder, not the per-user one");
        }
        return true;
    }
    if (!force && CheckGameData(sGameDir).mMissing.empty())
    {
        return true;
    }
    SDL_InitSubSystem(SDL_INIT_EVENTS);
    const bool ok = SetupLoop(force);
    SDL_QuitSubSystem(SDL_INIT_EVENTS);
    return ok;
}
