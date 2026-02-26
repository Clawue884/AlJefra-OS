/* SPDX-License-Identifier: MIT */
/* AlJefra OS -- AI Chat Engine Implementation
 *
 * Conversational interface: translates natural language (English + Arabic)
 * into system actions, executes them via HAL / FS / driver APIs, and
 * formats human-readable responses.
 *
 * Design constraints:
 *   - Bare-metal kernel code: no malloc, no stdio, no stdlib.
 *   - All buffers are statically allocated.
 *   - Only depends on: stdint.h, HAL API, kernel/fs.h, kernel/driver_loader.h,
 *     and lib/string.h.
 *   - Must compile with -ffreestanding -fno-builtin.
 */

#include "ai_chat.h"
#include "fs.h"
#include "driver_loader.h"
#include "../hal/hal.h"
#include "../lib/string.h"

/* ========================================================================
 * Internal String Helpers
 *
 * We cannot use libc.  The lib/string.h provides memcpy, memset, memcmp,
 * str_eq, str_len, str_copy.  We add a few more here that are specific to
 * the chat engine (case-insensitive matching, substring search, number
 * formatting).
 * ======================================================================== */

/* --- Character classification (ASCII only) --- */

static int is_upper(char c) { return (c >= 'A' && c <= 'Z'); }
static int is_space(char c) { return (c == ' ' || c == '\t' || c == '\n' || c == '\r'); }

static char to_lower(char c) { return is_upper(c) ? (char)(c + 32) : c; }

/* Case-insensitive byte-by-byte string equality (ASCII). */
static int ci_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (to_lower(*a) != to_lower(*b))
            return 0;
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

/* Case-insensitive substring search.
 * Returns pointer to first occurrence of `needle` in `haystack`, or NULL. */
static const char *ci_strstr(const char *haystack, const char *needle)
{
    if (!needle[0])
        return haystack;

    uint32_t nlen = str_len(needle);
    uint32_t hlen = str_len(haystack);

    if (nlen > hlen)
        return (const char *)0;

    for (uint32_t i = 0; i <= hlen - nlen; i++) {
        uint32_t j;
        for (j = 0; j < nlen; j++) {
            if (to_lower(haystack[i + j]) != to_lower(needle[j]))
                break;
        }
        if (j == nlen)
            return &haystack[i];
    }
    return (const char *)0;
}

/* Exact (byte-level) substring search.  Used for Arabic UTF-8 patterns
 * where case-insensitive comparison is not meaningful. */
static const char *byte_strstr(const char *haystack, const char *needle)
{
    if (!needle[0])
        return haystack;

    uint32_t nlen = str_len(needle);
    uint32_t hlen = str_len(haystack);

    if (nlen > hlen)
        return (const char *)0;

    for (uint32_t i = 0; i <= hlen - nlen; i++) {
        if (memcmp(&haystack[i], needle, nlen) == 0)
            return &haystack[i];
    }
    return (const char *)0;
}

