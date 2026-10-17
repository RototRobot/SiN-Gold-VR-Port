/*
 * OpenXR for ref_vr. Brings a session up on ref_gl's own GL context and shows
 * each engine frame one of two ways (see vr_xr.h):
 *
 *   - stereo: the engine's two BeginFrame passes render the eyes into per-eye
 *     swapchains, from eye poses located at the first pass, and the frame is
 *     submitted as a projection layer carrying those same poses;
 *   - theatre: the finished window image goes on a quad layer, a virtual screen.
 *
 * Runs on the engine's main thread with ref_gl's context current.
 * docs/OPENXR_NOTES.md has the runtime facts this relies on.
 */
#include <windows.h>
#include <unknwn.h> /* openxr_platform.h's Win32 interop structs use IUnknown */
#include <GL/gl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "ref_vr.h"
#include "vr_gl.h"
#include "vr_input.h"
#include "vr_poses.h"
#include "vr_room.h"
#include "vr_weapon.h"
#include "vr_xr.h"
#include "vrlog.h"
#include "vr_gesture.h"
#include "xr_layers.h"

typedef BOOL(WINAPI *swap_interval_t)(int interval);

/* A swapchain and the framebuffer that draws into its images. */
typedef struct
{
	XrSwapchain swapchain;
	int width, height;
	uint32_t image_count;
	XrSwapchainImageOpenGLKHR *images;
	GLuint framebuffer;
	GLuint depth; /* a swapchain gives colour only, so depth is ours */
	int checked;  /* framebuffer completeness is reported once */
	int acquired;
	uint32_t index;
} xr_target_t;

/* GL state a blit must not be affected by, and must leave as it found it. */
typedef struct
{
	GLint read_framebuffer, draw_framebuffer;
	GLboolean scissor, srgb;
} blit_state_t;

static struct
{
	int started; /* a session exists */
	int failed;  /* could not start, or the runtime ended it: stay down until vr_xr_shutdown() */
	int lost;    /* the runtime wants the session gone */

	XrInstance instance;
	XrSystemId system;
	XrSession session;
	XrSessionState state;
	int running;
	XrSpace space;
	XrSpace view_space; /* the head, for placing the screen in front of it */
	int64_t format;

	HDC dc;
	HGLRC context;

	/* Theatre screen. */
	xr_target_t screen;
	unsigned frames_shown;
	float screen_distance, screen_width; /* as last shown, for the idle frames */
	int last_was_theatre;                /* the last frame shown was the screen */
	int screen_placed;                   /* screen_head/screen_yaw hold where the head was */
	float screen_head[3];
	float screen_yaw;                    /* about +y, radians; 0 faces -z */
	unsigned idle_frames;

	/* Stereo. */
	int stereo_failed;
	xr_target_t eye[2];
	float eye_scale;  /* the resolution the eye buffers were made for, as asked */
	float eye_made;   /* the last one the runtime actually made; 0 before any */
	int in_frame;      /* pass 0 began an OpenXR frame that vr_xr_end_stereo_frame() ends */
	int should_render;
	XrTime display_time;
	XrView views[2];
	vr_eye_t eye_views[2];
	int bound_eye;     /* 1 + the eye whose target is bound, or 0 */
	int eyes_rendered; /* a bit per eye */
	int window_width, window_height;
	int warned_incomplete;
	int warned_idle;   /* said once that the headset does not want frames */
	int recenter_wanted;
	int dump_wanted;
	unsigned stereo_frames;
} xr;

static int xr_check(XrResult result, const char *call)
{
	char text[XR_MAX_RESULT_STRING_SIZE];

	if (XR_SUCCEEDED(result))
	{
		return 1;
	}

	if (xr.instance == XR_NULL_HANDLE || XR_FAILED(xrResultToString(xr.instance, result, text)))
	{
		snprintf(text, sizeof(text), "%d", (int)result);
	}

	vr_printf("VR: %s failed: %s", call, text);
	return 0;
}

static const char *state_name(XrSessionState state)
{
	static const char *const names[] = {
		"UNKNOWN", "IDLE", "READY", "SYNCHRONIZED", "VISIBLE", "FOCUSED", "STOPPING", "LOSS_PENDING", "EXITING"
	};

	return (state >= 0 && state <= XR_SESSION_STATE_EXITING) ? names[state] : "?";
}

static void report_layer(const char *library, const char *reason)
{
	vr_printf("VR: switched off an OpenXR layer a 32-bit game cannot load (%s): %s", reason, library);
}

static void report_runtime(const char *manifest, const char *what)
{
	vr_printf("VR: %s (it was %s)", what, manifest);
}

/* ------------------------------------------------------------------------- */
/* Render targets                                                             */
/* ------------------------------------------------------------------------- */

static void blit_state_begin(blit_state_t *state)
{
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &state->read_framebuffer);
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &state->draw_framebuffer);
	state->scissor = glIsEnabled(GL_SCISSOR_TEST);
	state->srgb = glIsEnabled(GL_FRAMEBUFFER_SRGB);

	/* A blit is clipped by the scissor test, and must copy colour unconverted. */
	if (state->scissor)
	{
		glDisable(GL_SCISSOR_TEST);
	}
	if (state->srgb)
	{
		glDisable(GL_FRAMEBUFFER_SRGB);
	}
}

static void blit_state_end(const blit_state_t *state)
{
	vrgl.BindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)state->read_framebuffer);
	vrgl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)state->draw_framebuffer);
	if (state->scissor)
	{
		glEnable(GL_SCISSOR_TEST);
	}
	if (state->srgb)
	{
		glEnable(GL_FRAMEBUFFER_SRGB);
	}
}

static void xr_destroy_target(xr_target_t *t)
{
	if (t->swapchain != XR_NULL_HANDLE)
	{
		xrDestroySwapchain(t->swapchain);
	}
	free(t->images);
	if (t->framebuffer && vrgl.DeleteFramebuffers)
	{
		vrgl.DeleteFramebuffers(1, &t->framebuffer);
	}
	if (t->depth && vrgl.DeleteRenderbuffers)
	{
		vrgl.DeleteRenderbuffers(1, &t->depth);
	}
	memset(t, 0, sizeof(*t));
}

