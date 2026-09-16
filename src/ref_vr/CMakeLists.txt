# ref_vr.dll - the VR renderer. sin.exe loads it for "+set vid_ref vr".
add_library(ref_vr SHARED
	ref_vr.c
	ref_vr.h
	ref_vr.def
	refapi.h
	vr_gl.c
	vr_gl.h
	vr_hook.c
	vr_hook.h
	vr_input.c
	vr_input.h
	vr_joy.c
	vr_joy.h
	vr_keys.c
	vr_keys.h
	vr_poses.c
	vr_poses.h
	../shared/sinvr_poses.h
	vr_view.c
	vr_view.h
	vr_xr.c
	vr_xr.h
	vrlog.c
	vrlog.h
	xr_layers.c
	xr_layers.h
)

target_compile_definitions(ref_vr PRIVATE
	WIN32_LEAN_AND_MEAN
	_CRT_SECURE_NO_WARNINGS
	SINVR_VERSION="${PROJECT_VERSION}"
)
target_compile_options(ref_vr PRIVATE /W3)

# openxr_loader.dll is delay-loaded, so a missing loader costs VR, not the game.
target_link_libraries(ref_vr PRIVATE OpenXR::openxr_loader OpenXR::headers opengl32 winmm delayimp)
target_link_options(ref_vr PRIVATE /DELAYLOAD:openxr_loader.dll)

# Static CRT: nothing to install beyond the DLL itself.
set_property(TARGET ref_vr PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

sinvr_stage(ref_vr "")

# The loader goes beside sin.exe too.
add_custom_command(TARGET ref_vr POST_BUILD
	COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:openxr_loader>" "${SINVR_STAGE_DIR}/$<CONFIG>/"
	VERBATIM
)
