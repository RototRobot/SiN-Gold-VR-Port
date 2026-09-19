#include <string.h>

#include "vr_hook.h"

int vr_hook_import(HMODULE module, const char *dll, const char *function, void *replacement, void **previous)
{
	BYTE *base = (BYTE *)module;
	const IMAGE_NT_HEADERS *nt;
	const IMAGE_DATA_DIRECTORY *directory;
	const IMAGE_IMPORT_DESCRIPTOR *import;

	if (!base)
	{
		return 0;
	}

	nt = (const IMAGE_NT_HEADERS *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
	directory = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (!directory->VirtualAddress)
	{
		return 0;
	}

	for (import = (const IMAGE_IMPORT_DESCRIPTOR *)(base + directory->VirtualAddress); import->Name; ++import)
	{
		const IMAGE_THUNK_DATA *names;
		IMAGE_THUNK_DATA *slots;

		if (_stricmp((const char *)(base + import->Name), dll) != 0)
		{
			continue;
		}

		/* The names table keeps the original names even after the loader has
		   written addresses into the slots. */
		names = (const IMAGE_THUNK_DATA *)(base +
			(import->OriginalFirstThunk ? import->OriginalFirstThunk : import->FirstThunk));
		slots = (IMAGE_THUNK_DATA *)(base + import->FirstThunk);

		for (; names->u1.AddressOfData; ++names, ++slots)
		{
			const IMAGE_IMPORT_BY_NAME *by_name;
			DWORD protection;

			if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
			{
				continue;
			}

			by_name = (const IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
			if (strcmp((const char *)by_name->Name, function) != 0)
			{
				continue;
			}

			if (previous)
			{
				*previous = (void *)(DWORD_PTR)slots->u1.Function;
			}

			if (!VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function), PAGE_READWRITE, &protection))
			{
				return 0;
			}
			slots->u1.Function = (DWORD)(DWORD_PTR)replacement;
			VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function), protection, &protection);
			return 1;
		}
	}

	return 0;
}

/* What vr_hook_call changed, so it can be put back. */
static struct
{
	BYTE *site;
	LONG displacement;
} g_calls[8];
static int g_call_count;

static int write_displacement(BYTE *site, LONG displacement)
{
	DWORD protection;

	if (!VirtualProtect(site + 1, sizeof(LONG), PAGE_EXECUTE_READWRITE, &protection))
	{
		return 0;
	}
	memcpy(site + 1, &displacement, sizeof(LONG));
	VirtualProtect(site + 1, sizeof(LONG), protection, &protection);
	FlushInstructionCache(GetCurrentProcess(), site, 5);
	return 1;
}

int vr_hook_call(HMODULE module, DWORD site_rva, DWORD target_rva, void *replacement, void **original)
{
	BYTE *base = (BYTE *)module;
	const IMAGE_NT_HEADERS *nt;
	BYTE *site;
	LONG displacement;

	if (!base || g_call_count >= (int)(sizeof(g_calls) / sizeof(g_calls[0])))
	{
		return 0;
	}

	nt = (const IMAGE_NT_HEADERS *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE || site_rva + 5 > nt->OptionalHeader.SizeOfImage ||
		target_rva >= nt->OptionalHeader.SizeOfImage)
	{
		return 0;
	}

	site = base + site_rva;
	memcpy(&displacement, site + 1, sizeof(LONG));
	if (site[0] != 0xE8 || site + 5 + displacement != base + target_rva)
	{
		return 0;
	}

	if (!write_displacement(site, (LONG)((BYTE *)replacement - (site + 5))))
	{
		return 0;
	}

	g_calls[g_call_count].site = site;
	g_calls[g_call_count].displacement = displacement;
	g_call_count++;

	if (original)
	{
		*original = base + target_rva;
	}
	return 1;
}

void vr_unhook_calls(void)
{
	while (g_call_count > 0)
	{
		g_call_count--;
		write_displacement(g_calls[g_call_count].site, g_calls[g_call_count].displacement);
	}
}
