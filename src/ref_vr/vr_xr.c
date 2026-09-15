/*
 * OpenXR for ref_vr. Brings a session up on ref_gl's own GL context and, for
 * now, shows each finished frame as a virtual screen: a quad layer floating in
 * front of the player. This is the platform half on its own. Stereo rendering
 * with head tracking comes next and reuses all of it.
 *
 * Runs on the engine's main thread from EndFrame, with ref_gl's context
 * current. docs/OPENXR_NOTES.md has the runtime facts this relies on.
 */
#include <windows.h>
#include <unknwn.h> /* openxr_platform.h's Win32 interop structs use IUnknown */
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "ref_vr.h"
#include "vr_xr.h"
#include "xr_layers.h"

/* GL 3.0 names the Windows GL 1.1 header lacks. The functions come from the driver. */
#define GL_SRGB8_ALPHA8 0x8C43
#define GL_FRAMEBUFFER_SRGB 0x8DB9
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5

typedef void(APIENTRY *gen_framebuffers_t)(GLsizei n, GLuint *framebuffers);
typedef void(APIENTRY *delete_framebuffers_t)(GLsizei n, const GLuint *framebuffers);
typedef void(APIENTRY *bind_framebuffer_t)(GLenum target, GLuint framebuffer);
typedef void(APIENTRY *framebuffer_texture_2d_t)(GLenum target, GLenum attachment, GLenum textarget,
	GLuint texture, GLint level);
typedef GLenum(APIENTRY *check_framebuffer_status_t)(GLenum target);
typedef void(APIENTRY *blit_framebuffer_t)(GLint src_x0, GLint src_y0, GLint src_x1, GLint src_y1,
	GLint dst_x0, GLint dst_y0, GLint dst_x1, GLint dst_y1, GLbitfield mask, GLenum filter);
typedef BOOL(WINAPI *swap_interval_t)(int interval);

static struct
{
	gen_framebuffers_t GenFramebuffers;
	delete_framebuffers_t DeleteFramebuffers;
	bind_framebuffer_t BindFramebuffer;
	framebuffer_texture_2d_t FramebufferTexture2D;
	check_framebuffer_status_t CheckFramebufferStatus;
	blit_framebuffer_t BlitFramebuffer;
} gl;

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

	HDC dc;
	HGLRC context;

	XrSwapchain swapchain;
	int width, height;
	uint32_t image_count;
	XrSwapchainImageOpenGLKHR *images;
	GLuint framebuffer;
	int framebuffer_checked;

	unsigned frames_shown;
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

static int load_gl(void)
{
	gl.GenFramebuffers = (gen_framebuffers_t)wglGetProcAddress("glGenFramebuffers");
	gl.DeleteFramebuffers = (delete_framebuffers_t)wglGetProcAddress("glDeleteFramebuffers");
	gl.BindFramebuffer = (bind_framebuffer_t)wglGetProcAddress("glBindFramebuffer");
	gl.FramebufferTexture2D = (framebuffer_texture_2d_t)wglGetProcAddress("glFramebufferTexture2D");
	gl.CheckFramebufferStatus = (check_framebuffer_status_t)wglGetProcAddress("glCheckFramebufferStatus");
	gl.BlitFramebuffer = (blit_framebuffer_t)wglGetProcAddress("glBlitFramebuffer");

	return gl.GenFramebuffers && gl.DeleteFramebuffers && gl.BindFramebuffer &&
		gl.FramebufferTexture2D && gl.CheckFramebufferStatus && gl.BlitFramebuffer;
}

static void xr_destroy_swapchain(void)
{
	if (xr.swapchain != XR_NULL_HANDLE)
	{
		xrDestroySwapchain(xr.swapchain);
		xr.swapchain = XR_NULL_HANDLE;
	}

	free(xr.images);
	xr.images = NULL;
	xr.image_count = 0;
	xr.width = xr.height = 0;
}

