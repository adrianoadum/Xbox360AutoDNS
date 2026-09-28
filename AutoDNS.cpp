// AutoDNS.xex 2.0.0, a DashLaunch plugin.
//
// The console's stored network settings point at a DNS server that doesn't
// answer (192.0.2.1, from the RFC 5737 documentation range). The dashboard
// that runs before the exploit therefore can't resolve a single Xbox Live
// hostname.
// Once the exploit chain has loaded this plugin, it decrypts those settings
// with XnpLoadConfigParams, swaps in working DNS servers, and applies them
// with XnpConfig. XnpConfig only changes the running stack. Storage still
// holds the dead server, so the next boot starts offline again on its own.
// The working servers come from AutoDNS.ini next to the plugin. Without a
// usable AutoDNS.ini the plugin changes nothing. A notification on the console
// says what happened.
//
// Set this up once in the dashboard. Network Settings, DNS Manual, 192.0.2.1
// for both servers.

#include <xtl.h>
#include <stddef.h>
#include <string.h>

#define BOOT_WAIT  90000         // ms to wait for Wi-Fi association and DHCP at boot
#define SWAP_WAIT  30000         // ms to wait for DHCP to finish after XnpConfig

typedef LONG NTSTATUS;

// Kernel types the XDK headers leave out. Layouts from xkelib. This kernel
// names objects with ANSI strings.
typedef struct { USHORT Length, MaximumLength; PCHAR Buffer; } STRING;
typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } UNICODE_STRING;
typedef struct { HANDLE RootDirectory; STRING *ObjectName; ULONG Attributes; } OBJECT_ATTRIBUTES;
typedef struct { NTSTATUS Status; ULONG_PTR Information; } IO_STATUS_BLOCK;

// The head of a module's loader entry. DllMain's module handle points at one.
typedef struct {
    LIST_ENTRY InLoadOrderLinks, InClosureOrderLinks, InInitializationOrderLinks;
    PVOID NtHeadersBase, ImageBase;
    DWORD SizeOfNtImage;
    UNICODE_STRING FullDllName;
} LDR_DATA_TABLE_ENTRY;
C_ASSERT(offsetof(LDR_DATA_TABLE_ENTRY, FullDllName) == 0x24);

#define OBJ_CASE_INSENSITIVE          0x40
#define FILE_SYNCHRONOUS_IO_NONALERT  0x20
#define FILE_NON_DIRECTORY_FILE       0x40

extern "C" {
    NTSTATUS ExCreateThread(PHANDLE, DWORD, LPDWORD, PVOID, LPTHREAD_START_ROUTINE, LPVOID, DWORD);
    NTSTATUS XexGetModuleHandle(PCHAR, PHANDLE);
    NTSTATUS XexGetProcedureAddress(HANDLE, DWORD, PVOID *);
    void     RtlInitAnsiString(STRING *, const char *);
    NTSTATUS NtOpenFile(PHANDLE, ACCESS_MASK, OBJECT_ATTRIBUTES *, IO_STATUS_BLOCK *, DWORD, DWORD);
    NTSTATUS NtReadFile(HANDLE, HANDLE, PVOID, PVOID, IO_STATUS_BLOCK *, PVOID, DWORD, LARGE_INTEGER *);
    NTSTATUS NtClose(HANDLE);
}

#define SYSAPP 2   // XNCALLER_SYSAPP. Plugins run in the system context.

#pragma pack(push, 1)
typedef struct {
    DWORD ina, inaOnline;
    WORD  port;
    BYTE  enet[6], online[20];
} XNADDR_;

// XNetConfigParams. 492 bytes, layout from xkelib.
typedef struct {
    BYTE  hash[0x14], confounder[8];
    WORD  name[0x18], flags;
    BYTE  enet[6];
    DWORD ina, mask, gw, dns[2];
    char  host[0x28], pppoe[0x40 + 0x40 + 0x28 + 0x28];
    LARGE_INTEGER leaseTime;
    DWORD leaseSecs, rest[3 + 4 + 4];
    BYTE  tail[0x44 + 16];
} CFG;
#pragma pack(pop)
C_ASSERT(sizeof(CFG) == 492);
C_ASSERT(offsetof(CFG, flags) == 0x4C);      // fields Load() and Run() depend on,
C_ASSERT(offsetof(CFG, dns) == 0x60);        // pinned so a layout slip fails the
C_ASSERT(offsetof(CFG, leaseSecs) == 0x168); // build instead of writing 1.1.1.1 into the hostname

