#include "core/common.h"

// ---------------------------------------------------------------------------
// Logging

CRITICAL_SECTION g_logLock;
FILE* g_log;

void Log(const char* fmt, ...) {
    if (!g_log) return;
    EnterCriticalSection(&g_logLock);
    fprintf(g_log, "[%10lu] ", GetTickCount());
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
    LeaveCriticalSection(&g_logLock);
}

// ---------------------------------------------------------------------------
// Memory helpers

bool IsReadable(uintptr_t addr, size_t len) {
    uintptr_t end = addr + len;
    while (addr < end) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery((const void*)addr, &mbi, sizeof(mbi))) return false;
        if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
        addr = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    }
    return true;
}

bool ReadSafe(uintptr_t addr, void* out, size_t len) {
    if (!IsReadable(addr, len)) return false;
    __try {
        memcpy(out, (const void*)addr, len);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

uint32_t Read32(uintptr_t addr) {
    uint32_t v = 0xDEADBEEF;
    ReadSafe(addr, &v, 4);
    return v;
}

bool BytesMatch(uintptr_t addr, const uint8_t* expect, size_t len) {
    uint8_t buf[64];
    if (len > sizeof(buf) || !ReadSafe(addr, buf, len)) return false;
    return memcmp(buf, expect, len) == 0;
}

bool WriteCode(uintptr_t addr, const uint8_t* bytes, size_t len) {
    DWORD old;
    if (!VirtualProtect((void*)addr, len, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy((void*)addr, bytes, len);
    VirtualProtect((void*)addr, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)addr, len);
    return true;
}

uint32_t ImageTimestamp(uintptr_t base) {
    if ((Read32(base) & 0xffff) != 0x5a4d) return 0;
    uint32_t peOff = Read32(base + 0x3c);
    if (Read32(base + peOff) != 0x00004550) return 0;
    return Read32(base + peOff + 8);
}

// ---------------------------------------------------------------------------
// Settings and patching

wchar_t g_gameDir[MAX_PATH];

// ReadIniInt is called from about 200 places, many of them every frame and some several times
// per view pass, from the main thread, the render thread and the workers. GetPrivateProfileIntW
// opens and parses the file on every call. ini_cache=1 (default): the values are kept in a
// table (key -> present, value; the default stays the caller's, call sites differ) and the
// table is dropped when the file's write time or size changes, checked every 200 ms. A file
// that is missing for a moment (a replace by move) keeps the table as it is.
// ini_cache=0 reads the file on every call (useful for comparing frame rates).
volatile LONG g_iniCalls, g_iniFileReads;
namespace {
struct IniEntry {
    wchar_t key[48];
    int value;
    bool present, used;
};
constexpr unsigned kIniSlots = 1024;
IniEntry g_iniTable[kIniSlots];
SRWLOCK g_iniLock = SRWLOCK_INIT;
volatile LONG g_iniNextCheck, g_iniChecking;
volatile LONG g_iniCacheOn = 1;
FILETIME g_iniWriteTime;
DWORD g_iniSize;

void IniPath(wchar_t* ini) {
    lstrcpynW(ini, g_gameDir, MAX_PATH);
    lstrcatW(ini, L"bo2ss.ini");
}
unsigned IniHash(const wchar_t* key, size_t* len) {
    unsigned h = 2166136261u;
    size_t n = 0;
    for (; key[n]; ++n)
        h = (h ^ (unsigned)key[n]) * 16777619u;
    *len = n;
    return h;
}
void IniCheckFile() {
    DWORD now = GetTickCount();
    if ((LONG)(now - (DWORD)g_iniNextCheck) < 0) return;
    if (InterlockedCompareExchange(&g_iniChecking, 1, 0) != 0) return;
    g_iniNextCheck = (LONG)(now + 200);
    wchar_t ini[MAX_PATH];
    IniPath(ini);
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (GetFileAttributesExW(ini, GetFileExInfoStandard, &fa)) {
        if (CompareFileTime(&fa.ftLastWriteTime, &g_iniWriteTime) != 0 || fa.nFileSizeLow != g_iniSize) {
            AcquireSRWLockExclusive(&g_iniLock);
            memset(g_iniTable, 0, sizeof(g_iniTable));
            g_iniWriteTime = fa.ftLastWriteTime;
            g_iniSize = fa.nFileSizeLow;
            ReleaseSRWLockExclusive(&g_iniLock);
        }
        InterlockedIncrement(&g_iniFileReads);
        g_iniCacheOn = (LONG)GetPrivateProfileIntW(L"bo2ss", L"ini_cache", 1, ini);
    }
    InterlockedExchange(&g_iniChecking, 0);
}
} // namespace

bool TraceOn() {
    return ReadIniInt(L"trace", 0) != 0;
}

int ReadIniInt(const wchar_t* key, int def) {
    InterlockedIncrement(&g_iniCalls);
    IniCheckFile();
    size_t len;
    unsigned h = IniHash(key, &len);
    wchar_t ini[MAX_PATH];
    if (!g_iniCacheOn || len >= 48) {
        IniPath(ini);
        InterlockedIncrement(&g_iniFileReads);
        return (int)GetPrivateProfileIntW(L"bo2ss", key, def, ini);
    }
    AcquireSRWLockShared(&g_iniLock);
    for (unsigned i = 0; i < kIniSlots; ++i) {
        const IniEntry& e = g_iniTable[(h + i) & (kIniSlots - 1)];
        if (!e.used) break;
        if (wcscmp(e.key, key) == 0) {
            int v = e.present ? e.value : def;
            ReleaseSRWLockShared(&g_iniLock);
            return v;
        }
    }
    ReleaseSRWLockShared(&g_iniLock);
    // Not in the table: read it twice with different defaults to tell a missing key from a value.
    IniPath(ini);
    InterlockedExchangeAdd(&g_iniFileReads, 2);
    int a = (int)GetPrivateProfileIntW(L"bo2ss", key, 0x5a5a5a5a, ini);
    int b = (int)GetPrivateProfileIntW(L"bo2ss", key, 0x3c3c3c3c, ini);
    bool present = a == b;
    AcquireSRWLockExclusive(&g_iniLock);
    for (unsigned i = 0; i < kIniSlots; ++i) {
        IniEntry& e = g_iniTable[(h + i) & (kIniSlots - 1)];
        if (e.used && wcscmp(e.key, key) != 0) continue;
        wcscpy_s(e.key, key);
        e.value = a;
        e.present = present;
        e.used = true;
        break;
    }
    ReleaseSRWLockExclusive(&g_iniLock);
    return present ? a : def;
}

void ApplyBytePatch(const BytePatch& p) {
    if (!ReadIniInt(p.iniKey, 1) || (p.unlessKey && ReadIniInt(p.unlessKey, 0))) {
        Log("patch '%s': disabled in bo2ss.ini", p.name);
        return;
    }
    if (!BytesMatch(p.addr, p.original, p.len)) {
        uint8_t seen[8] = {};
        ReadSafe(p.addr, seen, p.len);
        Log("patch '%s': unexpected bytes at %08x (%02x %02x %02x %02x %02x), skipped", p.name, (unsigned)p.addr,
            seen[0], seen[1], seen[2], seen[3], seen[4]);
        return;
    }
    Log("patch '%s' at %08x: %s", p.name, (unsigned)p.addr, WriteCode(p.addr, p.replacement, p.len) ? "ok" : "FAILED");
}

static uint8_t* g_trampolinePage;
static size_t g_trampolineUsed;

void* MakeTrampoline(uintptr_t target, size_t len) {
    if (!g_trampolinePage)
        g_trampolinePage = (uint8_t*)VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!g_trampolinePage || g_trampolineUsed + len + 5 > 4096) return nullptr;
    uint8_t* t = g_trampolinePage + g_trampolineUsed;
    memcpy(t, (const void*)target, len);
    t[len] = 0xe9;
    int32_t rel = (int32_t)((target + len) - ((uintptr_t)t + len + 5));
    memcpy(t + len + 1, &rel, 4);
    g_trampolineUsed += (len + 5 + 15) & ~15u;
    return t;
}

void InstallDetour(const Detour& d) {
    if (!BytesMatch(d.target, d.prologue, d.prologueLen)) {
        Log("hook '%s': unexpected bytes at %08x, skipped", d.name, (unsigned)d.target);
        return;
    }
    void* t = MakeTrampoline(d.target, d.prologueLen);
    if (!t) {
        Log("hook '%s': trampoline allocation failed", d.name);
        return;
    }
    *d.trampoline = t;
    uint8_t jmp[5] = {0xe9};
    int32_t rel = (int32_t)((uintptr_t)d.hook - (d.target + 5));
    memcpy(jmp + 1, &rel, 4);
    Log("hook '%s' at %08x: %s", d.name, (unsigned)d.target, WriteCode(d.target, jmp, 5) ? "ok" : "FAILED");
}