static int xr_create_target(xr_target_t *t, int width, int height, int with_depth)
{
	XrSwapchainCreateInfo info = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
	uint32_t i;

	info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
	info.format = xr.format;
	info.sampleCount = 1;
	info.width = (uint32_t)width;
	info.height = (uint32_t)height;
	info.faceCount = 1;
	info.arraySize = 1;
	info.mipCount = 1;

	if (!xr_check(xrCreateSwapchain(xr.session, &info, &t->swapchain), "xrCreateSwapchain") ||
		!xr_check(xrEnumerateSwapchainImages(t->swapchain, 0, &t->image_count, NULL), "xrEnumerateSwapchainImages"))
	{
		return 0;
	}

	t->images = calloc(t->image_count, sizeof(*t->images));
	if (!t->images)
	{
		return 0;
	}
	for (i = 0; i < t->image_count; ++i)
	{
		t->images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
	}
	if (!xr_check(xrEnumerateSwapchainImages(t->swapchain, t->image_count, &t->image_count,
			(XrSwapchainImageBaseHeader *)t->images), "xrEnumerateSwapchainImages"))
	{
		return 0;
	}

	vrgl.GenFramebuffers(1, &t->framebuffer);

	if (with_depth)
	{
		vrgl.GenRenderbuffers(1, &t->depth);
		vrgl.BindRenderbuffer(GL_RENDERBUFFER, t->depth);
		vrgl.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
		vrgl.BindRenderbuffer(GL_RENDERBUFFER, 0);
	}

	t->width = width;
	t->height = height;
	return 1;
}

/* Binds the target's framebuffer on its next swapchain image. */
static int xr_acquire(xr_target_t *t)
{
	XrSwapchainImageAcquireInfo acquire_info = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
	XrSwapchainImageWaitInfo wait_info = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };

	if (!xr_check(xrAcquireSwapchainImage(t->swapchain, &acquire_info, &t->index), "xrAcquireSwapchainImage"))
	{
		return 0;
	}
	t->acquired = 1;

	wait_info.timeout = XR_INFINITE_DURATION;
	if (!xr_check(xrWaitSwapchainImage(t->swapchain, &wait_info), "xrWaitSwapchainImage"))
	{
		return 0;
	}

	vrgl.BindFramebuffer(GL_FRAMEBUFFER, t->framebuffer);
	vrgl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->images[t->index].image, 0);
	if (t->depth)
	{
		vrgl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, t->depth);
	}

	if (!t->checked)
	{
		GLenum status = vrgl.CheckFramebufferStatus(GL_FRAMEBUFFER);

		if (status != GL_FRAMEBUFFER_COMPLETE)
		{
			vr_printf("VR: %dx%d framebuffer incomplete (0x%X)", t->width, t->height, (unsigned)status);
		}
		t->checked = 1;
	}

	return 1;
}

/* Gives the image back to the runtime, and the window's framebuffer back to ref_gl. */
static void xr_release(xr_target_t *t)
{
	XrSwapchainImageReleaseInfo release_info = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };

	if (!t->acquired)
	{
		return;
	}

	vrgl.BindFramebuffer(GL_FRAMEBUFFER, 0);
	xr_check(xrReleaseSwapchainImage(t->swapchain, &release_info), "xrReleaseSwapchainImage");
	t->acquired = 0;
}

/* ------------------------------------------------------------------------- */
/* Session                                                                    */
/* ------------------------------------------------------------------------- */

/* Everything created so far, in reverse. Keeps the failed flag. */
static void xr_teardown(void)
{
	vr_input_destroy();
	vr_poses_clear();

	int failed = xr.failed;

	if (xr.bound_eye)
	{
		vr_gl_eye_end();
	}

	xr_release(&xr.screen);
	xr_release(&xr.eye[0]);
	xr_release(&xr.eye[1]);
	xr_destroy_target(&xr.screen);
	xr_destroy_target(&xr.eye[0]);
	xr_destroy_target(&xr.eye[1]);

	if (xr.view_space != XR_NULL_HANDLE)
	{
		xrDestroySpace(xr.view_space);
	}
	if (xr.space != XR_NULL_HANDLE)
	{
		xrDestroySpace(xr.space);
	}
	if (xr.session != XR_NULL_HANDLE)
	{
		xrDestroySession(xr.session);
	}
	if (xr.instance != XR_NULL_HANDLE)
	{
		xrDestroyInstance(xr.instance);
	}

	memset(&xr, 0, sizeof(xr));
	xr.failed = failed;
}

static int xr_choose_format(void)
{
	int64_t *formats;
	uint32_t count = 0, i;

	if (!xr_check(xrEnumerateSwapchainFormats(xr.session, 0, &count, NULL), "xrEnumerateSwapchainFormats") || !count)
	{
		return 0;
	}

	formats = malloc(count * sizeof(*formats));
	if (!formats)
	{
		return 0;
	}

	if (xr_check(xrEnumerateSwapchainFormats(xr.session, count, &count, formats), "xrEnumerateSwapchainFormats"))
	{
		/* sRGB first: the engine writes gamma-encoded colour, stored unconverted. */
		for (i = 0; i < count && !xr.format; ++i)
		{
			if (formats[i] == GL_SRGB8_ALPHA8)
			{
				xr.format = formats[i];
			}
		}
		for (i = 0; i < count && !xr.format; ++i)
		{
			if (formats[i] == GL_RGBA8)
			{
				xr.format = formats[i];
			}
		}
	}
	free(formats);

	if (!xr.format)
	{
		vr_printf("VR: the runtime offers no RGBA8 swapchain format");
	}
	return xr.format != 0;
}