// xam.xex exports by ordinal (Xenia xam_table.inc and xkelib xamext.def agree):
//   51 NetDll_XNetStartup   73 NetDll_XNetGetTitleXnAddr
//  101 NetDll_XnpLoadConfigParams   104 NetDll_XnpConfig
static int   (*pXNetStartup)(int, BYTE *);
static DWORD (*pXNetGetTitleXnAddr)(int, XNADDR_ *);
static int   (*pXnpConfig)(int, CFG *, DWORD);
static int   (*pXnpLoadConfigParams)(int, CFG *, DWORD, DWORD);

static BOOL Resolve()
{
    HANDLE xam;
    if (XexGetModuleHandle("xam.xex", &xam) < 0)
        return FALSE;

    struct { DWORD ord; PVOID *fn; } table[] = {
        {  51, (PVOID *)&pXNetStartup         },
        {  73, (PVOID *)&pXNetGetTitleXnAddr  },
        { 101, (PVOID *)&pXnpLoadConfigParams },
        { 104, (PVOID *)&pXnpConfig           },
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (XexGetProcedureAddress(xam, table[i].ord, table[i].fn) < 0 || *table[i].fn == NULL)
            return FALSE;
    }
    return TRUE;
}

// Polls XNetGetTitleXnAddr until the console has an address.
static BOOL WaitForAddress(DWORD ms)
{
    DWORD t0 = GetTickCount();

    for (;;) {
        XNADDR_ a;
        memset(&a, 0, sizeof(a));
        DWORD flags = pXNetGetTitleXnAddr(SYSAPP, &a);

        BOOL configured = (flags & 0xC) != 0;   // STATIC or DHCP
        BOOL none       = (flags & 0x1) != 0;   // NONE
        if (configured && !none && a.ina != 0)
            return TRUE;

        if (GetTickCount() - t0 > ms)
            return FALSE;
        Sleep(500);
    }
}

// Decrypts the stored network settings into c. If decryption didn't happen,
// the lease reads as 70 years and the flags are noise, so that's the check.
static BOOL Load(CFG *c)
{
    memset(c, 0, sizeof(*c));
    pXnpLoadConfigParams(SYSAPP, c, 0, 0);   // returns 1 on hardware; meaning undocumented
    return c->leaseSecs <= 30u * 24 * 3600 && c->flags < 0x1000;
}

// AutoDNS.ini, in the plugin's folder, holds the servers to switch to:
//
//   dns1 = 1.1.1.1
//   dns2 = 1.0.0.1   ; optional. Missing or empty means no second server.
//   notify = 1       ; optional. 0/false/no/off turns the notifications off.
//
// Other keys, sections and comments are ignored. The file is all or nothing:
// a bad value, or no dns1, and the plugin leaves the console's DNS alone.

// Dotted quad in [p, end) -> 0xAABBCCDD, network order like XNetConfigParams.
static BOOL ParseIp(const char *p, const char *end, DWORD *ip)
{
    DWORD value = 0;
    for (int part = 0; part < 4; part++) {
        if (part > 0 && (p == end || *p++ != '.'))
            return FALSE;
        DWORD octet = 0;
        int digits = 0;
        for (; p < end && *p >= '0' && *p <= '9' && digits < 3; p++, digits++)
            octet = octet * 10 + (DWORD)(*p - '0');
        if (digits == 0 || octet > 255)
            return FALSE;
        value = value << 8 | octet;
    }
    if (p != end)
        return FALSE;
    *ip = value;
    return TRUE;
}

static void Trim(const char **p, const char **end)
{
    while (*p < *end && (**p == ' ' || **p == '\t'))
        (*p)++;
    while (*end > *p && ((*end)[-1] == ' ' || (*end)[-1] == '\t' || (*end)[-1] == '\r'))
        (*end)--;
}

// Case-insensitive match of [p, end) against a lowercase word.
static BOOL Matches(const char *p, const char *end, const char *word)
{
    for (; p < end; p++, word++) {
        char c = *p;
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        if (c != *word)
            return FALSE;
    }
    return *word == 0;
}

static BOOL ParseBool(const char *p, const char *end, BOOL *b)
{
    static const char *const words[] = { "0", "false", "no", "off", "1", "true", "yes", "on" };
    for (int i = 0; i < 8; i++) {
        if (Matches(p, end, words[i])) {
            *b = i >= 4;
            return TRUE;
        }
    }
    return FALSE;
}

typedef struct { DWORD dns[2]; BOOL notify; } SETTINGS;

