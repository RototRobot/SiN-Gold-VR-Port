#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "vrlog.h"

static FILE *g_log;
static DWORD g_start;

void vrlog_open(void *module)
{
	char path[MAX_PATH];
	char *slash;
	const char *mode = "a";

	if (g_log)
	{
		return;
	}

	if (!GetModuleFileNameA((HMODULE)module, path, sizeof(path) - 16))
	{
		return;
	}

	slash = strrchr(path, '\\');
	strcpy(slash ? slash + 1 : path, "sinvr.log");

	/* An environment variable outlives this DLL being unloaded and reloaded
	   within the process, so it tells a fresh start from a vid_restart. */
	if (GetEnvironmentVariableA("SINVR_LOG_STARTED", NULL, 0) == 0)
	{
		mode = "w";
		SetEnvironmentVariableA("SINVR_LOG_STARTED", "1");
	}

	g_log = fopen(path, mode);
	g_start = GetTickCount();
}

void vrlog_close(void)
{
	if (g_log)
	{
		fclose(g_log);
		g_log = NULL;
	}
}

void vrlog(const char *fmt, ...)
{
	va_list args;
	DWORD ms;

	if (!g_log)
	{
		return;
	}

	ms = GetTickCount() - g_start;
	fprintf(g_log, "[%5lu.%03lu] ", ms / 1000, ms % 1000);

	va_start(args, fmt);
	vfprintf(g_log, fmt, args);
	va_end(args);

	fputc('\n', g_log);
	fflush(g_log);
}
