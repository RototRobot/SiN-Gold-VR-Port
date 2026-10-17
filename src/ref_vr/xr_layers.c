#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xr_layers.h"

#pragma comment(lib, "advapi32.lib")

static char *read_file(const char *path)
{
	FILE *f = fopen(path, "rb");
	char *buffer = NULL;
	long size;

	if (!f)
	{
		return NULL;
	}

	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (size > 0 && size < (1 << 20))
	{
		buffer = malloc((size_t)size + 1);
		if (buffer && fread(buffer, 1, (size_t)size, f) == (size_t)size)
		{
			buffer[size] = 0;
		}
		else
		{
			free(buffer);
			buffer = NULL;
		}
	}

	fclose(f);
	return buffer;
}

/* Finds "key": "value" in a small JSON manifest. Handles the escapes a path can
   contain (\\ \/ \"), which is all a layer manifest needs. */
static int json_string(const char *json, const char *key, char *out, size_t out_size)
{
	char pattern[128];
	const char *p;
	size_t n = 0;

	snprintf(pattern, sizeof(pattern), "\"%s\"", key);
	p = strstr(json, pattern);
	if (!p)
	{
		return 0;
	}

	p = strchr(p + strlen(pattern), ':');
	p = p ? strchr(p, '"') : NULL;
	if (!p)
	{
		return 0;
	}

	for (++p; *p && *p != '"' && n + 1 < out_size; ++p)
	{
		if (*p == '\\' && p[1])
		{
			++p;
		}
		out[n++] = *p;
	}

	out[n] = 0;
	return *p == '"';
}

/* IMAGE_FILE_MACHINE_* of a PE file, or 0 if it cannot be read as one. */
static unsigned pe_machine(const char *path)
{
	unsigned char header[4096];
	FILE *f = fopen(path, "rb");
	size_t got;
	long pe;

	if (!f)
	{
		return 0;
	}

	got = fread(header, 1, sizeof(header), f);
	fclose(f);

	if (got < 0x40 || header[0] != 'M' || header[1] != 'Z')
	{
		return 0;
	}

	memcpy(&pe, header + 0x3C, sizeof(pe));
	if (pe < 0 || (size_t)pe + 6 > got || memcmp(header + pe, "PE\0\0", 4) != 0)
	{
		return 0;
	}

	return header[pe + 4] | (header[pe + 5] << 8);
}

/* The machine type of a manifest's runtime library, or 0 if it cannot be read. */
static unsigned manifest_machine(const char *manifest)
{
	char library[MAX_PATH], full[2 * MAX_PATH];
	const char *slash;
	char *json = read_file(manifest);
	unsigned machine = 0;

	if (json && json_string(json, "library_path", library, sizeof(library)))
	{
		if ((library[0] && library[1] == ':') || (library[0] == '\\' && library[1] == '\\'))
		{
			snprintf(full, sizeof(full), "%s", library);
		}
		else
		{
			slash = strrchr(manifest, '\\');
			if (!slash)
			{
				slash = strrchr(manifest, '/');
			}
			snprintf(full, sizeof(full), "%.*s%s", slash ? (int)(slash - manifest) + 1 : 0, manifest, library);
		}
		machine = pe_machine(full);
	}

	free(json);
	return machine;
}

int xr_fix_runtime_override(xr_layer_report_t report)
{
	char manifest[MAX_PATH], sibling[MAX_PATH];
	char *at;
	DWORD length = GetEnvironmentVariableA("XR_RUNTIME_JSON", manifest, sizeof(manifest));

	if (length == 0 || length >= sizeof(manifest) || manifest_machine(manifest) == IMAGE_FILE_MACHINE_I386)
	{
		return 0;
	}

	/* its 32-bit sibling, if it has one by the usual name */
	strcpy(sibling, manifest);
	at = strstr(sibling, "win64");
	if (!at)
	{
		at = strstr(sibling, "Win64");
	}
	if (at)
	{
		memcpy(at + 3, "32", 2);
		if (manifest_machine(sibling) == IMAGE_FILE_MACHINE_I386)
		{
			SetEnvironmentVariableA("XR_RUNTIME_JSON", sibling);
			if (report)
			{
				report(manifest, "XR_RUNTIME_JSON names a runtime that is not 32-bit - using its 32-bit one");
			}
			return 1;
		}
	}

	SetEnvironmentVariableA("XR_RUNTIME_JSON", NULL);
	if (report)
	{
		report(manifest, "XR_RUNTIME_JSON names a runtime that is not 32-bit - using the registry's instead");
	}
	return 1;
}

int xr_disable_unloadable_implicit_layers(xr_layer_report_t report)
{
	static const HKEY roots[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };
	int disabled = 0;
	int r;

	for (r = 0; r < 2; ++r)
	{
		HKEY key;
		DWORD index;

		/* In a 32-bit process this opens the WOW6432Node view, which is the one
		   the 32-bit loader reads. */
		if (RegOpenKeyExA(roots[r], "SOFTWARE\\Khronos\\OpenXR\\1\\ApiLayers\\Implicit",
				0, KEY_READ, &key) != ERROR_SUCCESS)
		{
			continue;
		}

		for (index = 0;; ++index)
		{
			char manifest[MAX_PATH], library[MAX_PATH], full[2 * MAX_PATH], disable_env[256];
			DWORD manifest_len = sizeof(manifest), type = 0, value = 0, value_len = sizeof(value);
			const char *slash;
			char *json;
			LONG e;
			int has_separator, exists;
			unsigned machine;

			e = RegEnumValueA(key, index, manifest, &manifest_len, NULL, &type, (BYTE *)&value, &value_len);
			if (e == ERROR_NO_MORE_ITEMS)
			{
				break;
			}

			/* A non-zero value means the layer is already disabled. */
			if (e != ERROR_SUCCESS || type != REG_DWORD || value != 0)
			{
				continue;
			}

			json = read_file(manifest);
			if (!json)
			{
				continue;
			}

			if (json_string(json, "library_path", library, sizeof(library)) &&
				json_string(json, "disable_environment", disable_env, sizeof(disable_env)))
			{
				/* Relative paths are relative to the manifest's folder. A bare file
				   name is left to the library search path, so it is not judged here. */
				has_separator = strchr(library, '\\') != NULL || strchr(library, '/') != NULL;
				if ((library[0] && library[1] == ':') || (library[0] == '\\' && library[1] == '\\'))
				{
					snprintf(full, sizeof(full), "%s", library);
				}
				else
				{
					slash = strrchr(manifest, '\\');
					snprintf(full, sizeof(full), "%.*s%s", slash ? (int)(slash - manifest) + 1 : 0,
						manifest, library);
				}

				exists = GetFileAttributesA(full) != INVALID_FILE_ATTRIBUTES;
				machine = exists ? pe_machine(full) : 0;

				if ((exists && machine != IMAGE_FILE_MACHINE_I386) || (!exists && has_separator))
				{
					SetEnvironmentVariableA(disable_env, "1");
					disabled++;
					if (report)
					{
						report(full, exists ? "library is not 32-bit" : "library missing");
					}
				}
			}

			free(json);
		}

		RegCloseKey(key);
	}

	return disabled;
}
