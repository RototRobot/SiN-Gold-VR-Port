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
