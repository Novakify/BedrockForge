// LyricsForge - [BF] small shell helpers (kept out of Runtime_win.cpp so <windows.h> stays out of it).
// Original code for Bedrock Forge.
#include "lyrics/win/Win.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#pragma comment(lib, "shell32.lib")

namespace bf::lyrics::win {

void openInNotepad(std::filesystem::path const& p) {
    std::wstring const arg = L"\"" + p.wstring() + L"\"";
    ShellExecuteW(nullptr, L"open", L"notepad.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
}

void pressHotkey(int const* vks, int count) {
    if (count <= 0 || count > 4) return;
    std::array<WORD, 4> k{};
    for (int i = 0; i < count; ++i) k[size_t(i)] = WORD(vks[i]);
    std::thread([k, count] { // held ~60 ms like a real press: the NVIDIA app's hotkey hook ignores 0 ms taps
        auto send = [](WORD vk, bool up) {
            INPUT in{};
            in.type       = INPUT_KEYBOARD;
            in.ki.wVk     = vk;
            in.ki.wScan   = WORD(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
            in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
            SendInput(1, &in, sizeof in);
        };
        for (int i = 0; i < count; ++i) { send(k[size_t(i)], false); Sleep(20); }
        Sleep(60);
        for (int i = count - 1; i >= 0; --i) { send(k[size_t(i)], true); Sleep(10); }
    }).detach();
}

namespace {
void clipLog(std::filesystem::path const& file, std::string const& line) {
    SYSTEMTIME t; GetLocalTime(&t);
    char b[32];
    std::snprintf(b, sizeof b, "%02d:%02d:%02d ", t.wHour, t.wMinute, t.wSecond);
    std::ofstream out(file, std::ios::app | std::ios::binary);
    out << b << line << "\n";
}
std::filesystem::path findFfmpeg(std::filesystem::path const& hint, std::filesystem::path const& local) {
    std::error_code ec;
    if (!hint.empty() && std::filesystem::exists(hint, ec)) return hint;
    if (std::filesystem::exists(local, ec)) return local;
    wchar_t buf[MAX_PATH];
    if (SearchPathW(nullptr, L"ffmpeg.exe", nullptr, MAX_PATH, buf, nullptr)) return buf;
    wchar_t const* const guesses[] = {L"C:\\ffmpeg\\bin\\ffmpeg.exe", L"C:\\Program Files\\ffmpeg\\bin\\ffmpeg.exe"};
    for (auto g : guesses) if (std::filesystem::exists(g, ec)) return g;
    if (wchar_t const* la = _wgetenv(L"LOCALAPPDATA")) { // winget's links folder
        std::filesystem::path p = std::filesystem::path(la) / L"Microsoft" / L"WinGet" / L"Links" / L"ffmpeg.exe";
        if (std::filesystem::exists(p, ec)) return p;
    }
    return {};
}
} // namespace

void verticalClip(std::filesystem::path folder, std::filesystem::path ffmpegHint, std::filesystem::path localFfmpeg, std::filesystem::path logFile) {
    std::thread([=] {
        using namespace std::chrono_literals;
        std::error_code ec;
        auto const since = std::filesystem::file_time_type::clock::now() - 10s;
        std::filesystem::path clip;
        uintmax_t lastSize = 0;
        int stable = 0;
        for (int i = 0; i < 90 && stable < 3; ++i) { // the recorder needs a moment to write the file
            std::this_thread::sleep_for(1s);
            std::filesystem::path best;
            auto bestT = since;
            for (auto it = std::filesystem::recursive_directory_iterator(folder, std::filesystem::directory_options::skip_permission_denied, ec);
                 !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
                if (it.depth() > 2) { it.disable_recursion_pending(); continue; }
                if (!it->is_regular_file(ec)) continue;
                auto const ext = it->path().extension().wstring();
                if (ext != L".mp4" && ext != L".mkv" && ext != L".mov") continue;
                if (it->path().stem().wstring().ends_with(L"_tiktok")) continue;
                auto const t = it->last_write_time(ec);
                if (!ec && t > bestT) { bestT = t; best = it->path(); }
            }
            ec.clear();
            if (best.empty()) continue;
            uintmax_t const sz = std::filesystem::file_size(best, ec);
            if (best == clip && sz == lastSize && sz > 0) ++stable; else stable = 0;
            clip = best; lastSize = sz;
        }
        if (clip.empty() || stable < 3) { clipLog(logFile, "no new clip appeared in " + folder.string() + " (is the replay buffer running?)"); return; }
        auto const ff = findFfmpeg(ffmpegHint, localFfmpeg);
        if (ff.empty()) { clipLog(logFile, "clip saved: " + clip.string() + " (vertical copy needs ffmpeg: winget install ffmpeg)"); return; }
        auto out = clip; out.replace_filename(clip.stem().wstring() + L"_tiktok.mp4");
        // TikTok / Shorts: 1080x1920, the whole picture centred on a blurred, zoomed copy of itself
        std::wstring cmd = L"\"" + ff.wstring() + L"\" -y -hide_banner -loglevel error -i \"" + clip.wstring() +
                           L"\" -filter_complex \"[0:v]scale=1080:1920:force_original_aspect_ratio=increase,crop=1080:1920,boxblur=24:2[bg];"
                           L"[0:v]scale=1080:-2[fg];[bg][fg]overlay=(W-w)/2:(H-h)/2,format=yuv420p\" -c:v libx264 -preset veryfast -crf 20 "
                           L"-c:a aac -b:a 192k -movflags +faststart \"" + out.wstring() + L"\"";
        STARTUPINFOW si{}; si.cb = sizeof si;
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr, nullptr, &si, &pi)) {
            clipLog(logFile, "clip saved: " + clip.string() + " (ffmpeg would not start)");
            return;
        }
        WaitForSingleObject(pi.hProcess, 10 * 60 * 1000);
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        clipLog(logFile, code == 0 ? "clip " + clip.string() + " -> vertical " + out.string() : "clip saved: " + clip.string() + " (ffmpeg failed, code " + std::to_string(code) + ")");
    }).detach();
}

} // namespace bf::lyrics::win
#endif
