/*
 * log.c - persistent log file and crash reporting (on by default).
 *
 * Everything that goes through SDL_Log* (game, platform layer, and the Rust
 * renderer via its log callback) is written with a timestamp to
 * doom64rtx.log, next to doom64rtx.ini (the executable's folder for portable
 * installs, otherwise the user's preference folder). The previous run is kept
 * as doom64rtx.old.log. Fatal signals / unhandled exceptions append a crash
 * report with a backtrace before the process exits.
 *
 * Doom64-RTX PC port, GPLv3.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>

#include "log.h"
#include "config.h"

#if defined(_WIN32)
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <signal.h>
#include <unistd.h>
#if defined(__GLIBC__)
#include <execinfo.h>
#define D64_HAVE_BACKTRACE 1
#endif
#endif

#ifndef D64_VERSION
#define D64_VERSION "0.1.0"
#endif
#ifndef D64_GIT_HASH
#define D64_GIT_HASH "unknown"
#endif

static FILE *log_file;
static char log_path[1024];
static Uint64 log_t0;
static SDL_LogOutputFunction prev_output;
static void *prev_userdata;
static char crash_context[512];

static const char *prio_name(SDL_LogPriority p)
{
    switch (p)
    {
    case SDL_LOG_PRIORITY_VERBOSE: return "VERB";
    case SDL_LOG_PRIORITY_DEBUG:   return "DEBUG";
    case SDL_LOG_PRIORITY_INFO:    return "INFO";
    case SDL_LOG_PRIORITY_WARN:    return "WARN";
    case SDL_LOG_PRIORITY_ERROR:   return "ERROR";
    case SDL_LOG_PRIORITY_CRITICAL:return "CRIT";
    default:                       return "LOG";
    }
}

static void SDLCALL log_output(void *userdata, int category, SDL_LogPriority priority, const char *message)
{
    (void)userdata;
    if (log_file)
    {
        double t = (double)(SDL_GetTicksNS() - log_t0) / 1e9;
        fprintf(log_file, "[%9.3f] %-5s %s\n", t, prio_name(priority), message);
        fflush(log_file);
    }
    if (prev_output)
        prev_output(prev_userdata, category, priority, message);
}

void I_PCLog(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    SDL_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    SDL_Log("%s", buf);
}

const char *Log_Path(void)
{
    return log_path;
}

void Log_SetCrashContext(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    SDL_vsnprintf(crash_context, sizeof(crash_context), fmt, ap);
    va_end(ap);
}

static void crash_header(const char *what)
{
    if (!log_file)
        return;
    fprintf(log_file, "\n==================== CRASH ====================\n");
    fprintf(log_file, "%s\n", what);
    fprintf(log_file, "context: %s\n", crash_context[0] ? crash_context : "(none)");
    fprintf(log_file, "version: %s (%s)\n", D64_VERSION, D64_GIT_HASH);
    fflush(log_file);
}

#if defined(_WIN32)
static LONG WINAPI crash_filter(EXCEPTION_POINTERS *ep)
{
    char what[256];
    void *frames[48];
    USHORT n, i;
    HMODULE mod = NULL;

    SDL_snprintf(what, sizeof(what), "Unhandled exception 0x%08lx at %p",
                 (unsigned long)ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    crash_header(what);
    if (log_file)
    {
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)ep->ExceptionRecord->ExceptionAddress, &mod);
        if (mod)
        {
            char name[MAX_PATH];
            GetModuleFileNameA(mod, name, sizeof(name));
            fprintf(log_file, "module: %s +0x%llx\n", name,
                    (unsigned long long)((char *)ep->ExceptionRecord->ExceptionAddress - (char *)mod));
        }
        n = CaptureStackBackTrace(0, 48, frames, NULL);
        fprintf(log_file, "backtrace (exe base %p):\n", (void *)GetModuleHandleA(NULL));
        for (i = 0; i < n; i++)
            fprintf(log_file, "  #%u %p\n", (unsigned)i, frames[i]);
        fprintf(log_file, "===============================================\n");
        fflush(log_file);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
#elif defined(__unix__) || defined(__APPLE__)
static void crash_signal(int sig)
{
    char what[128];
    SDL_snprintf(what, sizeof(what), "Fatal signal %d (%s)", sig,
                 sig == SIGSEGV ? "segmentation fault" : sig == SIGABRT ? "abort" :
                 sig == SIGFPE ? "floating point exception" : sig == SIGILL ? "illegal instruction" :
                 sig == SIGBUS ? "bus error" : "?");
    crash_header(what);
#ifdef D64_HAVE_BACKTRACE
    if (log_file)
    {
        void *frames[64];
        int n = backtrace(frames, 64);
        fprintf(log_file, "backtrace:\n");
        fflush(log_file);
        backtrace_symbols_fd(frames, n, fileno(log_file));
        fprintf(log_file, "===============================================\n");
        fflush(log_file);
    }
#endif
    signal(sig, SIG_DFL);
    raise(sig);
}
#endif

static void install_crash_handlers(void)
{
#if defined(_WIN32)
    SetUnhandledExceptionFilter(crash_filter);
#elif defined(__unix__) || defined(__APPLE__)
    signal(SIGSEGV, crash_signal);
    signal(SIGABRT, crash_signal);
    signal(SIGFPE, crash_signal);
    signal(SIGILL, crash_signal);
    signal(SIGBUS, crash_signal);
#endif
}

void Log_Init(int argc, char **argv)
{
    char dir[1024];
    char old[1100];
    const char *cfg;
    char *slash;
    char *cwd;
    time_t now = time(NULL);
    int i;

    log_t0 = SDL_GetTicksNS();
    install_crash_handlers();

    if (!pc_config.log)
        return;

    cfg = Config_Path();
    SDL_strlcpy(dir, cfg, sizeof(dir));
    slash = SDL_strrchr(dir, '/');
#ifdef _WIN32
    {
        char *bs = SDL_strrchr(dir, '\\');
        if (!slash || (bs && bs > slash))
            slash = bs;
    }
#endif
    if (slash)
        slash[1] = 0;
    else
        dir[0] = 0;

    SDL_snprintf(log_path, sizeof(log_path), "%sdoom64rtx.log", dir);
    SDL_snprintf(old, sizeof(old), "%sdoom64rtx.old.log", dir);
    remove(old);
    rename(log_path, old);
    log_file = fopen(log_path, "w");
    if (!log_file)
    {
        fprintf(stderr, "log: cannot write %s\n", log_path);
        return;
    }

    SDL_GetLogOutputFunction(&prev_output, &prev_userdata);
    SDL_SetLogOutputFunction(log_output, NULL);
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_INFO);

    fprintf(log_file, "Doom64-RTX %s (%s), built %s %s\n", D64_VERSION, D64_GIT_HASH, __DATE__, __TIME__);
    fprintf(log_file, "started %s", ctime(&now));
    fflush(log_file);

    SDL_Log("platform: %s, %d CPUs, %d MB RAM, SDL %d.%d.%d", SDL_GetPlatform(), SDL_GetNumLogicalCPUCores(),
            SDL_GetSystemRAM(), SDL_VERSIONNUM_MAJOR(SDL_GetVersion()), SDL_VERSIONNUM_MINOR(SDL_GetVersion()),
            SDL_VERSIONNUM_MICRO(SDL_GetVersion()));
    SDL_Log("video driver: %s", SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "none");
    cwd = SDL_GetCurrentDirectory();
    SDL_Log("working dir: %s", cwd ? cwd : "?");
    SDL_free(cwd);
    SDL_Log("exe dir: %s", SDL_GetBasePath() ? SDL_GetBasePath() : "?");
    SDL_Log("config: %s", cfg);
    for (i = 1; i < argc; i++)
        SDL_Log("arg %d: %s", i, argv[i]);
    SDL_Log("settings: renderer=%s raytracing=%d widescreen=%d brightness=%d %dx%d fullscreen=%d vsync=%d",
            pc_config.renderer == RENDERER_OPENGL ? "opengl" : "vulkan", pc_config.raytracing,
            pc_config.aspect, pc_config.brightness, pc_config.width, pc_config.height,
            pc_config.fullscreen, pc_config.vsync);
}

void Log_Shutdown(void)
{
    if (log_file)
    {
        SDL_Log("exiting normally");
        SDL_SetLogOutputFunction(prev_output, prev_userdata);
        fclose(log_file);
        log_file = NULL;
    }
}