/* Bounded string copy that always NUL-terminates. */
static void safe_copy(char *dst, const char *src, uint32_t max)
{
    if (max == 0)
        return;
    uint32_t i;
    for (i = 0; i < max - 1 && src[i] != '\0'; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

/* Append src to dst (dst has total capacity `max`). */
static void safe_append(char *dst, const char *src, uint32_t max)
{
    uint32_t dlen = str_len(dst);
    if (dlen >= max - 1)
        return;
    safe_copy(dst + dlen, src, max - dlen);
}

/* Format an unsigned 64-bit integer into a decimal string.
 * Returns the number of characters written (not including NUL). */
static int fmt_u64(char *buf, uint32_t max, uint64_t val)
{
    if (max == 0)
        return 0;

    /* Build digits in reverse */
    char tmp[21]; /* max 20 digits for uint64_t */
    int pos = 0;

    if (val == 0) {
        tmp[pos++] = '0';
    } else {
        while (val > 0 && pos < 20) {
            tmp[pos++] = (char)('0' + (val % 10));
            val /= 10;
        }
    }

    /* Reverse into output */
    int written = 0;
    for (int i = pos - 1; i >= 0 && (uint32_t)written < max - 1; i--) {
        buf[written++] = tmp[i];
    }
    buf[written] = '\0';
    return written;
}

/* Skip leading whitespace and return pointer past it. */
static const char *skip_ws(const char *s)
{
    while (*s && is_space(*s))
        s++;
    return s;
}

/* Extract the first whitespace-delimited word after a position.
 * Skips leading spaces, then copies up to the next space or end of string. */
static void extract_arg(const char *after, char *arg, uint32_t max)
{
    after = skip_ws(after);
    uint32_t i = 0;
    while (after[i] && !is_space(after[i]) && i < max - 1) {
        arg[i] = after[i];
        i++;
    }
    arg[i] = '\0';
}

/* Extract the rest of the string after a position (trimmed both sides). */
static void extract_rest(const char *after, char *arg, uint32_t max)
{
    after = skip_ws(after);
    safe_copy(arg, after, max);

    /* Trim trailing whitespace */
    uint32_t len = str_len(arg);
    while (len > 0 && is_space(arg[len - 1])) {
        arg[--len] = '\0';
    }
}

/* ========================================================================
 * Action Name Lookup Table
 * ======================================================================== */

typedef struct {
    action_type_t  type;
    const char    *name;
} action_name_entry_t;

static const action_name_entry_t g_action_names[] = {
    { ACTION_FS_LIST,        "fs_list"        },
    { ACTION_FS_READ,        "fs_read"        },
    { ACTION_FS_WRITE,       "fs_write"       },
    { ACTION_FS_DELETE,      "fs_delete"      },
    { ACTION_FS_CREATE,      "fs_create"      },
    { ACTION_NET_STATUS,     "net_status"     },
    { ACTION_NET_CONNECT,    "net_connect"    },
    { ACTION_DRIVER_LIST,    "driver_list"    },
    { ACTION_DRIVER_SEARCH,  "driver_search"  },
    { ACTION_DRIVER_INSTALL, "driver_install" },
    { ACTION_SYS_INFO,       "sys_info"       },
    { ACTION_SYS_REBOOT,     "sys_reboot"     },
    { ACTION_SYS_SHUTDOWN,   "sys_shutdown"   },
    { ACTION_SYS_UPDATE,     "sys_update"     },
    { ACTION_SYS_UPTIME,     "sys_uptime"     },
    { ACTION_DISPLAY_SET,    "display_set"    },
    { ACTION_TASK_LIST,      "task_list"      },
    { ACTION_MEMORY_INFO,    "memory_info"    },
    { ACTION_HELP,           "help"           },
    { ACTION_NONE,           "none"           },
};

#define ACTION_NAME_COUNT (sizeof(g_action_names) / sizeof(g_action_names[0]))

const char *ai_action_name(action_type_t type)
{
    for (uint32_t i = 0; i < ACTION_NAME_COUNT; i++) {
        if (g_action_names[i].type == type)
            return g_action_names[i].name;
    }
    return "none";
}

action_type_t ai_action_from_name(const char *name)
{
    if (!name)
        return ACTION_NONE;

    for (uint32_t i = 0; i < ACTION_NAME_COUNT; i++) {
        if (ci_eq(name, g_action_names[i].name))
            return g_action_names[i].type;
    }
    return ACTION_NONE;
}

/* ========================================================================
 * Chat Session (Global Singleton)
 * ======================================================================== */

static ai_chat_session_t g_session;

/* ========================================================================
 * Language Detection
 *
 * Arabic UTF-8 bytes: the Arabic block (U+0600..U+06FF) encodes as
 *   0xD8 0x80 .. 0xDB 0xBF
 * If we see a lead byte in 0xD8..0xDB, it is very likely Arabic text.
 *
 * We also check for known Arabic keyword substrings as a secondary signal.
 * ======================================================================== */

/* Arabic keyword fragments (UTF-8 encoded) used for language detection. */
static const char * const g_arabic_keywords[] = {
    "\xd8\xa7\xd8\xb9\xd8\xb1\xd8\xb6",                                         /* اعرض */
    "\xd8\xa7\xd9\x81\xd8\xaa\xd8\xad",                                         /* افتح */
    "\xd8\xa7\xd8\xad\xd8\xb0\xd9\x81",                                         /* احذف */
    "\xd8\xad\xd8\xa7\xd9\x84\xd8\xa9",                                         /* حالة */
    "\xd8\xa7\xd8\xaa\xd8\xb5\xd9\x84",                                         /* اتصل */
    "\xd9\x85\xd8\xb9\xd9\x84\xd9\x88\xd9\x85\xd8\xa7\xd8\xaa",               /* معلومات */
    "\xd8\xa3\xd8\xb9\xd8\xaf",                                                   /* أعد */
    "\xd8\xa7\xd9\x84\xd8\xb3\xd8\xa7\xd8\xa6\xd9\x82\xd9\x8a\xd9\x86",       /* السائقين */
    "\xd8\xa7\xd9\x84\xd8\xb4\xd8\xa7\xd8\xb4\xd8\xa9",                       /* الشاشة */
    "\xd9\x85\xd8\xb3\xd8\xa7\xd8\xb9\xd8\xaf\xd8\xa9",                       /* مساعدة */
    "\xd8\xa7\xd9\x84\xd8\xb0\xd8\xa7\xd9\x83\xd8\xb1\xd8\xa9",               /* الذاكرة */
    "\xd8\xa7\xd9\x86\xd8\xb4\xd8\xa6",                                         /* انشئ */
    "\xd8\xa7\xd9\x82\xd8\xb1\xd8\xa3",                                         /* اقرأ */
    "\xd8\xa7\xd9\x84\xd9\x85\xd9\x84\xd9\x81\xd8\xa7\xd8\xaa",               /* الملفات */
    "\xd8\xa7\xd8\xba\xd9\x84\xd8\xa7\xd9\x82",                               /* اغلاق */
    "\xd8\xaa\xd8\xad\xd8\xaf\xd9\x8a\xd8\xab",                               /* تحديث */
};

#define ARABIC_KW_COUNT (sizeof(g_arabic_keywords) / sizeof(g_arabic_keywords[0]))

/* Returns 1 if input appears to be Arabic, 0 for English. */
static int detect_arabic(const char *input)
{
    /* Primary: check for Arabic UTF-8 lead bytes (0xD8..0xDB) */
    for (uint32_t i = 0; input[i]; i++) {
        uint8_t b = (uint8_t)input[i];
        if (b >= 0xD8 && b <= 0xDB)
            return 1;
    }

    /* Secondary: check for known Arabic keyword byte sequences */
    for (uint32_t k = 0; k < ARABIC_KW_COUNT; k++) {
        if (byte_strstr(input, g_arabic_keywords[k]))
            return 1;
    }

    return 0;
}

/* Update the session's language field based on input. */
static void detect_language(const char *input)
{
    if (detect_arabic(input)) {
        g_session.language[0] = 'a';
        g_session.language[1] = 'r';
    } else {
        g_session.language[0] = 'e';
        g_session.language[1] = 'n';
    }
    g_session.language[2] = '\0';
}

/* ========================================================================
 * History Ring Buffer
 * ======================================================================== */

static void history_add(int is_user, const char *text)
{
    uint32_t idx = g_session.history_head;

    g_session.history[idx].is_user = is_user;
    safe_copy(g_session.history[idx].text, text,
              sizeof(g_session.history[idx].text));

    g_session.history_head = (idx + 1) % AI_CHAT_HISTORY_MAX;

    if (g_session.history_count < AI_CHAT_HISTORY_MAX)
        g_session.history_count++;
}

/* Get the i-th oldest message in history (0 = oldest retained).
 * Returns NULL if index is out of range. */
static const chat_message_t *history_get(uint32_t i)
{
    if (i >= g_session.history_count)
        return (const chat_message_t *)0;

    uint32_t start;
    if (g_session.history_count < AI_CHAT_HISTORY_MAX)
        start = 0;
    else
        start = g_session.history_head; /* oldest is at head (about to be overwritten) */

    uint32_t idx = (start + i) % AI_CHAT_HISTORY_MAX;
    return &g_session.history[idx];
}

/* ========================================================================
 * Local NLP Parser -- Pattern Matching Tables
 *
 * Each entry maps a keyword/phrase to an action type, with metadata about
 * whether an argument follows, whether confirmation is needed, and the
 * confidence level.
 *
 * Longer patterns are preferred over shorter ones (best-match strategy).
 * ======================================================================== */

typedef struct {
    const char    *pattern;       /* Keyword or phrase to match                      */
    action_type_t  action;        /* Resulting action type                           */
    int            has_arg;       /* 1 = extract the text after the pattern as arg   */
    int            needs_confirm; /* 1 = destructive action, needs user confirmation */
    int            confidence;    /* 0-100 confidence level                          */
    int            is_arabic;     /* 1 = exact byte match (UTF-8), 0 = ci ASCII     */
} nlp_pattern_t;

static const nlp_pattern_t g_patterns[] = {

    /* ==== English: File commands ==== */
    { "show files",       ACTION_FS_LIST,    0, 0, 90, 0 },
    { "list files",       ACTION_FS_LIST,    0, 0, 90, 0 },
    { "ls",               ACTION_FS_LIST,    0, 0, 90, 0 },
    { "dir",              ACTION_FS_LIST,    0, 0, 70, 0 },
    { "open ",            ACTION_FS_READ,    1, 0, 90, 0 },
    { "read ",            ACTION_FS_READ,    1, 0, 90, 0 },
    { "cat ",             ACTION_FS_READ,    1, 0, 90, 0 },
    { "show ",            ACTION_FS_READ,    1, 0, 70, 0 },
    { "create file ",     ACTION_FS_CREATE,  1, 0, 90, 0 },
    { "create ",          ACTION_FS_CREATE,  1, 0, 70, 0 },
    { "new file ",        ACTION_FS_CREATE,  1, 0, 90, 0 },
    { "touch ",           ACTION_FS_CREATE,  1, 0, 90, 0 },
    { "delete ",          ACTION_FS_DELETE,  1, 1, 90, 0 },
    { "remove ",          ACTION_FS_DELETE,  1, 1, 90, 0 },
    { "rm ",              ACTION_FS_DELETE,  1, 1, 90, 0 },
    { "write ",           ACTION_FS_WRITE,   1, 0, 70, 0 },

    /* ==== English: Network ==== */
    { "network status",   ACTION_NET_STATUS,  0, 0, 90, 0 },
    { "net status",       ACTION_NET_STATUS,  0, 0, 90, 0 },
    { "show network",     ACTION_NET_STATUS,  0, 0, 90, 0 },
    { "wifi status",      ACTION_NET_STATUS,  0, 0, 90, 0 },
    { "wifi",             ACTION_NET_STATUS,  0, 0, 70, 0 },
    { "connect",          ACTION_NET_CONNECT, 0, 0, 90, 0 },
    { "dhcp",             ACTION_NET_CONNECT, 0, 0, 90, 0 },

    /* ==== English: System ==== */
    { "system info",      ACTION_SYS_INFO,     0, 0, 90, 0 },
    { "sysinfo",          ACTION_SYS_INFO,     0, 0, 90, 0 },
    { "system status",    ACTION_SYS_INFO,     0, 0, 90, 0 },
    { "about",            ACTION_SYS_INFO,     0, 0, 70, 0 },
    { "reboot",           ACTION_SYS_REBOOT,   0, 1, 90, 0 },
    { "restart",          ACTION_SYS_REBOOT,   0, 1, 90, 0 },
    { "shutdown",         ACTION_SYS_SHUTDOWN, 0, 1, 90, 0 },
    { "power off",        ACTION_SYS_SHUTDOWN, 0, 1, 90, 0 },
    { "halt",             ACTION_SYS_SHUTDOWN, 0, 1, 70, 0 },
    { "uptime",           ACTION_SYS_UPTIME,   0, 0, 90, 0 },
    { "update",           ACTION_SYS_UPDATE,   0, 0, 90, 0 },
    { "check updates",    ACTION_SYS_UPDATE,   0, 0, 90, 0 },

    /* ==== English: Drivers ==== */
    { "list drivers",     ACTION_DRIVER_LIST,    0, 0, 90, 0 },
    { "show drivers",     ACTION_DRIVER_LIST,    0, 0, 90, 0 },
    { "drivers",          ACTION_DRIVER_LIST,    0, 0, 70, 0 },
    { "install driver ",  ACTION_DRIVER_INSTALL, 1, 1, 90, 0 },
    { "search driver ",   ACTION_DRIVER_SEARCH,  1, 0, 90, 0 },
    { "find driver ",     ACTION_DRIVER_SEARCH,  1, 0, 90, 0 },

    /* ==== English: Display ==== */
    { "change resolution", ACTION_DISPLAY_SET, 0, 0, 90, 0 },
    { "set resolution",    ACTION_DISPLAY_SET, 0, 0, 90, 0 },
    { "brightness",        ACTION_DISPLAY_SET, 0, 0, 70, 0 },
    { "display settings",  ACTION_DISPLAY_SET, 0, 0, 90, 0 },

    /* ==== English: Memory ==== */
    { "memory info",     ACTION_MEMORY_INFO, 0, 0, 90, 0 },
    { "memory usage",    ACTION_MEMORY_INFO, 0, 0, 90, 0 },
    { "memory",          ACTION_MEMORY_INFO, 0, 0, 70, 0 },
    { "ram",             ACTION_MEMORY_INFO, 0, 0, 70, 0 },
    { "free memory",     ACTION_MEMORY_INFO, 0, 0, 90, 0 },

    /* ==== English: Tasks ==== */
    { "list tasks",      ACTION_TASK_LIST, 0, 0, 90, 0 },
    { "show tasks",      ACTION_TASK_LIST, 0, 0, 90, 0 },
    { "processes",       ACTION_TASK_LIST, 0, 0, 70, 0 },

    /* ==== English: Help ==== */
    { "help",            ACTION_HELP, 0, 0, 90, 0 },
    { "what can you do", ACTION_HELP, 0, 0, 90, 0 },
    { "commands",        ACTION_HELP, 0, 0, 70, 0 },
    { "usage",           ACTION_HELP, 0, 0, 70, 0 },

    /* ==== Arabic: File commands ==== */
    /* اعرض الملفات (show files) */
    { "\xd8\xa7\xd8\xb9\xd8\xb1\xd8\xb6 \xd8\xa7\xd9\x84\xd9\x85\xd9\x84\xd9\x81\xd8\xa7\xd8\xaa",
      ACTION_FS_LIST, 0, 0, 90, 1 },
    /* اعرض (show -- general list fallback) */
    { "\xd8\xa7\xd8\xb9\xd8\xb1\xd8\xb6",
      ACTION_FS_LIST, 0, 0, 70, 1 },
    /* افتح (open) + arg */
    { "\xd8\xa7\xd9\x81\xd8\xaa\xd8\xad ",
      ACTION_FS_READ, 1, 0, 90, 1 },
    /* اقرأ (read) + arg */
    { "\xd8\xa7\xd9\x82\xd8\xb1\xd8\xa3 ",
      ACTION_FS_READ, 1, 0, 90, 1 },
    /* انشئ (create) + arg */
    { "\xd8\xa7\xd9\x86\xd8\xb4\xd8\xa6 ",
      ACTION_FS_CREATE, 1, 0, 90, 1 },
    /* احذف (delete) + arg -- destructive */
    { "\xd8\xa7\xd8\xad\xd8\xb0\xd9\x81 ",
      ACTION_FS_DELETE, 1, 1, 90, 1 },

    /* ==== Arabic: Network ==== */
    /* حالة الشبكة (network status) */
    { "\xd8\xad\xd8\xa7\xd9\x84\xd8\xa9 \xd8\xa7\xd9\x84\xd8\xb4\xd8\xa8\xd9\x83\xd8\xa9",
      ACTION_NET_STATUS, 0, 0, 90, 1 },
    /* اتصل (connect) */
    { "\xd8\xa7\xd8\xaa\xd8\xb5\xd9\x84",
      ACTION_NET_CONNECT, 0, 0, 90, 1 },

    /* ==== Arabic: System ==== */
    /* معلومات النظام (system info) */
    { "\xd9\x85\xd8\xb9\xd9\x84\xd9\x88\xd9\x85\xd8\xa7\xd8\xaa \xd8\xa7\xd9\x84\xd9\x86\xd8\xb8\xd8\xa7\xd9\x85",
      ACTION_SYS_INFO, 0, 0, 90, 1 },
    /* أعد التشغيل (reboot) -- destructive */
    { "\xd8\xa3\xd8\xb9\xd8\xaf \xd8\xa7\xd9\x84\xd8\xaa\xd8\xb4\xd8\xba\xd9\x8a\xd9\x84",
      ACTION_SYS_REBOOT, 0, 1, 90, 1 },
    /* اغلاق (shutdown) -- destructive */
    { "\xd8\xa7\xd8\xba\xd9\x84\xd8\xa7\xd9\x82",
      ACTION_SYS_SHUTDOWN, 0, 1, 90, 1 },
    /* تحديث (update) */
    { "\xd8\xaa\xd8\xad\xd8\xaf\xd9\x8a\xd8\xab",
      ACTION_SYS_UPDATE, 0, 0, 90, 1 },

    /* ==== Arabic: Drivers ==== */
    /* السائقين (drivers) */
    { "\xd8\xa7\xd9\x84\xd8\xb3\xd8\xa7\xd8\xa6\xd9\x82\xd9\x8a\xd9\x86",
      ACTION_DRIVER_LIST, 0, 0, 90, 1 },

    /* ==== Arabic: Display ==== */
    /* الشاشة (display/screen) */
    { "\xd8\xa7\xd9\x84\xd8\xb4\xd8\xa7\xd8\xb4\xd8\xa9",
      ACTION_DISPLAY_SET, 0, 0, 70, 1 },

    /* ==== Arabic: Memory ==== */
    /* الذاكرة (memory) */
    { "\xd8\xa7\xd9\x84\xd8\xb0\xd8\xa7\xd9\x83\xd8\xb1\xd8\xa9",
      ACTION_MEMORY_INFO, 0, 0, 90, 1 },

    /* ==== Arabic: Help ==== */
    /* مساعدة (help) */
    { "\xd9\x85\xd8\xb3\xd8\xa7\xd8\xb9\xd8\xaf\xd8\xa9",
      ACTION_HELP, 0, 0, 90, 1 },
};

#define NLP_PATTERN_COUNT (sizeof(g_patterns) / sizeof(g_patterns[0]))

/* ========================================================================
 * ai_parse_local -- Local NLP Parser
 *
 * Strategy: scan all patterns, pick the longest match with the highest
 * confidence.  This avoids ambiguous short-pattern matches (e.g. "show"
 * should not beat "show files" if both are present in the input).
 * ======================================================================== */

int ai_parse_local(const char *input, ai_action_t *action)
{
    if (!input || !action)
        return 0;

    /* Initialize action to "nothing recognized" */
    action->type = ACTION_NONE;
    action->args[0] = '\0';
    action->needs_confirm = 0;
    action->confidence = 0;

    /* Skip leading whitespace */
    input = skip_ws(input);
    if (input[0] == '\0')
        return 0;

    /* Best-match tracking */
    int best_idx = -1;
    uint32_t best_len = 0;
    int best_conf = 0;

    for (uint32_t p = 0; p < NLP_PATTERN_COUNT; p++) {
        const nlp_pattern_t *pat = &g_patterns[p];
        const char *match;

        uint32_t plen = str_len(pat->pattern);

        if (pat->is_arabic) {
            /* Arabic patterns: exact byte-level match */
            match = byte_strstr(input, pat->pattern);
        } else {
            /* English patterns: case-insensitive substring match */
            match = ci_strstr(input, pat->pattern);
        }

        if (match) {
            /* Prefer longer patterns, then higher confidence */
            if (plen > best_len ||
                (plen == best_len && pat->confidence > best_conf)) {
                best_idx = (int)p;
                best_len = plen;
                best_conf = pat->confidence;
            }
        }
    }

    if (best_idx < 0)
        return 0;

    const nlp_pattern_t *best = &g_patterns[best_idx];

    action->type = best->action;
    action->needs_confirm = best->needs_confirm;
    action->confidence = best->confidence;

    /* Extract argument text if this pattern expects one */
    if (best->has_arg) {
        const char *match;
        if (best->is_arabic)
            match = byte_strstr(input, best->pattern);
        else
            match = ci_strstr(input, best->pattern);

        if (match) {
            const char *after = match + str_len(best->pattern);
            extract_rest(after, action->args, sizeof(action->args));
        }
    }

    return 1;
}

/* ========================================================================
 * fs_list callback -- formats directory entries for ACTION_FS_LIST
 * ======================================================================== */

typedef struct {
    char     *buf;      /* Output buffer */
    uint32_t  max;      /* Buffer capacity */
    int       count;    /* Number of entries appended */
} fs_list_ctx_t;

static void fs_list_callback(const char *name, uint64_t size, void *ctx)
{
    fs_list_ctx_t *c = (fs_list_ctx_t *)ctx;

    /* Format: "  name (size unit)\n" */
    safe_append(c->buf, "  ", c->max);
    safe_append(c->buf, name, c->max);
    safe_append(c->buf, " (", c->max);

    char num[21];
    if (size >= 1024 * 1024) {
        fmt_u64(num, sizeof(num), size / (1024 * 1024));
        safe_append(c->buf, num, c->max);
        safe_append(c->buf, " MB", c->max);
    } else if (size >= 1024) {
        fmt_u64(num, sizeof(num), size / 1024);
        safe_append(c->buf, num, c->max);
        safe_append(c->buf, " KB", c->max);
    } else {
        fmt_u64(num, sizeof(num), size);
        safe_append(c->buf, num, c->max);
        safe_append(c->buf, " bytes", c->max);
    }

    safe_append(c->buf, ")\n", c->max);
    c->count++;
}

/* ========================================================================
 * ai_execute_action -- Action Executor
 *
 * Calls the appropriate HAL / FS / driver API for each action type and
 * writes the result into the output buffer.
 * ======================================================================== */

/* Scratch buffer for file read data (avoids stack allocation of large arrays) */
static char g_file_read_buf[4096];

int ai_execute_action(const ai_action_t *action, char *output, int output_size)
{
    if (!action || !output || output_size <= 0)
        return -1;

    output[0] = '\0';
    uint32_t omax = (uint32_t)output_size;

    switch (action->type) {

    /* ------------------------------------------------------------------ */
    case ACTION_FS_LIST: {
        fs_list_ctx_t ctx;
        ctx.buf = output;
        ctx.max = omax;
        ctx.count = 0;

        int rc = fs_list(fs_list_callback, &ctx);
        if (rc < 0) {
            safe_copy(output,
                "Error: filesystem not initialized or no storage available.",
                omax);
            return -1;
        }

        if (ctx.count == 0) {
            safe_copy(output, "No files found on disk.", omax);
        } else {
            /* Prepend a header line: shift existing content right */
            char header[64];
            char num[12];
            header[0] = '\0';
            fmt_u64(num, sizeof(num), (uint64_t)ctx.count);
            safe_copy(header, num, sizeof(header));
            safe_append(header, " file(s) found:\n", sizeof(header));

            uint32_t hlen = str_len(header);
            uint32_t olen = str_len(output);
            uint32_t total = hlen + olen;
            if (total >= omax)
                total = omax - 1;

            /* Shift content right to make room for header */
            uint32_t to_keep = total - hlen;
            for (uint32_t i = to_keep; i > 0; i--)
                output[hlen + i - 1] = output[i - 1];
            output[total] = '\0';

            /* Copy header into front */
            for (uint32_t i = 0; i < hlen && i < total; i++)
                output[i] = header[i];
        }
        return 0;
    }

    /* ------------------------------------------------------------------ */
    case ACTION_FS_READ: {
        if (action->args[0] == '\0') {
            safe_copy(output, "Error: no filename specified.", omax);
            return -1;
        }

        int fd = fs_open(action->args);
        if (fd < 0) {
            safe_copy(output, "Error: file '", omax);
            safe_append(output, action->args, omax);
            safe_append(output, "' not found.", omax);
            return -1;
        }

        uint64_t fsize = fs_size(fd);
        uint64_t to_read = fsize;
        if (to_read > sizeof(g_file_read_buf) - 1)
            to_read = sizeof(g_file_read_buf) - 1;

        int64_t bytes = fs_read(fd, g_file_read_buf, 0, to_read);
        fs_close(fd);

        if (bytes < 0) {
            safe_copy(output, "Error: failed to read file.", omax);
            return -1;
        }

        g_file_read_buf[bytes] = '\0';

        /* Build output: "File: name (size bytes)\n---\ncontent" */
        safe_copy(output, "File: ", omax);
        safe_append(output, action->args, omax);
        safe_append(output, " (", omax);

        char num[21];
        fmt_u64(num, sizeof(num), fsize);
        safe_append(output, num, omax);
        safe_append(output, " bytes)\n---\n", omax);
        safe_append(output, g_file_read_buf, omax);

        if (to_read < fsize)
            safe_append(output,
                "\n... (truncated, file is larger than display buffer)", omax);

        return 0;
    }

    /* ------------------------------------------------------------------ */
    case ACTION_FS_CREATE: {
        if (action->args[0] == '\0') {
            safe_copy(output, "Error: no filename specified.", omax);
            return -1;
        }

        int rc = fs_create(action->args, 1);
        if (rc < 0) {
            safe_copy(output, "Error: could not create file '", omax);
            safe_append(output, action->args, omax);
            safe_append(output,
                "'. It may already exist or the disk may be full.", omax);
            return -1;
        }

        safe_copy(output, "File '", omax);
        safe_append(output, action->args, omax);
        safe_append(output, "' created successfully (2 MB reserved).", omax);
        return 0;
    }

    /* ------------------------------------------------------------------ */
    case ACTION_FS_DELETE: {
        if (action->args[0] == '\0') {
            safe_copy(output, "Error: no filename specified.", omax);
            return -1;
        }

        int rc = fs_delete(action->args);
        if (rc < 0) {
            safe_copy(output, "Error: could not delete file '", omax);
            safe_append(output, action->args, omax);
            safe_append(output, "'. File not found.", omax);
            return -1;
        }

        safe_copy(output, "File '", omax);
        safe_append(output, action->args, omax);
        safe_append(output, "' deleted.", omax);
        return 0;
    }

    /* ------------------------------------------------------------------ */
    case ACTION_FS_WRITE: {
        if (action->args[0] == '\0') {
            safe_copy(output,
                "Usage: write <filename> <data>\n"
                "Specify a filename and the data to write.", omax);
            return -1;
        }

        /* Parse "filename rest-of-data" from args */
        char fname[BMFS_MAX_FILENAME];
        extract_arg(action->args, fname, sizeof(fname));

        if (fname[0] == '\0') {
            safe_copy(output, "Error: no filename provided.", omax);
            return -1;
        }

        int fd = fs_open(fname);
        if (fd < 0) {
            safe_copy(output, "Error: file '", omax);
            safe_append(output, fname, omax);
            safe_append(output, "' not found. Create it first.", omax);
            return -1;
        }

        /* Find where data starts (after filename in args) */
        const char *rest = action->args;
        while (*rest && !is_space(*rest))
            rest++;
        rest = skip_ws(rest);

        uint32_t dlen = str_len(rest);
        if (dlen == 0) {
            fs_close(fd);
            safe_copy(output, "Error: no data provided to write.", omax);
            return -1;
        }

        int64_t written = fs_write(fd, rest, 0, dlen);
        fs_close(fd);

        if (written < 0) {
            safe_copy(output, "Error: write failed.", omax);
            return -1;
        }

        safe_copy(output, "Wrote ", omax);
        char num[21];
        fmt_u64(num, sizeof(num), (uint64_t)written);
        safe_append(output, num, omax);
        safe_append(output, " bytes to '", omax);
        safe_append(output, fname, omax);
        safe_append(output, "'.", omax);
        return 0;
    }

    /* ------------------------------------------------------------------ */
    case ACTION_NET_STATUS: {
        const driver_ops_t *net = driver_get_network();
        if (!net) {
            safe_copy(output,
                "Network: no network driver loaded.\n"
                "No NIC detected or driver not yet initialized.", omax);
            return 0;
        }

        safe_copy(output, "Network Status:\n", omax);
        safe_append(output, "  Driver: ", omax);
        safe_append(output, net->name ? net->name : "unknown", omax);
        safe_append(output, "\n", omax);

        /* MAC address */
        if (net->net_get_mac) {
            uint8_t mac[6];
            net->net_get_mac(mac);

            safe_append(output, "  MAC: ", omax);
            char hex[4];
            for (int i = 0; i < 6; i++) {
                if (i > 0)
                    safe_append(output, ":", omax);
                hex[0] = "0123456789ab"[(mac[i] >> 4) & 0xF];
                hex[1] = "0123456789ab"[mac[i] & 0xF];
                hex[2] = '\0';
                safe_append(output, hex, omax);
            }
            safe_append(output, "\n", omax);
        }

        safe_append(output, "  Status: link up (driver active)\n", omax);
        return 0;
    }

    /* ------------------------------------------------------------------ */
    case ACTION_NET_CONNECT: {
        const driver_ops_t *net = driver_get_network();
        if (!net) {
            safe_copy(output, "Error: no network driver available.", omax);
            return -1;
        }

        safe_copy(output, "Network connection initiated via driver '", omax);
        safe_append(output, net->name ? net->name : "unknown", omax);
        safe_append(output,
            "'.\nDHCP/IP configuration is handled by the AI bootstrap module.",
            omax);
        return 0;
    }

    /* ------------------------------------------------------------------ */
    case ACTION_SYS_INFO: {
        hal_cpu_info_t cpu;
        hal_cpu_get_info(&cpu);

        safe_copy(output, "AlJefra OS v1.0\n", omax);
        safe_append(output, "Architecture: ", omax);

        switch (hal_arch()) {
        case HAL_ARCH_X86_64:
            safe_append(output, "x86-64\n", omax);
            break;
        case HAL_ARCH_AARCH64:
            safe_append(output, "AArch64 (ARM64)\n", omax);
            break;
        case HAL_ARCH_RISCV64:
            safe_append(output, "RISC-V 64-bit\n", omax);
            break;
        }

        safe_append(output, "CPU: ", omax);
        safe_append(output, cpu.model, omax);
        safe_append(output, "\n", omax);

        safe_append(output, "Vendor: ", omax);
        safe_append(output, cpu.vendor, omax);
        safe_append(output, "\n", omax);

        char num[21];
        safe_append(output, "Cores: ", omax);
        fmt_u64(num, sizeof(num), (uint64_t)cpu.cores_logical);
        safe_append(output, num, omax);
        safe_append(output, "\n", omax);

        uint64_t total_mb = hal_mmu_total_ram() / (1024 * 1024);
        safe_append(output, "RAM: ", omax);
        fmt_u64(num, sizeof(num), total_mb);
        safe_append(output, num, omax);
        safe_append(output, " MB\n", omax);

        safe_append(output, "Cache line: ", omax);
        fmt_u64(num, sizeof(num), (uint64_t)cpu.cache_line_bytes);
        safe_append(output, num, omax);
        safe_append(output, " bytes\n", omax);

        /* CPU feature flags */
        safe_append(output, "Features:", omax);
        if (cpu.features & HAL_CPU_FEAT_FPU)    safe_append(output, " FPU", omax);
        if (cpu.features & HAL_CPU_FEAT_SSE)    safe_append(output, " SSE", omax);
        if (cpu.features & HAL_CPU_FEAT_AVX)    safe_append(output, " AVX", omax);
        if (cpu.features & HAL_CPU_FEAT_NEON)   safe_append(output, " NEON", omax);
        if (cpu.features & HAL_CPU_FEAT_SVE)    safe_append(output, " SVE", omax);
        if (cpu.features & HAL_CPU_FEAT_RVVEC)  safe_append(output, " RVV", omax);
        if (cpu.features & HAL_CPU_FEAT_RDRAND) safe_append(output, " RDRAND", omax);
        if (cpu.features & HAL_CPU_FEAT_AES)    safe_append(output, " AES", omax);
        safe_append(output, "\n", omax);

        return 0;
    }

    /* ------------------------------------------------------------------ */
    case ACTION_SYS_REBOOT: {
        safe_copy(output, "System reboot initiated...\n", omax);
        safe_append(output, "Flushing filesystem...\n", omax);

        /* Sync FS before reset */
        fs_sync();

        /* On x86-64, pulse the keyboard controller reset line (port 0x64).
         * On ARM/RISC-V, a PSCI/SBI call would be needed. */
#if defined(__x86_64__) || defined(_M_X64)
        hal_port_out8(0x64, 0xFE);
#endif
        safe_append(output,
            "Reboot command sent. If the system does not restart,\n"
            "a manual power cycle may be required.", omax);
        return 0;
    }

    /* ------------------------------------------------------------------ */
    case ACTION_SYS_SHUTDOWN: {
        safe_copy(output, "System shutdown initiated...\n", omax);
        safe_append(output, "Flushing filesystem...\n", omax);

        fs_sync();

        /* QEMU PIIX4 ACPI power-off: write 0x2000 to port 0x604. */
#if defined(__x86_64__) || defined(_M_X64)
        hal_port_out16(0x604, 0x2000);
#endif
        safe_append(output, "Shutdown command sent. System halting.\n", omax);

        /* Halt CPU as fallback */
        hal_cpu_disable_interrupts();
        for (;;)
            hal_cpu_halt();

        return 0; /* unreachable */
    }

    /* ------------------------------------------------------------------ */
    case ACTION_SYS_UPDATE: {
        safe_copy(output, "Checking for updates...\n", omax);
        safe_append(output, "AlJefra OS v1.0 is the current version.\n", omax);

        const driver_ops_t *net = driver_get_network();
        if (net) {
            safe_append(output,
                "Network is available. The marketplace can be queried for\n"
                "driver and system updates via the AI bootstrap module.", omax);
        } else {
            safe_append(output,
                "No network connection. Connect to a network first to "
                "check for updates.", omax);
        }
        return 0;
    }

    /* ------------------------------------------------------------------ */
    case ACTION_SYS_UPTIME: {
        uint64_t ms = hal_timer_ms();
        uint64_t secs  = ms / 1000;
        uint64_t mins  = secs / 60;
        uint64_t hours = mins / 60;
        uint64_t days  = hours / 24;

        char num[21];
        safe_copy(output, "System uptime: ", omax);

        if (days > 0) {
            fmt_u64(num, sizeof(num), days);
            safe_append(output, num, omax);
            safe_append(output, " day(s), ", omax);
        }

        fmt_u64(num, sizeof(num), hours % 24);
        safe_append(output, num, omax);
        safe_append(output, "h ", omax);

        fmt_u64(num, sizeof(num), mins % 60);
        safe_append(output, num, omax);
        safe_append(output, "m ", omax);

        fmt_u64(num, sizeof(num), secs % 60);
        safe_append(output, num, omax);
        safe_append(output, "s", omax);

        return 0;
    }

    /* ------------------------------------------------------------------ */
    case ACTION_DRIVER_LIST: {
        const driver_ops_t *drivers[MAX_DRIVERS];
        uint32_t count = driver_list(drivers, MAX_DRIVERS);

        if (count == 0) {
            safe_copy(output, "No drivers currently loaded.", omax);
            return 0;
        }

        char num[21];
        fmt_u64(num, sizeof(num), (uint64_t)count);
        safe_copy(output, num, omax);
        safe_append(output, " driver(s) loaded:\n", omax);

        static const char * const cat_names[] = {
            "storage", "network", "input", "display", "gpu", "bus", "other"
        };

        for (uint32_t i = 0; i < count; i++) {
            safe_append(output, "  ", omax);
            safe_append(output,
                drivers[i]->name ? drivers[i]->name : "(unnamed)", omax);
            safe_append(output, " [", omax);

            int cat = (int)drivers[i]->category;
            if (cat >= 0 && cat <= 6)
                safe_append(output, cat_names[cat], omax);
            else
                safe_append(output, "unknown", omax);

            safe_append(output, "]\n", omax);
        }
        return 0;
    }

    /* ------------------------------------------------------------------ */
    case ACTION_DRIVER_SEARCH: {
        safe_copy(output, "Searching marketplace for drivers", omax);
        if (action->args[0]) {
            safe_append(output, " matching '", omax);
            safe_append(output, action->args, omax);
            safe_append(output, "'", omax);
        }
        safe_append(output, "...\n", omax);

        const driver_ops_t *net = driver_get_network();
        if (!net) {
            safe_append(output,
                "Error: no network connection. Cannot reach the marketplace.",
                omax);
            return -1;
        }

        safe_append(output,
            "The AI bootstrap module will query the AlJefra marketplace\n"
            "at os.aljefra.com/api/v1/drivers for matching .ajdrv packages.",
            omax);
        return 0;
    }

    /* ------------------------------------------------------------------ */
    case ACTION_DRIVER_INSTALL: {
        if (action->args[0] == '\0') {
            safe_copy(output,
                "Error: specify a driver name to install.", omax);
            return -1;
        }

        safe_copy(output, "Driver installation for '", omax);
        safe_append(output, action->args, omax);
        safe_append(output, "' requested.\n", omax);

        const driver_ops_t *net = driver_get_network();
        if (!net) {
            safe_append(output,
                "Error: no network connection. Cannot download from marketplace.",
                omax);
            return -1;
        }

        safe_append(output,
            "The AI bootstrap module will download, verify (Ed25519),\n"
            "and load the .ajdrv package at runtime.", omax);
        return 0;
    }

    /* ------------------------------------------------------------------ */
    case ACTION_DISPLAY_SET: {
        hal_console_type_t ctype = hal_console_type();

        safe_copy(output, "Display Settings:\n", omax);
        safe_append(output, "  Console backend: ", omax);

        switch (ctype) {
        case HAL_CONSOLE_SERIAL:
            safe_append(output, "Serial (UART)\n", omax);
            safe_append(output,
                "  Resolution changes are not applicable in serial mode.\n",
                omax);
            break;
        case HAL_CONSOLE_VGA:
            safe_append(output, "VGA text mode (80x25)\n", omax);
            safe_append(output,
                "  VGA text mode does not support resolution changes.\n"
                "  A framebuffer driver is needed for graphical modes.\n",
                omax);
            break;
        case HAL_CONSOLE_LFB:
            safe_append(output, "Framebuffer (graphical)\n", omax);
            safe_append(output,
                "  Resolution and brightness can be adjusted through the "
                "display driver.\n", omax);
            break;
        }

        return 0;
    }

    /* ------------------------------------------------------------------ */
    case ACTION_TASK_LIST: {
        safe_copy(output, "Task List:\n", omax);
        safe_append(output, "  [0] kernel (running)\n", omax);
        safe_append(output, "  [1] ai_chat (active)\n", omax);

        uint32_t cores = hal_cpu_count();
        char num[21];
        safe_append(output, "CPU cores online: ", omax);
        fmt_u64(num, sizeof(num), (uint64_t)cores);
        safe_append(output, num, omax);
        safe_append(output, "\n", omax);
        safe_append(output,
            "The scheduler manages cooperative tasks on all available cores.",
            omax);
        return 0;
    }

    /* ------------------------------------------------------------------ */
    case ACTION_MEMORY_INFO: {
        uint64_t total    = hal_mmu_total_ram();
        uint64_t free_ram = hal_mmu_free_ram();
        uint64_t used     = (total > free_ram) ? (total - free_ram) : 0;

        char num[21];

        safe_copy(output, "Memory Information:\n", omax);

        safe_append(output, "  Total RAM: ", omax);
        fmt_u64(num, sizeof(num), total / (1024 * 1024));
        safe_append(output, num, omax);
        safe_append(output, " MB\n", omax);

        safe_append(output, "  Used:      ", omax);
        fmt_u64(num, sizeof(num), used / (1024 * 1024));
        safe_append(output, num, omax);
        safe_append(output, " MB\n", omax);

        safe_append(output, "  Free:      ", omax);
        fmt_u64(num, sizeof(num), free_ram / (1024 * 1024));
        safe_append(output, num, omax);
        safe_append(output, " MB\n", omax);

        /* Usage percentage (avoid divide-by-zero) */
        if (total > 0) {
            uint64_t pct = (used * 100) / total;
            safe_append(output, "  Usage:     ", omax);
            fmt_u64(num, sizeof(num), pct);
            safe_append(output, num, omax);
            safe_append(output, "%\n", omax);
        }

        return 0;
    }

    /* ------------------------------------------------------------------ */
    case ACTION_HELP: {
        safe_copy(output,
            "AlJefra OS AI Assistant - Available Commands:\n"
            "\n"
            "Files:\n"
            "  show files / list files  - List all files on disk\n"
            "  open <name>              - Read a file\n"
            "  create <name>            - Create a new file\n"
            "  delete <name>            - Delete a file (needs confirmation)\n"
            "\n"
            "Network:\n"
            "  network status           - Show NIC and connection info\n"
            "  connect                  - Initiate network connection\n"
            "\n"
            "System:\n"
            "  system info              - CPU, RAM, architecture details\n"
            "  uptime                   - How long the system has been running\n"
            "  reboot                   - Restart the system\n"
            "  shutdown                 - Power off the system\n"
            "  update                   - Check for OS/driver updates\n"
            "\n"
            "Drivers:\n"
            "  list drivers             - Show loaded drivers\n"
            "  search driver <name>     - Search marketplace\n"
            "  install driver <name>    - Install from marketplace\n"
            "\n"
            "Display:\n"
            "  display settings         - Show display configuration\n"
            "\n"
            "Other:\n"
            "  memory                   - Show RAM usage\n"
            "  list tasks               - Show running tasks\n"
            "  help                     - This help message\n"
            "\n",
            omax);

        /* Arabic commands summary */
        safe_append(output,
            /* الأوامر بالعربية: */
            "\xd8\xa7\xd9\x84\xd8\xa3\xd9\x88\xd8\xa7\xd9\x85\xd8\xb1"
            " \xd8\xa8\xd8\xa7\xd9\x84\xd8\xb9\xd8\xb1\xd8\xa8\xd9\x8a\xd8\xa9:\n"
            /* اعرض الملفات، افتح، احذف، حالة الشبكة، معلومات النظام، مساعدة */
            "  \xd8\xa7\xd8\xb9\xd8\xb1\xd8\xb6 \xd8\xa7\xd9\x84\xd9\x85\xd9\x84\xd9\x81\xd8\xa7\xd8\xaa"
            ", \xd8\xa7\xd9\x81\xd8\xaa\xd8\xad"
            ", \xd8\xa7\xd8\xad\xd8\xb0\xd9\x81"
            ", \xd8\xad\xd8\xa7\xd9\x84\xd8\xa9 \xd8\xa7\xd9\x84\xd8\xb4\xd8\xa8\xd9\x83\xd8\xa9"
            ", \xd9\x85\xd8\xb9\xd9\x84\xd9\x88\xd9\x85\xd8\xa7\xd8\xaa \xd8\xa7\xd9\x84\xd9\x86\xd8\xb8\xd8\xa7\xd9\x85"
            ", \xd9\x85\xd8\xb3\xd8\xa7\xd8\xb9\xd8\xaf\xd8\xa9"
            "\n",
            omax);

        return 0;
    }

    /* ------------------------------------------------------------------ */
    case ACTION_NONE:
    default:
        safe_copy(output,
            "I did not understand that command. "
            "Type 'help' for a list of available commands.", omax);
        return 0;
    }
}

/* ========================================================================
 * ai_get_context -- Build LLM System Prompt
 *
 * Produces a text string describing the current OS state: architecture,
 * CPU, RAM, uptime, loaded drivers, network status, available actions,
 * and recent conversation history.  This is sent to the LLM as the
 * system prompt so it has full context about the running system.
 * ======================================================================== */

int ai_get_context(char *buf, int size)
{
    if (!buf || size <= 0)
        return -1;

    buf[0] = '\0';
    uint32_t smax = (uint32_t)size;

    safe_copy(buf,
        "You are the AlJefra OS AI assistant, embedded in a bare-metal "
        "operating system.\n"
        "AlJefra OS v1.0 is a universal boot OS that runs on x86-64, "
        "ARM64, and RISC-V.\n\n",
        smax);

    /* Architecture */
    safe_append(buf, "Current architecture: ", smax);
    switch (hal_arch()) {
    case HAL_ARCH_X86_64:  safe_append(buf, "x86-64\n", smax);  break;
    case HAL_ARCH_AARCH64: safe_append(buf, "AArch64\n", smax); break;
    case HAL_ARCH_RISCV64: safe_append(buf, "RISC-V 64\n", smax); break;
    }

    /* CPU info */
    hal_cpu_info_t cpu;
    hal_cpu_get_info(&cpu);
    safe_append(buf, "CPU: ", smax);
    safe_append(buf, cpu.model, smax);
    safe_append(buf, "\n", smax);

    /* RAM */
    char num[21];
    uint64_t total_mb = hal_mmu_total_ram() / (1024 * 1024);
    safe_append(buf, "RAM: ", smax);
    fmt_u64(num, sizeof(num), total_mb);
    safe_append(buf, num, smax);
    safe_append(buf, " MB\n", smax);

    /* Uptime */
    uint64_t secs = hal_timer_ms() / 1000;
    safe_append(buf, "Uptime: ", smax);
    fmt_u64(num, sizeof(num), secs);
    safe_append(buf, num, smax);
    safe_append(buf, " seconds\n", smax);

    /* Loaded drivers */
    const driver_ops_t *drivers[MAX_DRIVERS];
    uint32_t drv_count = driver_list(drivers, MAX_DRIVERS);
    safe_append(buf, "Loaded drivers: ", smax);
    fmt_u64(num, sizeof(num), (uint64_t)drv_count);
    safe_append(buf, num, smax);
    safe_append(buf, "\n", smax);

    /* Network status */
    const driver_ops_t *net = driver_get_network();
    safe_append(buf, "Network: ", smax);
    safe_append(buf, net ? "connected\n" : "offline\n", smax);

    /* Session stats */
    safe_append(buf, "Session commands executed: ", smax);
    fmt_u64(num, sizeof(num), (uint64_t)g_session.commands_executed);
    safe_append(buf, num, smax);
    safe_append(buf, "\n", smax);

    safe_append(buf, "Detected language: ", smax);
    safe_append(buf, g_session.language, smax);
    safe_append(buf, "\n\n", smax);

    /* Available actions for LLM to suggest */
    safe_append(buf,
        "Available actions you can suggest:\n"
        "  fs_list, fs_read, fs_write, fs_create, fs_delete,\n"
        "  net_status, net_connect,\n"
        "  driver_list, driver_search, driver_install,\n"
        "  sys_info, sys_reboot, sys_shutdown, sys_update, sys_uptime,\n"
        "  display_set, task_list, memory_info, help\n"
        "\n"
        "To suggest an action, include 'ACTION: <name>' in your response.\n"
        "To include an argument, add 'ARG: <value>'.\n\n",
        smax);

    /* Recent conversation history (last 6 messages) */
    if (g_session.history_count > 0) {
        safe_append(buf, "Recent conversation:\n", smax);

        uint32_t start = 0;
        if (g_session.history_count > 6)
            start = g_session.history_count - 6;

        for (uint32_t i = start; i < g_session.history_count; i++) {
            const chat_message_t *msg = history_get(i);
            if (!msg)
                continue;

            safe_append(buf,
                msg->is_user ? "  User: " : "  Assistant: ", smax);

            /* Truncate long messages to keep context compact */
            uint32_t mlen = str_len(msg->text);
            if (mlen > 120) {
                char snippet[128];
                safe_copy(snippet, msg->text, 121);
                safe_append(snippet, "...", sizeof(snippet));
                safe_append(buf, snippet, smax);
            } else {
                safe_append(buf, msg->text, smax);
            }
            safe_append(buf, "\n", smax);
        }
    }

    return (int)str_len(buf);
}

/* ========================================================================
 * ai_format_response -- User-Friendly Response Formatting
 *
 * Wraps the raw action result with a language-appropriate prefix.
 * English gets English headers; Arabic gets Arabic headers.
 * ======================================================================== */

int ai_format_response(const ai_action_t *action, const char *raw_result,
                       const char *lang, char *out, int out_size)
{
    if (!action || !raw_result || !out || out_size <= 0)
        return -1;

    out[0] = '\0';
    uint32_t omax = (uint32_t)out_size;
    int is_ar = (lang && lang[0] == 'a' && lang[1] == 'r');

    /* Choose a friendly prefix based on action type and language */
    switch (action->type) {

    case ACTION_FS_LIST:
        if (is_ar)
            safe_copy(out,
                /* ملفاتك هي: */
                "\xd9\x85\xd9\x84\xd9\x81\xd8\xa7\xd8\xaa\xd9\x83 "
                "\xd9\x87\xd9\x8a:\n", omax);
        else
            safe_copy(out, "Here are your files:\n", omax);
        break;

    case ACTION_FS_READ:
        if (is_ar)
            safe_copy(out,
                /* محتوى الملف: */
                "\xd9\x85\xd8\xad\xd8\xaa\xd9\x88\xd9\x89 "
                "\xd8\xa7\xd9\x84\xd9\x85\xd9\x84\xd9\x81:\n", omax);
        else
            safe_copy(out, "File contents:\n", omax);
        break;

    case ACTION_FS_CREATE:
        if (is_ar)
            safe_copy(out,
                /* تم بنجاح: */
                "\xd8\xaa\xd9\x85 \xd8\xa8\xd9\x86\xd8\xac\xd8\xa7\xd8\xad: ",
                omax);
        else
            safe_copy(out, "Success: ", omax);
        break;

    case ACTION_FS_DELETE:
        if (is_ar)
            safe_copy(out,
                /* تم الحذف: */
                "\xd8\xaa\xd9\x85 \xd8\xa7\xd9\x84\xd8\xad\xd8\xb0\xd9\x81: ",
                omax);
        else
            safe_copy(out, "Deleted: ", omax);
        break;

    case ACTION_NET_STATUS:
        if (is_ar)
            safe_copy(out,
                /* حالة الشبكة: */
                "\xd8\xad\xd8\xa7\xd9\x84\xd8\xa9 "
                "\xd8\xa7\xd9\x84\xd8\xb4\xd8\xa8\xd9\x83\xd8\xa9:\n", omax);
        else
            safe_copy(out, "Network status:\n", omax);
        break;

    case ACTION_SYS_INFO:
        if (is_ar)
            safe_copy(out,
                /* معلومات النظام: */
                "\xd9\x85\xd8\xb9\xd9\x84\xd9\x88\xd9\x85\xd8\xa7\xd8\xaa "
                "\xd8\xa7\xd9\x84\xd9\x86\xd8\xb8\xd8\xa7\xd9\x85:\n", omax);
        else
            safe_copy(out, "System information:\n", omax);
        break;

    case ACTION_SYS_REBOOT:
        if (is_ar)
            safe_copy(out,
                /* جاري إعادة التشغيل... */
                "\xd8\xac\xd8\xa7\xd8\xb1\xd9\x8a "
                "\xd8\xa5\xd8\xb9\xd8\xa7\xd8\xaf\xd8\xa9 "
                "\xd8\xa7\xd9\x84\xd8\xaa\xd8\xb4\xd8\xba\xd9\x8a\xd9\x84...\n",
                omax);
        else
            safe_copy(out, "Rebooting...\n", omax);
        break;

    case ACTION_SYS_SHUTDOWN:
        if (is_ar)
            safe_copy(out,
                /* جاري الإغلاق... */
                "\xd8\xac\xd8\xa7\xd8\xb1\xd9\x8a "
                "\xd8\xa7\xd9\x84\xd8\xa5\xd8\xba\xd9\x84\xd8\xa7\xd9\x82...\n",
                omax);
        else
            safe_copy(out, "Shutting down...\n", omax);
        break;

    case ACTION_MEMORY_INFO:
        if (is_ar)
            safe_copy(out,
                /* معلومات الذاكرة: */
                "\xd9\x85\xd8\xb9\xd9\x84\xd9\x88\xd9\x85\xd8\xa7\xd8\xaa "
                "\xd8\xa7\xd9\x84\xd8\xb0\xd8\xa7\xd9\x83\xd8\xb1\xd8\xa9:\n",
                omax);
        else
            safe_copy(out, "Memory information:\n", omax);
        break;

    case ACTION_HELP:
        if (is_ar)
            safe_copy(out,
                /* المساعدة: */
                "\xd8\xa7\xd9\x84\xd9\x85\xd8\xb3\xd8\xa7\xd8\xb9\xd8\xaf\xd8\xa9:\n",
                omax);
        else
            /* Help output already has its own header; no extra prefix */
            out[0] = '\0';
        break;

    case ACTION_DRIVER_LIST:
        if (is_ar)
            safe_copy(out,
                /* السائقين المحملين: */
                "\xd8\xa7\xd9\x84\xd8\xb3\xd8\xa7\xd8\xa6\xd9\x82\xd9\x8a\xd9\x86 "
                "\xd8\xa7\xd9\x84\xd9\x85\xd8\xad\xd9\x85\xd9\x84\xd9\x8a\xd9\x86:\n",
                omax);
        else
            safe_copy(out, "Loaded drivers:\n", omax);
        break;

    case ACTION_SYS_UPTIME:
        if (is_ar)
            safe_copy(out,
                /* وقت التشغيل: */
                "\xd9\x88\xd9\x82\xd8\xaa "
                "\xd8\xa7\xd9\x84\xd8\xaa\xd8\xb4\xd8\xba\xd9\x8a\xd9\x84:\n",
                omax);
        else
            /* Uptime result is self-describing; no extra prefix */
            out[0] = '\0';
        break;

    default:
        /* No special prefix for other action types */
        break;
    }

    /* Append the raw action result */
    safe_append(out, raw_result, omax);

    return (int)str_len(out);
}

/* ========================================================================
 * ai_chat_init -- Initialize the AI Chat Engine
 * ======================================================================== */

int ai_chat_init(void)
{
    memset(&g_session, 0, sizeof(g_session));

    g_session.initialized      = 1;
    g_session.llm_send         = (ai_llm_send_fn)0;
    g_session.history_count    = 0;
    g_session.history_head     = 0;
    g_session.session_start_ms = hal_timer_ms();
    g_session.commands_executed = 0;
    g_session.llm_calls        = 0;
    g_session.username[0]      = '\0';

    /* Default language: English */
    g_session.language[0] = 'e';
    g_session.language[1] = 'n';
    g_session.language[2] = '\0';

    hal_console_puts("[ai_chat] AI chat engine initialized\n");
    return 0;
}

/* ========================================================================
 * ai_chat_set_llm_callback
 * ======================================================================== */

void ai_chat_set_llm_callback(ai_llm_send_fn fn)
{
    g_session.llm_send = fn;

    if (fn)
        hal_console_puts("[ai_chat] LLM callback registered (online mode)\n");
    else
        hal_console_puts("[ai_chat] LLM callback cleared (offline mode)\n");
}

/* ========================================================================
 * ai_chat_get_session
 * ======================================================================== */

const ai_chat_session_t *ai_chat_get_session(void)
{
    return &g_session;
}

/* ========================================================================
 * ai_chat_process -- Main Chat Processing Pipeline
 *
 * Flow:
 *   1. Detect language (English / Arabic)
 *   2. Add user message to history
 *   3. Local NLP parse -- try to match a known command pattern
 *   4. If ACTION_NONE and LLM callback is set, build context and forward
 *      to LLM; parse LLM response for an action suggestion
 *   5. If action needs confirmation, return a confirmation prompt
 *   6. Execute the action via ai_execute_action()
 *   7. Format the result into a user-friendly response
 *   8. Add assistant response to history
 * ======================================================================== */

/* Static intermediate buffers (no heap allocation) */
static char g_raw_result[AI_CHAT_RESPONSE_MAX];
static char g_llm_context[2048];
static char g_llm_response[AI_CHAT_RESPONSE_MAX];

/* Try to parse an action from LLM response text.
 * Looks for "ACTION: <name>" on a line, and "ARG: <value>".
 * Returns 1 if an action was parsed, 0 otherwise. */
static int parse_llm_action(const char *llm_text, ai_action_t *action)
{
    action->type = ACTION_NONE;
    action->args[0] = '\0';
    action->needs_confirm = 0;
    action->confidence = 60; /* LLM-derived actions get moderate confidence */

    /* Look for "ACTION:" or "action:" */
    const char *pos = ci_strstr(llm_text, "action:");
    if (!pos)
        pos = ci_strstr(llm_text, "action :");

    if (!pos)
        return 0;

    /* Skip past "action:" and optional space */
    pos += 7;
    while (*pos == ' ' || *pos == ':')
        pos++;

    /* Extract the action name word */
    char name[32];
    extract_arg(pos, name, sizeof(name));

    action->type = ai_action_from_name(name);
    if (action->type == ACTION_NONE)
        return 0;

    /* Look for "ARG:" or "argument:" */
    const char *arg_pos = ci_strstr(llm_text, "arg:");
    if (arg_pos) {
        arg_pos += 4;
        extract_rest(arg_pos, action->args, sizeof(action->args));
    } else {
        arg_pos = ci_strstr(llm_text, "argument:");
        if (arg_pos) {
            arg_pos += 9;
            extract_rest(arg_pos, action->args, sizeof(action->args));
        }
    }

    /* Flag destructive actions */
    if (action->type == ACTION_FS_DELETE ||
        action->type == ACTION_SYS_REBOOT ||
        action->type == ACTION_SYS_SHUTDOWN) {
        action->needs_confirm = 1;
    }

    return 1;
}

int ai_chat_process(const char *user_input, char *response, int response_size)
{
    if (!user_input || !response || response_size <= 0)
        return -1;

    if (!g_session.initialized) {
        safe_copy(response,
            "Error: AI chat engine not initialized. "
            "Call ai_chat_init() first.",
            (uint32_t)response_size);
        return -1;
    }

    response[0] = '\0';

    /* Skip empty input */
    const char *trimmed = skip_ws(user_input);
    if (trimmed[0] == '\0') {
        safe_copy(response, "Please enter a command or question.",
                  (uint32_t)response_size);
        return (int)str_len(response);
    }

    /* Step 1: Detect language from input */
    detect_language(trimmed);

    /* Step 2: Add user message to history */
    history_add(1, trimmed);

    /* Step 3: Try local NLP parse */
    ai_action_t action;
    int parsed = ai_parse_local(trimmed, &action);

    /* Step 4: If local parse failed and LLM is available, try LLM */
    if (!parsed || action.type == ACTION_NONE) {
        if (g_session.llm_send) {
            /* Build system context */
            ai_get_context(g_llm_context, (int)sizeof(g_llm_context));

            /* Forward to LLM */
            int llm_len = g_session.llm_send(
                g_llm_context, trimmed,
                g_llm_response, (uint32_t)sizeof(g_llm_response));
            g_session.llm_calls++;

            if (llm_len > 0) {
                if ((uint32_t)llm_len < sizeof(g_llm_response))
                    g_llm_response[llm_len] = '\0';
                else
                    g_llm_response[sizeof(g_llm_response) - 1] = '\0';

                /* Try to extract an action from the LLM response */
                ai_action_t llm_action;
                if (parse_llm_action(g_llm_response, &llm_action) &&
                    llm_action.type != ACTION_NONE) {
                    action = llm_action;
                    parsed = 1;
                } else {
                    /* LLM gave a free-text response -- use it directly */
                    safe_copy(response, g_llm_response,
                              (uint32_t)response_size);
                    history_add(0, response);
                    return (int)str_len(response);
                }
            }
            /* If LLM call failed, fall through to ACTION_NONE handling */
        }
    }

    /* Step 5: If the action needs confirmation, return a prompt.
     * Check whether the user's current input is itself a confirmation
     * of a previously prompted action. */
    if (parsed && action.needs_confirm) {
        int is_confirmation =
            (ci_strstr(trimmed, "yes") != (const char *)0) ||
            (ci_strstr(trimmed, "confirm") != (const char *)0) ||
            /* نعم (Arabic "yes") */
            (byte_strstr(trimmed, "\xd9\x86\xd8\xb9\xd9\x85") != (const char *)0);

        if (!is_confirmation) {
            int is_ar = (g_session.language[0] == 'a' &&
                         g_session.language[1] == 'r');

            if (is_ar) {
                safe_copy(response,
                    /* هل أنت متأكد؟ هذا الإجراء لا يمكن التراجع عنه. */
                    "\xd9\x87\xd9\x84 \xd8\xa3\xd9\x86\xd8\xaa "
                    "\xd9\x85\xd8\xaa\xd8\xa3\xd9\x83\xd8\xaf\xd8\x9f "
                    "\xd9\x87\xd8\xb0\xd8\xa7 \xd8\xa7\xd9\x84\xd8\xa5\xd8\xac\xd8\xb1\xd8\xa7\xd8\xa1 "
                    "\xd9\x84\xd8\xa7 \xd9\x8a\xd9\x85\xd9\x83\xd9\x86 "
                    "\xd8\xa7\xd9\x84\xd8\xaa\xd8\xb1\xd8\xa7\xd8\xac\xd8\xb9 "
                    "\xd8\xb9\xd9\x86\xd9\x87.\n",
                    (uint32_t)response_size);
            } else {
                safe_copy(response,
                    "Are you sure? This action cannot be undone.\n",
                    (uint32_t)response_size);
            }

            safe_append(response, "Action: ", (uint32_t)response_size);
            safe_append(response, ai_action_name(action.type),
                        (uint32_t)response_size);
            if (action.args[0]) {
                safe_append(response, " (", (uint32_t)response_size);
                safe_append(response, action.args, (uint32_t)response_size);
                safe_append(response, ")", (uint32_t)response_size);
            }
            safe_append(response, "\n", (uint32_t)response_size);

            if (is_ar)
                /* اكتب "نعم" للتأكيد. */
                safe_append(response,
                    "\xd8\xa7\xd9\x83\xd8\xaa\xd8\xa8 "
                    "\"\xd9\x86\xd8\xb9\xd9\x85\" "
                    "\xd9\x84\xd9\x84\xd8\xaa\xd8\xa3\xd9\x83\xd9\x8a\xd8\xaf.",
                    (uint32_t)response_size);
            else
                safe_append(response, "Type 'yes' to confirm.",
                            (uint32_t)response_size);

            /* Return without executing -- wait for confirmation */
            history_add(0, response);
            return (int)str_len(response);
        }
        /* User confirmed -- fall through to execution */
    }

    /* Step 6: Execute the action */
    g_raw_result[0] = '\0';
    ai_execute_action(&action, g_raw_result, (int)sizeof(g_raw_result));
    g_session.commands_executed++;

    /* Step 7: Format the response with language-appropriate prefix */
    ai_format_response(&action, g_raw_result, g_session.language,
                       response, response_size);

    /* Step 8: Add assistant response to history */
    history_add(0, response);

    return (int)str_len(response);
}