/* Whether the runtime offers an extension, asked before there is an instance. */
static int xr_extension_available(const char *name)
{
	XrExtensionProperties *list;
	uint32_t count = 0, i;
	int found = 0;

	if (XR_FAILED(xrEnumerateInstanceExtensionProperties(NULL, 0, &count, NULL)) || count == 0)
	{
		return 0;
	}

	list = (XrExtensionProperties *)calloc(count, sizeof(*list));
	if (!list)
	{
		return 0;
	}
	for (i = 0; i < count; ++i)
	{
		list[i].type = XR_TYPE_EXTENSION_PROPERTIES;
	}

	if (XR_SUCCEEDED(xrEnumerateInstanceExtensionProperties(NULL, count, &count, list)))
	{
		for (i = 0; i < count; ++i)
		{
			if (strcmp(list[i].extensionName, name) == 0)
			{
				found = 1;
			}
		}
	}

	free(list);
	return found;
}

static int xr_start(void)
{
	const char *extensions[2];
	uint32_t extension_count = 0;
	XrInstanceCreateInfo instance_info = { XR_TYPE_INSTANCE_CREATE_INFO };
	XrInstanceProperties instance_props = { XR_TYPE_INSTANCE_PROPERTIES };
	XrSystemGetInfo system_info = { XR_TYPE_SYSTEM_GET_INFO };
	XrSystemProperties system_props = { XR_TYPE_SYSTEM_PROPERTIES };
	XrGraphicsRequirementsOpenGLKHR requirements = { XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };
	PFN_xrGetOpenGLGraphicsRequirementsKHR get_requirements = NULL;
	XrGraphicsBindingOpenGLWin32KHR binding = { XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR };
	XrSessionCreateInfo session_info = { XR_TYPE_SESSION_CREATE_INFO };
	XrReferenceSpaceCreateInfo space_info = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
	swap_interval_t swap_interval;
	int gl_major = 0, gl_minor = 0;
	const char *gl_version;

	/* Delay-loaded: check it is there before the first call binds to it. */
	if (!LoadLibraryA("openxr_loader.dll"))
	{
		vr_printf("VR: openxr_loader.dll is missing from the game folder");
		return 0;
	}

	if (!vr_gl_load())
	{
		vr_printf("VR: the GL context has no framebuffer objects");
		return 0;
	}

	xr_fix_runtime_override(report_runtime);
	xr_disable_unloadable_implicit_layers(report_layer);

	strcpy(instance_info.applicationInfo.applicationName, "SiN Gold VR");
	instance_info.applicationInfo.applicationVersion = 1;
	strcpy(instance_info.applicationInfo.engineName, "SiN");
	instance_info.applicationInfo.engineVersion = 113;
	/* SteamVR's 32-bit runtime refuses 1.1 (docs/OPENXR_NOTES.md). */
	instance_info.applicationInfo.apiVersion = XR_API_VERSION_1_0;
	extensions[extension_count++] = XR_KHR_OPENGL_ENABLE_EXTENSION_NAME;
	if (xr_extension_available(vr_input_extension()))
	{
		extensions[extension_count++] = vr_input_extension();
	}
	instance_info.enabledExtensionCount = extension_count;
	instance_info.enabledExtensionNames = extensions;
	if (!xr_check(xrCreateInstance(&instance_info, &xr.instance), "xrCreateInstance"))
	{
		return 0;
	}

	xrGetInstanceProperties(xr.instance, &instance_props);

	system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	if (!xr_check(xrGetSystem(xr.instance, &system_info, &xr.system), "xrGetSystem (is the headset on and SteamVR running?)"))
	{
		return 0;
	}

	xrGetSystemProperties(xr.instance, xr.system, &system_props);
	vr_printf("VR: %s %u.%u.%u, headset: %s", instance_props.runtimeName,
		(unsigned)XR_VERSION_MAJOR(instance_props.runtimeVersion),
		(unsigned)XR_VERSION_MINOR(instance_props.runtimeVersion),
		(unsigned)XR_VERSION_PATCH(instance_props.runtimeVersion), system_props.systemName);

	/* Required before xrCreateSession, and worth checking against the context. */
	if (!xr_check(xrGetInstanceProcAddr(xr.instance, "xrGetOpenGLGraphicsRequirementsKHR",
			(PFN_xrVoidFunction *)&get_requirements), "xrGetInstanceProcAddr") || !get_requirements ||
		!xr_check(get_requirements(xr.instance, xr.system, &requirements), "xrGetOpenGLGraphicsRequirementsKHR"))
	{
		return 0;
	}

	gl_version = (const char *)glGetString(GL_VERSION);
	if (gl_version)
	{
		sscanf(gl_version, "%d.%d", &gl_major, &gl_minor);
	}
	if (XR_MAKE_VERSION(gl_major, gl_minor, 0) < requirements.minApiVersionSupported)
	{
		vr_printf("VR: the runtime wants GL %u.%u or newer; this context is %s - trying anyway",
			(unsigned)XR_VERSION_MAJOR(requirements.minApiVersionSupported),
			(unsigned)XR_VERSION_MINOR(requirements.minApiVersionSupported), gl_version ? gl_version : "?");
	}

	xr.dc = wglGetCurrentDC();
	xr.context = wglGetCurrentContext();
	if (!xr.dc || !xr.context)
	{
		vr_printf("VR: no current GL context");
		return 0;
	}

	binding.hDC = xr.dc;
	binding.hGLRC = xr.context;
	session_info.next = &binding;
	session_info.systemId = xr.system;
	if (!xr_check(xrCreateSession(xr.instance, &session_info, &xr.session), "xrCreateSession"))
	{
		return 0;
	}

	/* A runtime is allowed to leave its own context current; put ref_gl's back. */
	if (wglGetCurrentContext() != xr.context)
	{
		wglMakeCurrent(xr.dc, xr.context);
	}

	space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	space_info.poseInReferenceSpace.orientation.w = 1.0f;
	if (!xr_check(xrCreateReferenceSpace(xr.session, &space_info, &xr.space), "xrCreateReferenceSpace") ||
		!xr_choose_format())
	{
		return 0;
	}

	/* Without it the screen stays where the tracking space began. */
	space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	if (XR_FAILED(xrCreateReferenceSpace(xr.session, &space_info, &xr.view_space)))
	{
		xr.view_space = XR_NULL_HANDLE;
	}

	/* The headset paces the frame loop from now on (xrWaitFrame). The desktop
	   window's vsync would be a second clock fighting it, so it goes off. */
	swap_interval = (swap_interval_t)wglGetProcAddress("wglSwapIntervalEXT");
	if (swap_interval)
	{
		swap_interval(0);
	}

	vr_input_create(xr.instance, xr.session);

	vr_printf("VR: session created");
	return 1;
}

