// LyricsFlow port - WinHTTP transport for the lyric search (lyrics/core/Fetcher.h HttpGet). Original code for
// Bedrock Forge. Synchronous GET with per-request timeouts derived from the search deadline.
#ifdef _WIN32
#include "lyrics/win/Win.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <string>

#pragma comment(lib, "winhttp.lib")

namespace bf::lyrics::win {

namespace {
std::wstring widen(std::string const& s) {
    if (s.empty()) return {};
    int          n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}
struct Handle {
    HINTERNET h = nullptr;
    ~Handle() { if (h) WinHttpCloseHandle(h); }
};
} // namespace

fetch::HttpResponse httpGet(std::string const& url, fetch::Headers const& headers, int64_t deadlineMs) {
    fetch::HttpResponse out;
    int64_t             left = deadlineMs - fetch::nowMs();
    if (left <= 0) return out;
    int timeout = int(std::min<int64_t>(left, 8000));

    std::wstring    wurl = widen(url);
    URL_COMPONENTS  uc{};
    wchar_t         host[256]{}, path[4096]{};
    uc.dwStructSize     = sizeof uc;
    uc.lpszHostName     = host;
    uc.dwHostNameLength = 255;
    uc.lpszUrlPath      = path;
    uc.dwUrlPathLength  = 4095;
    wchar_t extra[4096]{};
    uc.lpszExtraInfo     = extra;
    uc.dwExtraInfoLength = 4095;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) return out;
    std::wstring object = std::wstring(path, uc.dwUrlPathLength) + std::wstring(extra, uc.dwExtraInfoLength);

    Handle session{WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) BedrockForge-LyricsFlow/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.h) return out;
    WinHttpSetTimeouts(session.h, timeout, timeout, timeout, timeout);
    Handle conn{WinHttpConnect(session.h, std::wstring(host, uc.dwHostNameLength).c_str(), uc.nPort, 0)};
    if (!conn.h) return out;
    DWORD  flags = uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    Handle req{WinHttpOpenRequest(conn.h, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags)};
    if (!req.h) return out;
    DWORD decomp = WINHTTP_DECOMPRESSION_FLAG_ALL;
    WinHttpSetOption(req.h, WINHTTP_OPTION_DECOMPRESSION, &decomp, sizeof decomp);
    std::wstring hdrs;
    for (auto const& [k, v] : headers) hdrs += widen(k) + L": " + widen(v) + L"\r\n"; // a User-Agent header overrides the session's
    if (!WinHttpSendRequest(req.h, hdrs.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : hdrs.c_str(), hdrs.empty() ? 0 : DWORD(-1L),
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        return out;
    if (!WinHttpReceiveResponse(req.h, nullptr)) return out;
    DWORD status = 0, sz = sizeof status;
    WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz,
                        WINHTTP_NO_HEADER_INDEX);
    std::string body;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req.h, &avail) || avail == 0) break;
        size_t at = body.size();
        body.resize(at + avail);
        DWORD got = 0;
        if (!WinHttpReadData(req.h, body.data() + at, avail, &got)) { body.resize(at); break; }
        body.resize(at + got);
        if (body.size() > (8u << 20)) break; // 8 MiB cap
        if (fetch::nowMs() > deadlineMs) return out;
    }
    out.code = int(status);
    out.body = std::move(body);
    return out;
}

} // namespace bf::lyrics::win
#endif
