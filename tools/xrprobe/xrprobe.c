/*
 * xrprobe - checks the 32-bit OpenXR path end to end, without the game:
 * loader -> active runtime -> headset -> OpenGL graphics requirements.
 * Creates no session and no GL context. Creating the instance may start SteamVR.
 *
 * Runs the same implicit-layer hygiene as ref_vr (src/ref_vr/xr_layers.c), so a
 * pass here means the game's OpenXR start-up will get as far.
 */
#include <windows.h>
#include <unknwn.h> /* openxr_platform.h's Win32 interop structs use IUnknown */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "xr_layers.h"

static void print_version(const char *label, XrVersion v)
{
	printf("%s%u.%u.%u\n", label, (unsigned)XR_VERSION_MAJOR(v),
		(unsigned)XR_VERSION_MINOR(v), (unsigned)XR_VERSION_PATCH(v));
}

static void print_result(XrInstance instance, const char *call, XrResult r)
{
	char s[XR_MAX_RESULT_STRING_SIZE];

	if (instance != XR_NULL_HANDLE && XR_SUCCEEDED(xrResultToString(instance, r, s)))
	{
		printf("%s: %s\n", call, s);
	}
	else
	{
		printf("%s: %d\n", call, (int)r);
	}
}

static void report_layer(const char *library, const char *reason)
{
	printf("disabled implicit layer for this process (%s): %s\n", reason, library);
}

static void list_layers(void)
{
	uint32_t n = 0, i;
	XrApiLayerProperties *layers;

	if (XR_FAILED(xrEnumerateApiLayerProperties(0, &n, NULL)))
	{
		printf("api layers: enumeration failed\n");
		return;
	}

	printf("api layers: %u\n", n);
	if (n == 0)
	{
		return;
	}

	layers = calloc(n, sizeof(*layers));
	for (i = 0; i < n; ++i)
	{
		layers[i].type = XR_TYPE_API_LAYER_PROPERTIES;
	}

	xrEnumerateApiLayerProperties(n, &n, layers);
	for (i = 0; i < n; ++i)
	{
		printf("  %s\n", layers[i].layerName);
	}

	free(layers);
}

/* Loads the runtime but should not start it. Returns 1 if GL is supported. */
static int list_extensions(void)
{
	uint32_t n = 0, i;
	XrExtensionProperties *ext;
	XrResult r;
	int have_gl = 0;

	r = xrEnumerateInstanceExtensionProperties(NULL, 0, &n, NULL);
	if (XR_FAILED(r))
	{
		print_result(XR_NULL_HANDLE, "xrEnumerateInstanceExtensionProperties", r);
		return 0;
	}

	ext = calloc(n, sizeof(*ext));
	for (i = 0; i < n; ++i)
	{
		ext[i].type = XR_TYPE_EXTENSION_PROPERTIES;
	}

	xrEnumerateInstanceExtensionProperties(NULL, n, &n, ext);
	printf("runtime extensions: %u\n", n);
	for (i = 0; i < n; ++i)
	{
		printf("  %s v%u\n", ext[i].extensionName, ext[i].extensionVersion);
		if (strcmp(ext[i].extensionName, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME) == 0)
		{
			have_gl = 1;
		}
	}

	free(ext);
	return have_gl;
}

static XrInstance create_instance(XrVersion api, XrResult *result)
{
	const char *extensions[] = { XR_KHR_OPENGL_ENABLE_EXTENSION_NAME };
	XrInstanceCreateInfo ci = { XR_TYPE_INSTANCE_CREATE_INFO };
	XrInstance instance = XR_NULL_HANDLE;

	strcpy(ci.applicationInfo.applicationName, "SiN VR xrprobe");
	ci.applicationInfo.applicationVersion = 1;
	strcpy(ci.applicationInfo.engineName, "SiN");
	ci.applicationInfo.engineVersion = 113;
	ci.applicationInfo.apiVersion = api;
	ci.enabledExtensionCount = 1;
	ci.enabledExtensionNames = extensions;

	*result = xrCreateInstance(&ci, &instance);
	return instance;
}