static void xr_poll_events(void)
{
	XrEventDataBuffer event;

	for (;;)
	{
		memset(&event, 0, sizeof(event));
		event.type = XR_TYPE_EVENT_DATA_BUFFER;

		if (xrPollEvent(xr.instance, &event) != XR_SUCCESS)
		{
			return;
		}

		if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
		{
			const XrEventDataSessionStateChanged *changed = (const XrEventDataSessionStateChanged *)&event;

			xr.state = changed->state;
			vr_printf("VR: session %s", state_name(xr.state));

			if (xr.state == XR_SESSION_STATE_FOCUSED)
			{
				/* Whoever just put the headset on is the new centre. */
				xr.recenter_wanted = 1;
				xr.screen_placed = 0;
			}

			if (xr.state == XR_SESSION_STATE_READY)
			{
				XrSessionBeginInfo begin = { XR_TYPE_SESSION_BEGIN_INFO };

				begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
				xr.running = xr_check(xrBeginSession(xr.session, &begin), "xrBeginSession");
			}
			else if (xr.state == XR_SESSION_STATE_STOPPING)
			{
				xr_check(xrEndSession(xr.session), "xrEndSession");
				xr.running = 0;
			}
			else if (xr.state == XR_SESSION_STATE_EXITING || xr.state == XR_SESSION_STATE_LOSS_PENDING)
			{
				xr.running = 0;
				xr.lost = 1;
			}
		}
		else if (event.type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING)
		{
			xr.recenter_wanted = 1;
			xr.screen_placed = 0;
		}
		else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)
		{
			xr.running = 0;
			xr.lost = 1;
		}
	}
}

/* Starts OpenXR on first use and handles its events. True while frames can be submitted. */
static int xr_ready(void)
{
	if (xr.failed)
	{
		return 0;
	}

	if (!xr.started)
	{
		if (!xr_start())
		{
			xr_teardown();
			xr.failed = 1;
			vr_printf("VR: not available - carrying on flatscreen");
			return 0;
		}
		xr.started = 1;
		xr.recenter_wanted = 1;
		xr.screen_placed = 0;
	}

	xr_poll_events();

	if (xr.lost)
	{
		xr_teardown();
		xr.failed = 1;
		vr_printf("VR: the runtime ended the session - carrying on flatscreen");
		return 0;
	}

	return xr.running;
}

/* ------------------------------------------------------------------------- */
/* Theatre screen                                                             */
/* ------------------------------------------------------------------------- */

static int xr_make_screen(int width, int height)
{
	xr_destroy_target(&xr.screen);

	if (!xr_create_target(&xr.screen, width, height, 0))
	{
		xr_destroy_target(&xr.screen);
		return 0;
	}

	vr_printf("VR: theatre screen %dx%d, format 0x%X, %u images", width, height, (unsigned)xr.format,
		xr.screen.image_count);
	return 1;
}

/* Copies the finished frame, still in the window's back buffer, onto the screen. */
static int xr_copy_window(int width, int height)
{
	blit_state_t state;
	int copied;

	blit_state_begin(&state);

	copied = xr_acquire(&xr.screen);
	if (copied)
	{
		vrgl.BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
		vrgl.BlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	}
	xr_release(&xr.screen);

	blit_state_end(&state);
	return copied;
}

/*
 * The screen goes in front of the head, level and at its height, each time
 * the screen comes up after the world (a menu, a loading screen) and when the
 * player recentres. Otherwise it would hang in front of the tracking space's
 * origin - wherever the headset was when the session began, which is desk
 * height if it was lying there.
 */
static void place_screen(XrTime when)
{
	XrSpaceLocation location = { XR_TYPE_SPACE_LOCATION };
	const XrQuaternionf *q;
	float fx, fz;

	if (xr.view_space == XR_NULL_HANDLE ||
		XR_FAILED(xrLocateSpace(xr.view_space, xr.space, when, &location)) ||
		!(location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) ||
		!(location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))
	{
		return;
	}

	q = &location.pose.orientation;
	fx = -2.0f * (q->x * q->z + q->w * q->y);
	fz = -(1.0f - 2.0f * (q->x * q->x + q->y * q->y));
	if (fx * fx + fz * fz < 0.01f)
	{
		return; /* looking straight up or down: no way to face */
	}

	xr.screen_head[0] = location.pose.position.x;
	xr.screen_head[1] = location.pose.position.y;
	xr.screen_head[2] = location.pose.position.z;
	xr.screen_yaw = atan2f(-fx, -fz);
	xr.screen_placed = 1;
	vrlog("VR: the screen goes in front of the head, at (%.2f %.2f %.2f) m facing %.0f degrees",
		xr.screen_head[0], xr.screen_head[1], xr.screen_head[2], xr.screen_yaw * 57.29578f);
}

static void screen_pose(XrPosef *pose, float distance)
{
	pose->orientation.x = pose->orientation.z = 0.0f;
	pose->orientation.y = 0.0f;
	pose->orientation.w = 1.0f;
	pose->position.x = pose->position.y = 0.0f;
	pose->position.z = -distance;
	if (!xr.screen_placed)
	{
		return;
	}

	pose->orientation.y = sinf(0.5f * xr.screen_yaw);
	pose->orientation.w = cosf(0.5f * xr.screen_yaw);
	pose->position.x = xr.screen_head[0] - sinf(xr.screen_yaw) * distance;
	pose->position.y = xr.screen_head[1];
	pose->position.z = xr.screen_head[2] - cosf(xr.screen_yaw) * distance;
}