/* Everything created so far, in reverse. Keeps the failed flag. */
static void xr_teardown(void)
{
	int failed = xr.failed;

	xr_destroy_swapchain();

	if (xr.framebuffer && gl.DeleteFramebuffers)
	{
		gl.DeleteFramebuffers(1, &xr.framebuffer);
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

static int xr_start(void)
{
	const char *extensions[] = { XR_KHR_OPENGL_ENABLE_EXTENSION_NAME };
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

	if (!load_gl())
	{
		vr_printf("VR: the GL context has no framebuffer objects");
		return 0;
	}

	xr_disable_unloadable_implicit_layers(report_layer);

	strcpy(instance_info.applicationInfo.applicationName, "SiN Gold VR");
	instance_info.applicationInfo.applicationVersion = 1;
	strcpy(instance_info.applicationInfo.engineName, "SiN");
	instance_info.applicationInfo.engineVersion = 113;
	/* SteamVR's 32-bit runtime refuses 1.1 (docs/OPENXR_NOTES.md). */
	instance_info.applicationInfo.apiVersion = XR_API_VERSION_1_0;
	instance_info.enabledExtensionCount = 1;
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
	if (!xr_check(xrCreateReferenceSpace(xr.session, &space_info, &xr.space), "xrCreateReferenceSpace"))
	{
		return 0;
	}

	gl.GenFramebuffers(1, &xr.framebuffer);

	/* The headset paces the frame loop from now on (xrWaitFrame). The desktop
	   window's vsync would be a second clock fighting it, so turn it off. */
	swap_interval = (swap_interval_t)wglGetProcAddress("wglSwapIntervalEXT");
	if (swap_interval)
	{
		swap_interval(0);
	}

	vr_printf("VR: session created");
	return 1;
}

static int xr_make_swapchain(int width, int height)
{
	XrSwapchainCreateInfo info = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
	int64_t *formats = NULL;
	int64_t chosen = 0;
	uint32_t count = 0, i;

	xr_destroy_swapchain();

	if (!xr_check(xrEnumerateSwapchainFormats(xr.session, 0, &count, NULL), "xrEnumerateSwapchainFormats") || !count)
	{
		return 0;
	}

	formats = malloc(count * sizeof(*formats));
	if (!formats || !xr_check(xrEnumerateSwapchainFormats(xr.session, count, &count, formats), "xrEnumerateSwapchainFormats"))
	{
		free(formats);
		return 0;
	}

	/* sRGB first: the frame holds gamma-encoded colour, copied across unconverted. */
	for (i = 0; i < count && !chosen; ++i)
	{
		if (formats[i] == GL_SRGB8_ALPHA8)
		{
			chosen = formats[i];
		}
	}
	for (i = 0; i < count && !chosen; ++i)
	{
		if (formats[i] == GL_RGBA8)
		{
			chosen = formats[i];
		}
	}
	free(formats);

	if (!chosen)
	{
		vr_printf("VR: the runtime offers no RGBA8 swapchain format");
		return 0;
	}

	info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
	info.format = chosen;
	info.sampleCount = 1;
	info.width = (uint32_t)width;
	info.height = (uint32_t)height;
	info.faceCount = 1;
	info.arraySize = 1;
	info.mipCount = 1;
	if (!xr_check(xrCreateSwapchain(xr.session, &info, &xr.swapchain), "xrCreateSwapchain"))
	{
		return 0;
	}

	if (!xr_check(xrEnumerateSwapchainImages(xr.swapchain, 0, &xr.image_count, NULL), "xrEnumerateSwapchainImages"))
	{
		xr_destroy_swapchain();
		return 0;
	}

	xr.images = calloc(xr.image_count, sizeof(*xr.images));
	for (i = 0; xr.images && i < xr.image_count; ++i)
	{
		xr.images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
	}

	if (!xr.images || !xr_check(xrEnumerateSwapchainImages(xr.swapchain, xr.image_count, &xr.image_count,
			(XrSwapchainImageBaseHeader *)xr.images), "xrEnumerateSwapchainImages"))
	{
		xr_destroy_swapchain();
		return 0;
	}

	xr.width = width;
	xr.height = height;
	vr_printf("VR: virtual screen %dx%d, format 0x%X, %u images", width, height, (unsigned)chosen, xr.image_count);
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
		else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)
		{
			xr.running = 0;
			xr.lost = 1;
		}
	}
}