// Fills s from the text of AutoDNS.ini. Leaves s alone and returns FALSE
// when the text doesn't give a usable dns1 or holds a bad value.
static BOOL ParseIni(const char *text, SETTINGS *s)
{
    if (strncmp(text, "\xEF\xBB\xBF", 3) == 0)   // UTF-8 byte order mark, which Notepad can add
        text += 3;

    static const char *const keys[] = { "dns1", "dns2", "notify" };
    const char *val[3] = { NULL, NULL, NULL }, *valEnd[3] = { NULL, NULL, NULL };
    for (const char *line = text; *line; ) {
        const char *eol = strchr(line, '\n');
        if (eol == NULL)
            eol = line + strlen(line);

        // A comment or section line has no key of its own, so it never matches.
        const char *eq = (const char *)memchr(line, '=', (size_t)(eol - line));
        if (eq != NULL) {
            const char *k = line, *kEnd = eq;
            Trim(&k, &kEnd);
            const char *v = eq + 1, *vEnd = v;
            while (vEnd < eol && *vEnd != ';' && *vEnd != '#')
                vEnd++;
            Trim(&v, &vEnd);

            for (int i = 0; i < 3; i++) {
                if (Matches(k, kEnd, keys[i])) {
                    val[i] = v;
                    valEnd[i] = vEnd;
                }
            }
        }
        line = *eol ? eol + 1 : eol;
    }

    // Missing and empty are the same for the optional keys.
    DWORD ip[2] = { 0, 0 };
    BOOL notify = TRUE;
    if (val[0] == NULL || !ParseIp(val[0], valEnd[0], &ip[0]) || ip[0] == 0)
        return FALSE;
    if (val[1] != NULL && val[1] != valEnd[1] && !ParseIp(val[1], valEnd[1], &ip[1]))
        return FALSE;
    if (val[2] != NULL && val[2] != valEnd[2] && !ParseBool(val[2], valEnd[2], &notify))
        return FALSE;
    s->dns[0] = ip[0];
    s->dns[1] = ip[1];
    s->notify = notify;
    return TRUE;
}

// The plugin's own path with its file name swapped for AutoDNS.ini, for
// example \Device\Mass0\AutoDNS.xex -> \Device\Mass0\AutoDNS.ini. Object names
// are ANSI on this kernel, so a folder outside ASCII is refused.
static BOOL IniPath(const UNICODE_STRING *dll, char *out, size_t cap)
{
    static const char name[] = "AutoDNS.ini";

    size_t n = dll->Buffer != NULL ? dll->Length / sizeof(WCHAR) : 0;
    while (n > 0 && dll->Buffer[n - 1] != L'\\')
        n--;
    if (n == 0 || n + sizeof(name) > cap)
        return FALSE;

    for (size_t i = 0; i < n; i++) {
        if (dll->Buffer[i] > 0x7F)
            return FALSE;
        out[i] = (char)dll->Buffer[i];
    }
    memcpy(out + n, name, sizeof(name));
    return TRUE;
}

// Writes ip as a dotted quad at p and returns the end of the text.
static char *PutIp(char *p, DWORD ip)
{
    for (int shift = 24; shift >= 0; shift -= 8) {
        DWORD octet = ip >> shift & 0xFF;
        if (octet >= 100)
            *p++ = (char)('0' + octet / 100);
        if (octet >= 10)
            *p++ = (char)('0' + octet / 10 % 10);
        *p++ = (char)('0' + octet % 10);
        if (shift > 0)
            *p++ = '.';
    }
    *p = 0;
    return p;
}

// "AutoDNS: DNS set to 1.1.1.1, 1.0.0.1", or only dns1 when there's no dns2.
// At most 53 characters plus the NUL. Returns the end of the text.
static char *SetText(char *out, const DWORD dns[2])
{
    static const char head[] = "AutoDNS: DNS set to ";
    memcpy(out, head, sizeof(head));
    char *p = PutIp(out + sizeof(head) - 1, dns[0]);
    if (dns[1] != 0) {
        *p++ = ',';
        *p++ = ' ';
        p = PutIp(p, dns[1]);
    }
    return p;
}

// xam.xex ordinal 656, XNotifyQueueUI. Declared in xkelib; EatonZ's BadStorage
// calls it the same way from a plugin started by the exploit.
#define NOTIFY_OK    14   // XNOTIFYUI_TYPE_PREFERRED_REVIEW, a happy face
#define NOTIFY_FAIL  15   // XNOTIFYUI_TYPE_AVOID_REVIEW, a sad face
static void (*pXNotifyQueueUI)(DWORD type, DWORD user, ULONGLONG priority, WCHAR *text, PVOID context);
static BOOL g_notify = TRUE;   // AutoDNS.ini's notify, once the file has been read

// Without the export the plugin still does its job, just silently.
static void ResolveNotify()
{
    HANDLE xam;
    PVOID fn = NULL;
    if (XexGetModuleHandle("xam.xex", &xam) >= 0 && XexGetProcedureAddress(xam, 656, &fn) >= 0)
        pXNotifyQueueUI = (void (*)(DWORD, DWORD, ULONGLONG, WCHAR *, PVOID))fn;
}