void vr_xr_frame(float screen_distance, float screen_width, int force_render)
{
	XrFrameWaitInfo wait_info = { XR_TYPE_FRAME_WAIT_INFO };
	XrFrameBeginInfo begin_info = { XR_TYPE_FRAME_BEGIN_INFO };
	XrFrameEndInfo end_info = { XR_TYPE_FRAME_END_INFO };
	XrFrameState frame_state = { XR_TYPE_FRAME_STATE };
	XrCompositionLayerQuad quad = { XR_TYPE_COMPOSITION_LAYER_QUAD };
	const XrCompositionLayerBaseHeader *layers[1];
	uint32_t layer_count = 0;
	RECT client;

	if (!xr_ready())
	{
		return;
	}

	if (!xr_check(xrWaitFrame(xr.session, &wait_info, &frame_state), "xrWaitFrame") ||
		!xr_check(xrBeginFrame(xr.session, &begin_info), "xrBeginFrame"))
	{
		return;
	}

	/* The runtime says no while the headset is idle - on the desk, or not worn. */
	if (!frame_state.shouldRender && !xr.warned_idle)
	{
		vr_printf("VR: the headset does not need frames yet (session %s)%s", state_name(xr.state),
			force_render ? " - rendering anyway for vr_force_render" : " - put it on to start");
		xr.warned_idle = 1;
	}

	if ((frame_state.shouldRender || force_render) && GetClientRect(WindowFromDC(xr.dc), &client) &&
		client.right > 0 && client.bottom > 0 &&
		((client.right == xr.screen.width && client.bottom == xr.screen.height) ||
			xr_make_screen(client.right, client.bottom)) &&
		xr_copy_window(client.right, client.bottom))
	{
		if (screen_distance < 0.5f)
		{
			screen_distance = 0.5f;
		}
		if (screen_width < 0.2f)
		{
			screen_width = 0.2f;
		}

		if (!xr.last_was_theatre || !xr.screen_placed)
		{
			place_screen(frame_state.predictedDisplayTime);
		}

		quad.space = xr.space;
		quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
		quad.subImage.swapchain = xr.screen.swapchain;
		quad.subImage.imageRect.extent.width = xr.screen.width;
		quad.subImage.imageRect.extent.height = xr.screen.height;
		screen_pose(&quad.pose, screen_distance);
		quad.size.width = screen_width;
		quad.size.height = screen_width * (float)xr.screen.height / (float)xr.screen.width;
		layers[layer_count++] = (const XrCompositionLayerBaseHeader *)&quad;

		xr.screen_distance = screen_distance;
		xr.screen_width = screen_width;
	}

	end_info.displayTime = frame_state.predictedDisplayTime;
	end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	end_info.layerCount = layer_count;
	end_info.layers = layers;

	if (xr_check(xrEndFrame(xr.session, &end_info), "xrEndFrame") && layer_count)
	{
		xr.frames_shown++;
		xr.last_was_theatre = 1;
		if (xr.frames_shown == 1 || xr.frames_shown % 5400 == 0)
		{
			vr_printf("VR: %u frames shown on the theatre screen", xr.frames_shown);
		}
	}
}

void vr_xr_idle(int force_render)
{
	XrFrameWaitInfo wait_info = { XR_TYPE_FRAME_WAIT_INFO };
	XrFrameBeginInfo begin_info = { XR_TYPE_FRAME_BEGIN_INFO };
	XrFrameEndInfo end_info = { XR_TYPE_FRAME_END_INFO };
	XrFrameState frame_state = { XR_TYPE_FRAME_STATE };
	XrCompositionLayerQuad quad = { XR_TYPE_COMPOSITION_LAYER_QUAD };
	const XrCompositionLayerBaseHeader *layers[1];
	uint32_t layer_count = 0;

	/* Never starts OpenXR, and never interrupts a frame the engine began. */
	if (!xr.started || xr.in_frame || !xr.last_was_theatre || !xr_ready())
	{
		return;
	}

	/* The engine is not reading buttons now, but the card is waiting for one. */
	vr_input_idle(xr.session);

	if (!xr_check(xrWaitFrame(xr.session, &wait_info, &frame_state), "xrWaitFrame") ||
		!xr_check(xrBeginFrame(xr.session, &begin_info), "xrBeginFrame"))
	{
		return;
	}

	/* The swapchain's last released image is shown again: nothing is drawn. */
	if ((frame_state.shouldRender || force_render) && xr.frames_shown > 0)
	{
		quad.space = xr.space;
		quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
		quad.subImage.swapchain = xr.screen.swapchain;
		quad.subImage.imageRect.extent.width = xr.screen.width;
		quad.subImage.imageRect.extent.height = xr.screen.height;
		screen_pose(&quad.pose, xr.screen_distance);
		quad.size.width = xr.screen_width;
		quad.size.height = xr.screen_width * (float)xr.screen.height / (float)xr.screen.width;
		layers[layer_count++] = (const XrCompositionLayerBaseHeader *)&quad;
	}

	end_info.displayTime = frame_state.predictedDisplayTime;
	end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	end_info.layerCount = layer_count;
	end_info.layers = layers;
	if (xr_check(xrEndFrame(xr.session, &end_info), "xrEndFrame"))
	{
		xr.idle_frames++;
		if (xr.idle_frames == 1)
		{
			vr_printf("VR: the engine has stopped drawing - keeping the headset and controllers going");
		}
	}
}

/* ------------------------------------------------------------------------- */
/* Stereo                                                                     */
/* ------------------------------------------------------------------------- */

void vr_xr_request_recenter(void)
{
	xr.recenter_wanted = 1;
	xr.screen_placed = 0;
}

void vr_xr_input(void)
{
	if (!xr_ready())
	{
		return;
	}

	vr_input_frame(xr.session);
}

int vr_xr_stereo_possible(void)
{
	return xr.started && xr.running && !xr.failed && !xr.stereo_failed;
}

#define RESOLUTION_MIN 0.25f

/*
 * The eye buffers, at the size the runtime recommends times resolution on each
 * side. The recommended size is core OpenXR, so every runtime gives one: it is
 * the size that puts one rendered pixel on each of the display's at the middle
 * of the lens, where the distortion magnifies most, and a runtime with a
 * resolution setting of its own - SteamVR's - folds that in. There is no
 * portable way to ask for the panel's own pixel count, and rendering at it
 * would be softer in the middle than this. The runtime's maximum is never
 * passed, and one factor serves both sides and both eyes, so the shape stays.
 */
