/*
 * log.h - doom64rtx.log and crash reports. Doom64-RTX PC port, GPLv3.
 */
#ifndef D64_LOG_H
#define D64_LOG_H

void Log_Init(int argc, char **argv);   /* after Config_Load/ParseArgs */
void Log_Shutdown(void);
const char *Log_Path(void);
/* Short description of what the game is doing, included in crash reports. */
void Log_SetCrashContext(const char *fmt, ...);

#endif