static void probe_system(XrInstance instance)
{
	XrSystemGetInfo gi = { XR_TYPE_SYSTEM_GET_INFO };
	XrSystemProperties sp = { XR_TYPE_SYSTEM_PROPERTIES };
	XrViewConfigurationView views[2];
	XrEnvironmentBlendMode modes[8];
	XrGraphicsRequirementsOpenGLKHR req = { XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };
	PFN_xrGetOpenGLGraphicsRequirementsKHR get_gl_requirements = NULL;
	XrSystemId system = XR_NULL_SYSTEM_ID;
	uint32_t n = 0, i;
	XrResult r;

	gi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	r = xrGetSystem(instance, &gi, &system);
	print_result(instance, "xrGetSystem(HMD)", r);
	if (XR_FAILED(r))
	{
		return;
	}

	xrGetSystemProperties(instance, system, &sp);
	printf("system: %s, vendor 0x%x\n", sp.systemName, sp.vendorId);
	printf("  max swapchain %ux%u, max layers %u\n", sp.graphicsProperties.maxSwapchainImageWidth,
		sp.graphicsProperties.maxSwapchainImageHeight, sp.graphicsProperties.maxLayerCount);
	printf("  tracking: orientation %d, position %d\n", (int)sp.trackingProperties.orientationTracking,
		(int)sp.trackingProperties.positionTracking);

	for (i = 0; i < 2; ++i)
	{
		memset(&views[i], 0, sizeof(views[i]));
		views[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
	}

	r = xrEnumerateViewConfigurationViews(instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
		2, &n, views);
	print_result(instance, "xrEnumerateViewConfigurationViews(stereo)", r);
	for (i = 0; XR_SUCCEEDED(r) && i < n; ++i)
	{
		printf("  eye %u: recommended %ux%u, max %ux%u, samples %u (max %u)\n", i,
			views[i].recommendedImageRectWidth, views[i].recommendedImageRectHeight,
			views[i].maxImageRectWidth, views[i].maxImageRectHeight,
			views[i].recommendedSwapchainSampleCount, views[i].maxSwapchainSampleCount);
	}

	r = xrEnumerateEnvironmentBlendModes(instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
		8, &n, modes);
	for (i = 0; XR_SUCCEEDED(r) && i < n; ++i)
	{
		printf("  blend mode %d\n", (int)modes[i]);
	}

	r = xrGetInstanceProcAddr(instance, "xrGetOpenGLGraphicsRequirementsKHR",
		(PFN_xrVoidFunction *)&get_gl_requirements);
	if (XR_FAILED(r) || get_gl_requirements == NULL)
	{
		print_result(instance, "xrGetInstanceProcAddr(xrGetOpenGLGraphicsRequirementsKHR)", r);
		return;
	}

	r = get_gl_requirements(instance, system, &req);
	print_result(instance, "xrGetOpenGLGraphicsRequirementsKHR", r);
	if (XR_SUCCEEDED(r))
	{
		print_version("  GL min: ", req.minApiVersionSupported);
		print_version("  GL max: ", req.maxApiVersionSupported);
	}
}

int main(void)
{
	XrInstanceProperties ip = { XR_TYPE_INSTANCE_PROPERTIES };
	XrInstance instance;
	XrResult r;

	printf("process: %u-bit\n", (unsigned)(sizeof(void *) * 8));
	xr_disable_unloadable_implicit_layers(report_layer);
	list_layers();

	if (!list_extensions())
	{
		printf("XR_KHR_opengl_enable: NOT AVAILABLE - stopping\n");
		return 1;
	}
	printf("XR_KHR_opengl_enable: available\n");

	/* SteamVR 2.17.9 rejects 1.1 from a 32-bit process; try it anyway so the
	   probe notices the day that changes. */
	instance = create_instance(XR_API_VERSION_1_1, &r);
	print_result(XR_NULL_HANDLE, "xrCreateInstance(api 1.1)", r);
	if (r == XR_ERROR_API_VERSION_UNSUPPORTED)
	{
		instance = create_instance(XR_API_VERSION_1_0, &r);
		print_result(XR_NULL_HANDLE, "xrCreateInstance(api 1.0)", r);
	}
	if (XR_FAILED(r))
	{
		return 1;
	}

	xrGetInstanceProperties(instance, &ip);
	printf("runtime: %s ", ip.runtimeName);
	print_version("", ip.runtimeVersion);

	probe_system(instance);

	xrDestroyInstance(instance);
	printf("done\n");
	return 0;
}