static int xr_make_eye_targets(float resolution)
{
	XrViewConfigurationView config[2];
	uint32_t count = 0;
	float scale = resolution;
	int i;

	memset(config, 0, sizeof(config));
	config[0].type = config[1].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
	if (!xr_check(xrEnumerateViewConfigurationViews(xr.instance, xr.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
			2, &count, config), "xrEnumerateViewConfigurationViews") || count != 2)
	{
		return 0;
	}

	if (scale < RESOLUTION_MIN)
	{
		scale = RESOLUTION_MIN;
	}
	for (i = 0; i < 2; ++i)
	{
		float fit_x = (float)config[i].maxImageRectWidth / (float)config[i].recommendedImageRectWidth;
		float fit_y = (float)config[i].maxImageRectHeight / (float)config[i].recommendedImageRectHeight;

		if (config[i].recommendedImageRectWidth == 0 || config[i].recommendedImageRectHeight == 0)
		{
			return 0;
		}
		if (config[i].maxImageRectWidth && scale > fit_x)
		{
			scale = fit_x;
		}
		if (config[i].maxImageRectHeight && scale > fit_y)
		{
			scale = fit_y;
		}
	}

	for (i = 0; i < 2; ++i)
	{
		if (!xr_create_target(&xr.eye[i], (int)(config[i].recommendedImageRectWidth * scale + 0.5f),
				(int)(config[i].recommendedImageRectHeight * scale + 0.5f), 1))
		{
			return 0;
		}
	}

	xr.eye_scale = resolution;
	xr.eye_made = resolution;
	vr_printf("VR: stereo eye buffers %dx%d - %.2f of the %ux%u the runtime recommends%s", xr.eye[0].width,
		xr.eye[0].height, scale, config[0].recommendedImageRectWidth, config[0].recommendedImageRectHeight,
		scale < resolution ? ", as far as it allows" : (scale > resolution ? ", the least there is" : ""));
	vrlog("VR: format 0x%X; the runtime allows up to %ux%u", (unsigned)xr.format, config[0].maxImageRectWidth,
		config[0].maxImageRectHeight);
	return 1;
}

static void publish_poses(void);

/* Waits for the runtime, begins its frame, and locates both eyes for its display time. */
static int xr_begin_stereo(int force_render)
{
	XrFrameWaitInfo wait_info = { XR_TYPE_FRAME_WAIT_INFO };
	XrFrameBeginInfo begin_info = { XR_TYPE_FRAME_BEGIN_INFO };
	XrFrameState frame_state = { XR_TYPE_FRAME_STATE };
	XrViewLocateInfo locate_info = { XR_TYPE_VIEW_LOCATE_INFO };
	XrViewState view_state = { XR_TYPE_VIEW_STATE };
	uint32_t count = 0;
	int i;

	if (!xr_check(xrWaitFrame(xr.session, &wait_info, &frame_state), "xrWaitFrame") ||
		!xr_check(xrBeginFrame(xr.session, &begin_info), "xrBeginFrame"))
	{
		return 0;
	}

	xr.in_frame = 1;
	xr.eyes_rendered = 0;
	xr.display_time = frame_state.predictedDisplayTime;
	xr.should_render = frame_state.shouldRender || force_render;

	/* The runtime says no while the headset is idle - on the desk, or not worn. */
	if (!frame_state.shouldRender && !xr.warned_idle)
	{
		vr_printf("VR: the headset does not need frames yet (session %s)%s", state_name(xr.state),
			force_render ? " - rendering anyway for vr_force_render" : " - put it on to start");
		xr.warned_idle = 1;
	}

	memset(xr.views, 0, sizeof(xr.views));
	xr.views[0].type = xr.views[1].type = XR_TYPE_VIEW;
	locate_info.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	locate_info.displayTime = xr.display_time;
	locate_info.space = xr.space;

	if (!xr_check(xrLocateViews(xr.session, &locate_info, &view_state, 2, &count, xr.views), "xrLocateViews") ||
		count != 2 || !(view_state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT))
	{
		xr.should_render = 0; /* nothing to render from: this frame submits no layers */
	}

	for (i = 0; i < 2; ++i)
	{
		const XrView *view = &xr.views[i];
		vr_eye_t *eye = &xr.eye_views[i];

		eye->position[0] = view->pose.position.x;
		eye->position[1] = view->pose.position.y;
		eye->position[2] = view->pose.position.z;
		eye->orientation[0] = view->pose.orientation.x;
		eye->orientation[1] = view->pose.orientation.y;
		eye->orientation[2] = view->pose.orientation.z;
		eye->orientation[3] = view->pose.orientation.w;
		eye->tan_left = tanf(view->fov.angleLeft);
		eye->tan_right = tanf(view->fov.angleRight);
		eye->tan_down = tanf(view->fov.angleDown);
		eye->tan_up = tanf(view->fov.angleUp);
	}

	/* Between the eyes, which is where the head is. */
	if (xr.recenter_wanted && xr.should_render)
	{
		float head[3];

		for (i = 0; i < 3; ++i)
		{
			head[i] = 0.5f * (xr.eye_views[0].position[i] + xr.eye_views[1].position[i]);
		}
		vr_view_recenter(head, xr.eye_views[0].orientation);
		vr_weapon_recentred();
		xr.recenter_wanted = 0;
		vr_printf("VR: measuring from where the headset is now");
	}

	publish_poses();
	return 1;
}

/*
 * What the game module gets to see of the player: the head from between the
 * eyes, both hands, and the weapon placed in them. Written once a frame, with
 * this frame's display time, so that the hands and the view agree about when
 * they are - and before the engine builds the frame's scene, which is when the
 * weapon is put into it (vr_weapon.c).
 */
