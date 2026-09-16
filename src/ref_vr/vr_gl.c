#include <windows.h>
#include <GL/gl.h>
#include <math.h>
#include <string.h>

#include "ref_vr.h"
#include "vr_gl.h"

vr_gl_t vrgl;

typedef void(APIENTRY *gl_frustum_t)(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
	GLdouble znear, GLdouble zfar);
typedef void(APIENTRY *gl_rect_t)(GLint x, GLint y, GLsizei width, GLsizei height);

static gl_frustum_t real_glFrustum;
static gl_rect_t real_glViewport;
static gl_rect_t real_glScissor;

static struct
{
	int active;
	double scale_x, scale_y;
	double tan_left, tan_right, tan_down, tan_up;
} eye;

int vr_gl_load(void)
{
	vrgl.GenFramebuffers = (void *)wglGetProcAddress("glGenFramebuffers");
	vrgl.DeleteFramebuffers = (void *)wglGetProcAddress("glDeleteFramebuffers");
	vrgl.BindFramebuffer = (void *)wglGetProcAddress("glBindFramebuffer");
	vrgl.FramebufferTexture2D = (void *)wglGetProcAddress("glFramebufferTexture2D");
	vrgl.FramebufferRenderbuffer = (void *)wglGetProcAddress("glFramebufferRenderbuffer");
	vrgl.CheckFramebufferStatus = (void *)wglGetProcAddress("glCheckFramebufferStatus");
	vrgl.BlitFramebuffer = (void *)wglGetProcAddress("glBlitFramebuffer");
	vrgl.GenRenderbuffers = (void *)wglGetProcAddress("glGenRenderbuffers");
	vrgl.DeleteRenderbuffers = (void *)wglGetProcAddress("glDeleteRenderbuffers");
	vrgl.BindRenderbuffer = (void *)wglGetProcAddress("glBindRenderbuffer");
	vrgl.RenderbufferStorage = (void *)wglGetProcAddress("glRenderbufferStorage");

	return vrgl.GenFramebuffers && vrgl.DeleteFramebuffers && vrgl.BindFramebuffer &&
		vrgl.FramebufferTexture2D && vrgl.FramebufferRenderbuffer && vrgl.CheckFramebufferStatus &&
		vrgl.BlitFramebuffer && vrgl.GenRenderbuffers && vrgl.DeleteRenderbuffers &&
		vrgl.BindRenderbuffer && vrgl.RenderbufferStorage;
}

/* ------------------------------------------------------------------------- */
/* ref_gl's GL calls                                                          */
/* ------------------------------------------------------------------------- */

static void scale_rect(GLint *x, GLint *y, GLsizei *width, GLsizei *height)
{
	GLint x0 = (GLint)floor(*x * eye.scale_x + 0.5);
	GLint y0 = (GLint)floor(*y * eye.scale_y + 0.5);
	GLint x1 = (GLint)floor((*x + *width) * eye.scale_x + 0.5);
	GLint y1 = (GLint)floor((*y + *height) * eye.scale_y + 0.5);

	*x = x0;
	*y = y0;
	*width = x1 - x0;
	*height = y1 - y0;
}

static void APIENTRY hook_glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
	if (eye.active)
	{
		scale_rect(&x, &y, &width, &height);
	}

	real_glViewport(x, y, width, height);
}

static void APIENTRY hook_glScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
	if (eye.active)
	{
		scale_rect(&x, &y, &width, &height);
	}

	real_glScissor(x, y, width, height);
}

/* ref_gl sets a projection in three places, the view weapon included; all of
   them get the eye's frustum, at the depth range ref_gl asked for. */
static void APIENTRY hook_glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
	GLdouble znear, GLdouble zfar)
{
	if (eye.active)
	{
		left = znear * eye.tan_left;
		right = znear * eye.tan_right;
		bottom = znear * eye.tan_down;
		top = znear * eye.tan_up;
	}

	real_glFrustum(left, right, bottom, top, znear, zfar);
}

static FARPROC WINAPI hook_GetProcAddress(HMODULE module, LPCSTR name)
{
	FARPROC proc = GetProcAddress(module, name);

	/* Lookups by ordinal pass straight through. */
	if (!proc || !HIWORD((DWORD_PTR)name))
	{
		return proc;
	}

	if (strcmp(name, "glFrustum") == 0)
	{
		real_glFrustum = (gl_frustum_t)proc;
		return (FARPROC)hook_glFrustum;
	}
	if (strcmp(name, "glViewport") == 0)
	{
		real_glViewport = (gl_rect_t)proc;
		return (FARPROC)hook_glViewport;
	}
	if (strcmp(name, "glScissor") == 0)
	{
		real_glScissor = (gl_rect_t)proc;
		return (FARPROC)hook_glScissor;
	}

	return proc;
}

/* Points one of a module's imports at replacement. Returns 0 if it has no such import. */
static int patch_import(HMODULE module, const char *dll, const char *function, void *replacement)
{
	BYTE *base = (BYTE *)module;
	const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
	const IMAGE_DATA_DIRECTORY *directory = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	const IMAGE_IMPORT_DESCRIPTOR *import;

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

		names = (const IMAGE_THUNK_DATA *)(base + (import->OriginalFirstThunk ? import->OriginalFirstThunk : import->FirstThunk));
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

int vr_gl_hook_refgl(HMODULE refgl)
{
	if (!patch_import(refgl, "KERNEL32.dll", "GetProcAddress", (void *)hook_GetProcAddress))
	{
		vr_printf("could not hook ref_gl.dll's GL lookups - stereo will not work");
		return 0;
	}

	return 1;
}

void vr_gl_eye_begin(int target_width, int target_height, int window_width, int window_height,
	float tan_left, float tan_right, float tan_down, float tan_up)
{
	eye.scale_x = (double)target_width / window_width;
	eye.scale_y = (double)target_height / window_height;
	eye.tan_left = tan_left;
	eye.tan_right = tan_right;
	eye.tan_down = tan_down;
	eye.tan_up = tan_up;
	eye.active = real_glFrustum && real_glViewport && real_glScissor;
}

void vr_gl_eye_end(void)
{
	eye.active = 0;
}
