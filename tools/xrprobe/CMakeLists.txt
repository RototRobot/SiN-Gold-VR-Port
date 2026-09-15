# xrprobe - checks the 32-bit OpenXR path without the game (docs/OPENXR_NOTES.md).
add_executable(xrprobe xrprobe.c "${PROJECT_SOURCE_DIR}/src/ref_vr/xr_layers.c")
target_include_directories(xrprobe PRIVATE "${PROJECT_SOURCE_DIR}/src/ref_vr")
target_compile_definitions(xrprobe PRIVATE WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_options(xrprobe PRIVATE /W3)
target_link_libraries(xrprobe PRIVATE OpenXR::openxr_loader OpenXR::headers)
set_property(TARGET xrprobe PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

sinvr_stage(xrprobe tools)

add_custom_command(TARGET xrprobe POST_BUILD
	COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:openxr_loader>" "${SINVR_STAGE_DIR}/$<CONFIG>/tools/"
	VERBATIM
)