static void publish_poses(void)
{
	float head_position[3], head_origin[3], head_angles[3];
	float scale = vr_worldscale_units();
	int i;

	vr_poses_begin(scale);

	if (xr.should_render)
	{
		for (i = 0; i < 3; ++i)
		{
			head_position[i] = 0.5f * (xr.eye_views[0].position[i] + xr.eye_views[1].position[i]);
		}
		vr_room_frame(head_position, xr.eye_views[0].orientation);
		vr_view_local(head_position, xr.eye_views[0].orientation, scale, head_origin, head_angles);
		vr_poses_head(head_origin, head_angles);
		vr_gesture_head(head_origin, head_angles);
		vr_input_hands(xr.session, xr.space, xr.display_time, scale);
		vr_gesture_frame();
		vr_weapon_publish();
	}

	vr_poses_end();
}

/* Nothing else clears an eye target, and a clear needs every write mask open. */
static void xr_clear_bound_target(const xr_target_t *t)
{
	GLboolean depth_mask, color_mask[4], scissor;
	GLfloat clear_color[4];
	GLdouble clear_depth;
	GLint clear_stencil;

	glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
	glGetBooleanv(GL_COLOR_WRITEMASK, color_mask);
	glGetFloatv(GL_COLOR_CLEAR_VALUE, clear_color);
	glGetDoublev(GL_DEPTH_CLEAR_VALUE, &clear_depth);
	glGetIntegerv(GL_STENCIL_CLEAR_VALUE, &clear_stencil);
	scissor = glIsEnabled(GL_SCISSOR_TEST);

	glDepthMask(GL_TRUE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	if (scissor)
	{
		glDisable(GL_SCISSOR_TEST);
	}
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClearDepth(1.0);
	glClearStencil(0);

	/* ref_gl's BeginFrame set a full-window viewport; this is its eye-sized equal. */
	glViewport(0, 0, t->width, t->height);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	glDepthMask(depth_mask);
	glColorMask(color_mask[0], color_mask[1], color_mask[2], color_mask[3]);
	if (scissor)
	{
		glEnable(GL_SCISSOR_TEST);
	}
	glClearColor(clear_color[0], clear_color[1], clear_color[2], clear_color[3]);
	glClearDepth(clear_depth);
	glClearStencil(clear_stencil);
}

/* The left eye, cropped to the window's shape, into the window's back buffer. */
static void xr_mirror(const xr_target_t *t)
{
	blit_state_t state;
	double aspect;
	int width, height, x, y;

	if (xr.window_width <= 0 || xr.window_height <= 0)
	{
		return;
	}

	aspect = (double)xr.window_width / xr.window_height;
	width = t->width;
	height = (int)(width / aspect);
	if (height > t->height)
	{
		height = t->height;
		width = (int)(height * aspect);
	}
	x = (t->width - width) / 2;
	y = (t->height - height) / 2;

	blit_state_begin(&state);
	vrgl.BindFramebuffer(GL_READ_FRAMEBUFFER, t->framebuffer);
	vrgl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	vrgl.BlitFramebuffer(x, y, x + width, y + height, 0, 0, xr.window_width, xr.window_height,
		GL_COLOR_BUFFER_BIT, GL_LINEAR);
	blit_state_end(&state);
}

/* How far this eye is to the right of the head, in metres: half the distance
   between the eyes, which is the player's own IPD as the runtime reports it. */
static float eye_offset_x(int pass)
{
	float half = 0.0f;
	int i;

	for (i = 0; i < 3; ++i)
	{
		float d = xr.eye_views[1].position[i] - xr.eye_views[0].position[i];

		half += d * d;
	}
	half = 0.5f * (float)sqrt(half);

	return pass == 0 ? -half : half;
}

const vr_eye_t *vr_xr_begin_eye(int pass, int window_width, int window_height, int force_render,
	float resolution)
{
	xr_target_t *t;
	const vr_eye_t *eye;

	if (resolution <= 0.0f)
	{
		resolution = 1.0f;
	}

	if (pass == 0)
	{
		if (!vr_xr_stereo_possible() || !xr_ready())
		{
			return NULL;
		}

		/* A new size: between frames nothing holds an image, so the buffers can
		   simply go and be made again below. */
		if (xr.eye[0].swapchain != XR_NULL_HANDLE && resolution != xr.eye_scale)
		{
			xr_destroy_target(&xr.eye[0]);
			xr_destroy_target(&xr.eye[1]);
		}

		if (xr.eye[0].swapchain == XR_NULL_HANDLE && !xr_make_eye_targets(resolution))
		{
			/* A runtime can refuse a size it says it allows. SteamVR reports
			   8192x8192 and makes 1.4 of the recommended size happily on a fresh
			   start, but refuses a bigger size after a smaller one has been made
			   in the same session - even the recommended one - which looks like
			   a 32-bit process running out of room in one piece. So a refusal
			   goes back to the last size that was made, or to the recommended
			   one, before stereo is given up on; a vid_restart starts clean. */
			float fallback = xr.eye_made > 0.0f ? xr.eye_made : 1.0f;

			xr_destroy_target(&xr.eye[0]);
			xr_destroy_target(&xr.eye[1]);
			if (fallback != resolution && xr_make_eye_targets(fallback))
			{
				xr.eye_scale = resolution; /* not asked again until it changes */
				vr_printf("VR: the runtime would not make eye buffers at vr_resolution %.2f just now - they "
					"stay at %.2f; vid_restart makes them afresh at %.2f", resolution, fallback, resolution);
			}
			else
			{
				xr_destroy_target(&xr.eye[0]);
				xr_destroy_target(&xr.eye[1]);
				xr.stereo_failed = 1;
				vr_printf("VR: stereo is not available - staying on the theatre screen");
				return NULL;
			}
		}

		if (!xr_begin_stereo(force_render))
		{
			return NULL;
		}

		xr.window_width = window_width;
		xr.window_height = window_height;
	}
	else if (pass != 1 || !xr.in_frame)
	{
		return NULL;
	}

	if (!xr.should_render || xr.bound_eye)
	{
		return NULL;
	}

	t = &xr.eye[pass];
	if (!xr_acquire(t))
	{
		xr_release(t);
		return NULL;
	}

	xr_clear_bound_target(t);
	xr.bound_eye = pass + 1;

	eye = &xr.eye_views[pass];
	vr_gl_eye_begin(t->width, t->height, window_width, window_height,
		eye->tan_left, eye->tan_right, eye->tan_down, eye->tan_up, eye_offset_x(pass));
	return eye;
}

/*
 * The bound eye's image, as an uncompressed 24-bit TGA. GL reads rows from the
 * bottom up and TGA stores them that way by default, so the rows go straight
 * out, and GL_BGR is already the order TGA wants. The first dump of a session
 * is sinvr_eye0.tga and sinvr_eye1.tga; later ones are numbered, so that one
 * test run can compare several.
 */
static unsigned dumps_written;

static void dump_eye_image(const xr_target_t *t, int eye)
{
	char path[64];
	unsigned char header[18];
	unsigned char *pixels;
	FILE *file;
	GLint read_binding = 0, draw_binding = 0;
	GLenum error;
	size_t middle;

	pixels = (unsigned char *)malloc((size_t)t->width * t->height * 3);
	if (!pixels)
	{
		return;
	}

	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_binding);
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_binding);
	while (glGetError() != GL_NO_ERROR)
	{
		/* Whatever went wrong before this is not ours to report. */
	}

	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, t->width, t->height, GL_BGR_EXT, GL_UNSIGNED_BYTE, pixels);
	error = glGetError();
	middle = ((size_t)t->height / 2 * t->width + t->width / 2) * 3;
	vr_printf("VR: eye %d read from framebuffer %d (draw %d), error 0x%X, centre pixel %02X%02X%02X",
		eye, (int)read_binding, (int)draw_binding, (unsigned)error,
		pixels[middle + 2], pixels[middle + 1], pixels[middle]);

	if (dumps_written == 0)
	{
		sprintf(path, "sinvr_eye%d.tga", eye);
	}
	else
	{
		sprintf(path, "sinvr_eye%d_%u.tga", eye, dumps_written);
	}
	file = fopen(path, "wb");
	if (file)
	{
		memset(header, 0, sizeof(header));
		header[2] = 2; /* uncompressed true colour */
		header[12] = (unsigned char)(t->width & 0xFF);
		header[13] = (unsigned char)(t->width >> 8);
		header[14] = (unsigned char)(t->height & 0xFF);
		header[15] = (unsigned char)(t->height >> 8);
		header[16] = 24;
		fwrite(header, 1, sizeof(header), file);
		fwrite(pixels, 1, (size_t)t->width * t->height * 3, file);
		fclose(file);
		vr_printf("VR: wrote %s, %dx%d", path, t->width, t->height);
	}
	else
	{
		vr_printf("VR: could not write %s", path);
	}

	free(pixels);
}