// Shows text in a notification on the console. The text is ASCII.
static void Notify(DWORD type, const char *text)
{
    static WCHAR wide[128];   // static in case xam reads it after the call returns
    if (!g_notify || pXNotifyQueueUI == NULL)
        return;

    size_t i = 0;
    for (; text[i] != 0 && i < 127; i++)
        wide[i] = (WCHAR)text[i];
    wide[i] = 0;
    pXNotifyQueueUI(type, XUSER_INDEX_ANY, 2, wide, NULL);   // 2: XNOTIFYUI_PRIORITY_HIGH
}

// XnpConfig returns at once and reconfigures in the background (DHCP re-ran
// for about two seconds in every console test). The params live in a static
// so they outlive this call, and the caller waits for the address to come
// back before tearing the network context down.
static CFG g_cfg;

static void Run(const DWORD dns[2])
{
    if (!WaitForAddress(BOOT_WAIT)) {
        Notify(NOTIFY_FAIL, "AutoDNS: no network address after 90 s. DNS not changed.");
        return;
    }
    if (!Load(&g_cfg)) {
        Notify(NOTIFY_FAIL, "AutoDNS: can't read network settings. DNS not changed.");
        return;
    }

    g_cfg.dns[0] = dns[0];
    g_cfg.dns[1] = dns[1];
    pXnpConfig(SYSAPP, &g_cfg, 0);

    static const char late[] = ". No network address after 30 s.";
    char text[128];
    char *end = SetText(text, dns);
    if (WaitForAddress(SWAP_WAIT)) {
        Notify(NOTIFY_OK, text);
    } else {
        memcpy(end, late, sizeof(late));
        Notify(NOTIFY_FAIL, text);
    }
}

static HANDLE g_module;   // this plugin's loader entry, from DllMain

enum { INI_OK, INI_MISSING, INI_INVALID };

// Reads AutoDNS.ini from the plugin's folder into s. The kernel path from the
// loader entry works without the Usb: or Hdd: links. s is left alone unless
// the result is INI_OK.
static int ReadIni(SETTINGS *s)
{
    char path[MAX_PATH];
    if (!IniPath(&((LDR_DATA_TABLE_ENTRY *)g_module)->FullDllName, path, sizeof(path)))
        return INI_MISSING;

    STRING name;
    RtlInitAnsiString(&name, path);
    OBJECT_ATTRIBUTES attr = { NULL, &name, OBJ_CASE_INSENSITIVE };
    IO_STATUS_BLOCK io;
    HANDLE file;
    if (NtOpenFile(&file, GENERIC_READ | SYNCHRONIZE, &attr, &io, FILE_SHARE_READ,
                   FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE) < 0)
        return INI_MISSING;

    char text[512];
    NTSTATUS status = NtReadFile(file, NULL, NULL, NULL, &io, text, sizeof(text), NULL);
    NtClose(file);
    if (status < 0 || io.Information >= sizeof(text))   // a longer file would be cut mid-line
        return INI_INVALID;
    text[io.Information] = 0;
    return ParseIni(text, s) ? INI_OK : INI_INVALID;
}

// No XNetCleanup: the plugin stays resident for the console's uptime, and
// tearing down a context under xam's own caller id is the one call here whose
// refcount semantics aren't documented. A held reference costs nothing.
static DWORD WINAPI Worker(LPVOID)
{
    ResolveNotify();

    SETTINGS s;
    int ini = ReadIni(&s);
    if (ini != INI_OK) {
        Notify(NOTIFY_FAIL, ini == INI_MISSING ? "AutoDNS: AutoDNS.ini not found. DNS not changed."
                                               : "AutoDNS: AutoDNS.ini is invalid. DNS not changed.");
        return 0;
    }
    g_notify = s.notify;

    BYTE startup[13] = { 13 };   // XNetStartupParams. First byte is the size, zeros mean defaults.
    if (!Resolve() || pXNetStartup(SYSAPP, startup) != 0) {
        Notify(NOTIFY_FAIL, "AutoDNS: network startup failed. DNS not changed.");
        return 0;
    }

    Run(s.dns);
    return 0;
}

extern "C" BOOL WINAPI DllMain(HANDLE module, DWORD reason, LPVOID)
{
    if (reason != DLL_PROCESS_ATTACH)
        return TRUE;

    g_module = module;
    HANDLE h = NULL;
    if (ExCreateThread(&h, 0, NULL, NULL, Worker, NULL, 2) >= 0 && h != NULL)
        CloseHandle(h);
    return TRUE;
}
