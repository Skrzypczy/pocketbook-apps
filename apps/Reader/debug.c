#include "debug.h"
#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

reader_debug g_debug;

#ifndef NASKILLA_GIT_SHA
#define NASKILLA_GIT_SHA "unknown"
#endif
#ifndef NASKILLA_FT_VER
#define NASKILLA_FT_VER "unknown"
#endif

static int g_log_fd = -1;
static char g_alt_stack[65536];
static int g_sentinel_at_boot;
static volatile sig_atomic_t g_in_signal;

static void ensure_state_dirs(void)
{
    mkdir("/mnt/ext1/system", 0755);
    mkdir("/mnt/ext1/system/state", 0755);
    mkdir(CFG_STATE_DIR, 0755);
}

void boot_marker(const char *msg)
{
    const char *paths[] = {
        CFG_BOOT_LOG,
        CFG_LOG_PATH,
        "/mnt/ext1/system/state/naskilla_boot.log",
        "/tmp/naskilla_boot.log",
        NULL
    };
    struct stat st;
    int i;

    ensure_state_dirs();
    if (stat(CFG_BOOT_LOG, &st) == 0 && st.st_size > CFG_LOG_MAX)
        unlink(CFG_BOOT_LOG);

    for (i = 0; paths[i]; i++) {
        FILE *fp = fopen(paths[i], "a");
        if (fp) {
            fputs(msg ? msg : "", fp);
            fputc('\n', fp);
            fflush(fp);
            fclose(fp);
        }
    }
}

static void apply_debug_kv(const char *key, const char *val)
{
    if (strcmp(key, "render") == 0) {
        if (strcmp(val, "auto") == 0)
            g_debug.render = RENDER_AUTO;
        else if (strcmp(val, "pattern") == 0)
            g_debug.render = RENDER_PATTERN;
        else
            g_debug.render = RENDER_TEXT;
    } else if (strcmp(key, "draw_probe") == 0) {
        g_debug.draw_probe = atoi(val);
        if (g_debug.draw_probe < 0)
            g_debug.draw_probe = 0;
        if (g_debug.draw_probe > 6)
            g_debug.draw_probe = 6;
    } else if (strcmp(key, "crash_test") == 0) {
        g_debug.crash_test = atoi(val) ? 1 : 0;
    }
}

static void load_debug_cfg(void)
{
    FILE *fp;
    char line[256];

    g_debug.render = RENDER_AUTO;
    g_debug.draw_probe = 6;
    g_debug.crash_test = 0;
    g_debug.force_text = 0;

    fp = fopen(CFG_DEBUG_PATH, "r");
    if (!fp)
        return;
    while (fgets(line, sizeof(line), fp)) {
        char *eq;
        char *key;
        char *val;
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;
        eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        key = line;
        val = eq + 1;
        while (*key == ' ' || *key == '\t')
            key++;
        {
            char *ke = key + strlen(key);
            while (ke > key && (ke[-1] == ' ' || ke[-1] == '\t'))
                *--ke = '\0';
        }
        {
            char *ve = val + strlen(val);
            while (ve > val && (ve[-1] == '\n' || ve[-1] == '\r' ||
                                ve[-1] == ' ' || ve[-1] == '\t'))
                *--ve = '\0';
            while (*val == ' ' || *val == '\t')
                val++;
        }
        apply_debug_kv(key, val);
    }
    fclose(fp);
}

int debug_parse_line_for_test(const char *line_in, reader_debug *out)
{
    char line[256];
    char *eq;
    char *key;
    char *val;
    reader_debug saved = g_debug;

    if (!line_in || !out)
        return -1;
    snprintf(line, sizeof(line), "%s", line_in);
    eq = strchr(line, '=');
    if (!eq)
        return -1;
    *eq = '\0';
    key = line;
    val = eq + 1;
    while (*key == ' ' || *key == '\t')
        key++;
    {
        char *ke = key + strlen(key);
        while (ke > key && (ke[-1] == ' ' || ke[-1] == '\t'))
            *--ke = '\0';
    }
    {
        char *ve = val + strlen(val);
        while (ve > val && (ve[-1] == '\n' || ve[-1] == '\r' ||
                            ve[-1] == ' ' || ve[-1] == '\t'))
            *--ve = '\0';
        while (*val == ' ' || *val == '\t')
            val++;
    }
    g_debug = *out;
    apply_debug_kv(key, val);
    *out = g_debug;
    g_debug = saved;
    return 0;
}

void debug_log_build_id(void)
{
    reader_log("build id sha=%s built=%s %s freetype=%s",
               NASKILLA_GIT_SHA, __DATE__, __TIME__, NASKILLA_FT_VER);
    boot_marker("naskilla: build id logged");
}

void debug_log_mem(const char *tag)
{
    FILE *fp;
    char line[256];
    long vmrss = -1, vmpeak = -1, memfree = -1, memavail = -1;

    fp = fopen("/proc/self/status", "r");
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "VmRSS:", 6) == 0)
                sscanf(line + 6, "%ld", &vmrss);
            else if (strncmp(line, "VmPeak:", 7) == 0)
                sscanf(line + 7, "%ld", &vmpeak);
        }
        fclose(fp);
    }
    fp = fopen("/proc/meminfo", "r");
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "MemFree:", 8) == 0)
                sscanf(line + 8, "%ld", &memfree);
            else if (strncmp(line, "MemAvailable:", 13) == 0)
                sscanf(line + 13, "%ld", &memavail);
        }
        fclose(fp);
    }
    reader_log("mem %s VmRSS=%ldkB VmPeak=%ldkB MemFree=%ldkB MemAvail=%ldkB",
               tag ? tag : "?", vmrss, vmpeak, memfree, memavail);
}