/* Copies the finished frame, still in the back buffer, into the screen's next image. */
static int xr_copy_backbuffer(void)
{
	XrSwapchainImageAcquireInfo acquire_info = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
	XrSwapchainImageWaitInfo wait_info = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
	XrSwapchainImageReleaseInfo release_info = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
	GLint read_framebuffer = 0, draw_framebuffer = 0;
	GLboolean scissor, srgb;
	uint32_t index = 0;

	if (!xr_check(xrAcquireSwapchainImage(xr.swapchain, &acquire_info, &index), "xrAcquireSwapchainImage"))
	{
		return 0;
	}

	wait_info.timeout = XR_INFINITE_DURATION;
	if (!xr_check(xrWaitSwapchainImage(xr.swapchain, &wait_info), "xrWaitSwapchainImage"))
	{
		return 0;
	}

	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_framebuffer);
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_framebuffer);
	scissor = glIsEnabled(GL_SCISSOR_TEST);
	srgb = glIsEnabled(GL_FRAMEBUFFER_SRGB);

	/* A blit is clipped by the scissor test, and must not convert to sRGB. */
	if (scissor)
	{
		glDisable(GL_SCISSOR_TEST);
	}
	if (srgb)
	{
		glDisable(GL_FRAMEBUFFER_SRGB);
	}

	gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, xr.framebuffer);
	gl.FramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, xr.images[index].image, 0);

	if (!xr.framebuffer_checked)
	{
		GLenum status = gl.CheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);

		if (status != GL_FRAMEBUFFER_COMPLETE)
		{
			vr_printf("VR: virtual screen framebuffer incomplete (0x%X)", (unsigned)status);
		}
		xr.framebuffer_checked = 1;
	}

	gl.BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	gl.BlitFramebuffer(0, 0, xr.width, xr.height, 0, 0, xr.width, xr.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

	gl.BindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)read_framebuffer);
	gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)draw_framebuffer);
	if (scissor)
	{
		glEnable(GL_SCISSOR_TEST);
	}
	if (srgb)
	{
		glEnable(GL_FRAMEBUFFER_SRGB);
	}

	return xr_check(xrReleaseSwapchainImage(xr.swapchain, &release_info), "xrReleaseSwapchainImage");
}

void vr_xr_frame(float screen_distance, float screen_width)
{
	XrFrameWaitInfo wait_info = { XR_TYPE_FRAME_WAIT_INFO };
	XrFrameBeginInfo begin_info = { XR_TYPE_FRAME_BEGIN_INFO };
	XrFrameEndInfo end_info = { XR_TYPE_FRAME_END_INFO };
	XrFrameState frame_state = { XR_TYPE_FRAME_STATE };
	XrCompositionLayerQuad quad = { XR_TYPE_COMPOSITION_LAYER_QUAD };
	const XrCompositionLayerBaseHeader *layers[1];
	uint32_t layer_count = 0;
	RECT client;

	if (xr.failed)
	{
		return;
	}

	if (!xr.started)
	{
		if (!xr_start())
		{
			xr_teardown();
			xr.failed = 1;
			vr_printf("VR: not available - carrying on flatscreen");
			return;
		}
		xr.started = 1;
	}

	xr_poll_events();

	if (xr.lost)
	{
		xr_teardown();
		xr.failed = 1;
		vr_printf("VR: the runtime ended the session - carrying on flatscreen");
		return;
	}

	if (!xr.running)
	{
		return;
	}

	if (!xr_check(xrWaitFrame(xr.session, &wait_info, &frame_state), "xrWaitFrame") ||
		!xr_check(xrBeginFrame(xr.session, &begin_info), "xrBeginFrame"))
	{
		return;
	}

	if (frame_state.shouldRender && GetClientRect(WindowFromDC(xr.dc), &client) &&
		client.right > 0 && client.bottom > 0 &&
		((client.right == xr.width && client.bottom == xr.height) || xr_make_swapchain(client.right, client.bottom)) &&
		xr_copy_backbuffer())
	{
		if (screen_distance < 0.5f)
		{
			screen_distance = 0.5f;
		}
		if (screen_width < 0.2f)
		{
			screen_width = 0.2f;
		}

		quad.space = xr.space;
		quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
		quad.subImage.swapchain = xr.swapchain;
		quad.subImage.imageRect.extent.width = xr.width;
		quad.subImage.imageRect.extent.height = xr.height;
		quad.pose.orientation.w = 1.0f;
		quad.pose.position.z = -screen_distance;
		quad.size.width = screen_width;
		quad.size.height = screen_width * (float)xr.height / (float)xr.width;
		layers[layer_count++] = (const XrCompositionLayerBaseHeader *)&quad;
	}

	end_info.displayTime = frame_state.predictedDisplayTime;
	end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	end_info.layerCount = layer_count;
	end_info.layers = layers;

	if (xr_check(xrEndFrame(xr.session, &end_info), "xrEndFrame") && layer_count)
	{
		xr.frames_shown++;
		if (xr.frames_shown == 1 || xr.frames_shown % 5400 == 0)
		{
			vr_printf("VR: %u frames shown on the virtual screen", xr.frames_shown);
		}
	}
}

void vr_xr_shutdown(void)
{
	if (!xr.started && !xr.failed && xr.instance == XR_NULL_HANDLE)
	{
		return;
	}

	if (xr.started)
	{
		vr_printf("VR: shutting down after %u frames", xr.frames_shown);
	}

	xr_teardown();
	xr.failed = 0;
}
