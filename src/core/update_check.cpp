#include "core/common.h"
#include "core/version.h"

#include <shellapi.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")

// update_check=1: once per start of the game the mod asks GitHub for the newest release of
// the project (one HTTPS request, a GET of the public address below, nothing about the
// player or the game in it) and, when that release is newer than this DLL, shows a message
// with the two version numbers. "Yes" opens the release page in the browser. Nothing is
// downloaded or installed. A version is announced once: the announced one is kept in
// bo2ss.ini (update_seen). Without a connection, or with any unexpected answer, nothing
// happens. The request runs on a thread of its own and touches nothing in the game.
namespace update {
constexpr wchar_t kHost[] = L"api.github.com";
constexpr wchar_t kPath[] = L"/repos/pndevo/bo2split/releases/latest";
constexpr wchar_t kPage[] = L"https://github.com/pndevo/bo2split/releases/latest";
constexpr int kTimeoutMs = 5000;
} // namespace update

// Opens an address in the default browser, through explorer.exe. A browser that the game
// starts itself is a child process of the game, and Steam counts the game as running for as
// long as that browser stays open: it refuses the next start with "game is already running".
// explorer.exe hands the address to the desktop shell and ends; the browser is the shell's.
void OpenInBrowser(const wchar_t* url) {
    ShellExecuteW(nullptr, L"open", L"explorer.exe", url, nullptr, SW_SHOWNORMAL);
}

// "v1.2.3" or "1.2.3" -> three numbers; false when the text is not a version.
static bool ParseVersion(const char* text, int out[3]) {
    if (*text == 'v' || *text == 'V') ++text;
    out[0] = out[1] = out[2] = 0;
    for (int i = 0; i < 3; ++i) {
        if (*text < '0' || *text > '9') return false;
        while (*text >= '0' && *text <= '9')
            out[i] = out[i] * 10 + (*text++ - '0');
        if (i < 2) {
            if (*text != '.') return i > 0 && *text == '\0'; // "1.2" counts as 1.2.0
            ++text;
        }
    }
    return true;
}

static bool Newer(const int a[3], const int b[3]) {
    for (int i = 0; i < 3; ++i)
        if (a[i] != b[i]) return a[i] > b[i];
    return false;
}

// The value of "tag_name" in the answer, e.g. v0.2.0.
static bool FetchLatestTag(char* tag, size_t tagSize) {
    bool found = false;
    HINTERNET session =
        WinHttpOpen(L"bo2split", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    WinHttpSetTimeouts(session, update::kTimeoutMs, update::kTimeoutMs, update::kTimeoutMs, update::kTimeoutMs);
    HINTERNET connection = WinHttpConnect(session, update::kHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", update::kPath, nullptr, WINHTTP_NO_REFERER,
                                                        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                                   : nullptr;
    static char body[16384];
    size_t used = 0;
    DWORD status = 0, statusSize = sizeof(status);
    if (request &&
        WinHttpSendRequest(request, L"Accept: application/vnd.github+json\r\n", (DWORD)-1, WINHTTP_NO_REQUEST_DATA, 0,
                           0, 0) &&
        WinHttpReceiveResponse(request, nullptr) &&
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX) &&
        status == 200) {
        DWORD got = 0;
        while (used < sizeof(body) - 1 &&
               WinHttpReadData(request, body + used, (DWORD)(sizeof(body) - 1 - used), &got) && got)
            used += got;
        body[used] = '\0';
        const char* key = strstr(body, "\"tag_name\"");
        const char* open = key ? strchr(key + 10, '"') : nullptr;
        const char* close = open ? strchr(open + 1, '"') : nullptr;
        if (close && (size_t)(close - open - 1) < tagSize) {
            memcpy(tag, open + 1, (size_t)(close - open - 1));
            tag[close - open - 1] = '\0';
            found = true;
        }
    }
    if (!found) Log("[update] no answer about the newest release (HTTP status %lu, %u bytes)", status, (unsigned)used);
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return found;
}

static DWORD WINAPI UpdateCheckThread(LPVOID) {
    char tag[32];
    if (!FetchLatestTag(tag, sizeof(tag))) return 0;
    int latest[3], own[3];
    if (!ParseVersion(tag, latest) || !ParseVersion(BO2SS_VERSION, own)) {
        Log("[update] newest release '%s' is not a version number", tag);
        return 0;
    }
    if (!Newer(latest, own)) {
        Log("[update] this is v%s, the newest release is %s: up to date", BO2SS_VERSION, tag);
        return 0;
    }
    wchar_t ini[MAX_PATH], wtag[32], seen[32];
    lstrcpynW(ini, g_gameDir, MAX_PATH);
    lstrcatW(ini, L"bo2ss.ini");
    MultiByteToWideChar(CP_ACP, 0, tag, -1, wtag, 32);
    GetPrivateProfileStringW(L"bo2ss", L"update_seen", L"", seen, 32, ini);
    if (lstrcmpiW(seen, wtag) == 0) {
        Log("[update] this is v%s, release %s is out (announced before)", BO2SS_VERSION, tag);
        return 0;
    }
    Log("[update] this is v%s, release %s is out", BO2SS_VERSION, tag);
    WritePrivateProfileStringW(L"bo2ss", L"update_seen", wtag, ini);
    wchar_t text[512];
    _snwprintf_s(text, _countof(text), _TRUNCATE,
                 L"A new version of bo2split is out: %s (installed: v%S).\n\n"
                 L"Nothing is downloaded or installed by the mod. To update, get the new release and replace "
                 L"XINPUT1_3.dll in the game folder.\n\n"
                 L"Open the release page in the browser now?\n\n"
                 L"This message comes once per version. update_check=0 in bo2ss.ini switches the check off.",
                 wtag, BO2SS_VERSION);
    if (MessageBoxW(nullptr, text, L"bo2split", MB_TOPMOST | MB_SETFOREGROUND | MB_YESNO | MB_ICONINFORMATION) == IDYES)
        OpenInBrowser(update::kPage);
    return 0;
}

void UpdateCheckStart() {
    if (!ReadIniInt(L"update_check", 1)) {
        Log("[update] check switched off (update_check=0)");
        return;
    }
    HANDLE thread = CreateThread(nullptr, 0, UpdateCheckThread, nullptr, 0, nullptr);
    if (thread) CloseHandle(thread);
}