void vr_xr_request_eye_dump(void)
{
	xr.dump_wanted = 1;
}

void vr_xr_end_eye(void)
{
	int eye = xr.bound_eye - 1;

	if (eye < 0)
	{
		return;
	}

	vr_gl_eye_end();

	/* While the eye's framebuffer is still bound and its image still acquired. */
	if (xr.dump_wanted)
	{
		dump_eye_image(&xr.eye[eye], eye);
		if (eye == 1)
		{
			xr.dump_wanted = 0;
			dumps_written++;
		}
	}

	/* The image may only be read while it is still acquired. */
	if (eye == 0)
	{
		xr_mirror(&xr.eye[0]);
	}

	xr_release(&xr.eye[eye]);
	xr.eyes_rendered |= 1 << eye;
	xr.bound_eye = 0;
}

int vr_xr_end_stereo_frame(void)
{
	XrFrameEndInfo end_info = { XR_TYPE_FRAME_END_INFO };
	XrCompositionLayerProjection layer = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
	XrCompositionLayerProjectionView views[2];
	const XrCompositionLayerBaseHeader *layers[1];
	uint32_t layer_count = 0;
	int i;

	if (!xr.in_frame)
	{
		return 0;
	}

	vr_xr_end_eye();

	if (xr.should_render && xr.eyes_rendered == 3)
	{
		memset(views, 0, sizeof(views));
		for (i = 0; i < 2; ++i)
		{
			/* Exactly the pose and field of view the eye was rendered with. */
			views[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
			views[i].pose = xr.views[i].pose;
			views[i].fov = xr.views[i].fov;
			views[i].subImage.swapchain = xr.eye[i].swapchain;
			views[i].subImage.imageRect.extent.width = xr.eye[i].width;
			views[i].subImage.imageRect.extent.height = xr.eye[i].height;
		}

		layer.space = xr.space;
		layer.viewCount = 2;
		layer.views = views;
		layers[layer_count++] = (const XrCompositionLayerBaseHeader *)&layer;
	}
	else if (xr.should_render && !xr.warned_incomplete)
	{
		vr_printf("VR: a stereo frame rendered %s - nothing was submitted for it",
			xr.eyes_rendered == 1 ? "only the left eye" : xr.eyes_rendered == 2 ? "only the right eye" : "no eyes");
		xr.warned_incomplete = 1;
	}

	end_info.displayTime = xr.display_time;
	end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	end_info.layerCount = layer_count;
	end_info.layers = layers;

	if (layer_count)
	{
		xr.last_was_theatre = 0;
	}

	if (xr_check(xrEndFrame(xr.session, &end_info), "xrEndFrame") && layer_count)
	{
		xr.stereo_frames++;
		if (xr.stereo_frames == 1 || xr.stereo_frames % 5400 == 0)
		{
			vr_printf("VR: %u frames rendered in stereo", xr.stereo_frames);
		}
	}

	xr.in_frame = 0;
	return 1;
}

void vr_xr_shutdown(void)
{
	if (!xr.started && !xr.failed && xr.instance == XR_NULL_HANDLE)
	{
		return;
	}

	if (xr.started)
	{
		vr_printf("VR: shutting down after %u stereo, %u theatre and %u idle frames", xr.stereo_frames,
			xr.frames_shown, xr.idle_frames);
	}

	xr_teardown();
	xr.failed = 0;
}