static void open_log_fd(void)
{
    ensure_state_dirs();
    if (g_log_fd >= 0)
        return;
    g_log_fd = open(CFG_LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
}

static void signal_write(const char *s)
{
    size_t n;
    if (!s || g_log_fd < 0)
        return;
    n = strlen(s);
    (void)write(g_log_fd, s, n);
}

static void signal_write_hex(unsigned long v)
{
    char buf[20];
    int i = 18;
    buf[19] = '\0';
    if (v == 0) {
        signal_write("0");
        return;
    }
    while (v && i >= 0) {
        int d = (int)(v & 0xF);
        buf[i--] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        v >>= 4;
    }
    signal_write(buf + i + 1);
}

static void on_fatal_signal(int sig, siginfo_t *info, void *ucontext)
{
    ucontext_t *uc = (ucontext_t *)ucontext;
    unsigned long pc = 0, lr = 0, sp = 0;

    if (g_in_signal) {
        _exit(128 + sig);
    }
    g_in_signal = 1;

    if (uc) {
#if defined(__arm__)
        pc = (unsigned long)uc->uc_mcontext.arm_pc;
        lr = (unsigned long)uc->uc_mcontext.arm_lr;
        sp = (unsigned long)uc->uc_mcontext.arm_sp;
#endif
    }

    signal_write("FATAL signal=");
    signal_write_hex((unsigned long)sig);
    signal_write(" si_addr=");
    signal_write_hex(info ? (unsigned long)info->si_addr : 0);
    signal_write(" pc=");
    signal_write_hex(pc);
    signal_write(" lr=");
    signal_write_hex(lr);
    signal_write(" sp=");
    signal_write_hex(sp);
    signal_write("\n");
    if (g_log_fd >= 0)
        (void)fsync(g_log_fd);
    _exit(128 + sig);
}

void debug_install_handlers(void)
{
    stack_t ss;
    struct sigaction sa;
    int sigs[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
    int i;

    open_log_fd();

    memset(&ss, 0, sizeof(ss));
    ss.ss_sp = g_alt_stack;
    ss.ss_size = sizeof(g_alt_stack);
    ss.ss_flags = 0;
    if (sigaltstack(&ss, NULL) != 0)
        reader_log("sigaltstack failed errno=%d", errno);

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = on_fatal_signal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (i = 0; i < (int)(sizeof(sigs) / sizeof(sigs[0])); i++) {
        if (sigaction(sigs[i], &sa, NULL) != 0)
            reader_log("sigaction %d failed errno=%d", sigs[i], errno);
    }
    reader_log("signal handlers installed main=%p", (void *)(uintptr_t)debug_install_handlers);
}

void debug_sentinel_begin(const char *path)
{
    FILE *fp;
    ensure_state_dirs();
    fp = fopen(CFG_SENTINEL_PATH, "w");
    if (!fp)
        return;
    fprintf(fp, "sha=%s\n", NASKILLA_GIT_SHA);
    fprintf(fp, "path=%s\n", path ? path : "");
    fprintf(fp, "time=%ld\n", (long)time(NULL));
    fflush(fp);
    fsync(fileno(fp));
    fclose(fp);
}

void debug_sentinel_clear(void)
{
    unlink(CFG_SENTINEL_PATH);
}

int debug_sentinel_was_set(void)
{
    return g_sentinel_at_boot;
}

void debug_init(void)
{
    ensure_state_dirs();
    open_log_fd();
    load_debug_cfg();

    if (access(CFG_SENTINEL_PATH, F_OK) == 0) {
        FILE *fp;
        g_sentinel_at_boot = 1;
        g_debug.force_text = 1;
        g_debug.render = RENDER_TEXT;
        reader_log("sentinel present — forcing render=text (sticky)");
        boot_marker("naskilla: sentinel recovered -> text");
        unlink(CFG_SENTINEL_PATH);
        /* Persist so the next cold start does not crash-loop on Draw. */
        fp = fopen(CFG_DRAW_SAFE_PATH, "w");
        if (fp) {
            fputs("draw crashed; using text until this file is deleted\n", fp);
            fclose(fp);
        }
    } else if (access(CFG_DRAW_SAFE_PATH, F_OK) == 0) {
        g_debug.force_text = 1;
        g_debug.render = RENDER_TEXT;
        reader_log("draw_safe_text present — keeping render=text");
        boot_marker("naskilla: draw_safe_text -> text");
    }

    reader_log("debug.cfg render=%d probe=%d crash_test=%d force_text=%d",
               (int)g_debug.render, g_debug.draw_probe, g_debug.crash_test,
               g_debug.force_text);
}

void debug_clear_draw_safe(void)
{
    unlink(CFG_DRAW_SAFE_PATH);
}
