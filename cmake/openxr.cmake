# The OpenXR loader (openxr_loader.dll) and headers, built from Khronos' source.
#
# SINVR_OPENXR_SDK can point at an OpenXR-SDK-Source or OpenXR-SDK checkout. By
# default the copy beside this repository is used when it exists; otherwise the
# pinned release is fetched from GitHub at configure time.
set(SINVR_OPENXR_TAG "release-1.1.63")
set(SINVR_OPENXR_SDK "${PROJECT_SOURCE_DIR}/../OpenXR-SDK-Source-release-1.1.63"
	CACHE PATH "OpenXR SDK source tree (OpenXR-SDK-Source or OpenXR-SDK)")

# Only the loader: no API layers, tests or samples.
set(DYNAMIC_LOADER ON CACHE BOOL "" FORCE)
set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SDK_TESTS OFF CACHE BOOL "" FORCE)

if(EXISTS "${SINVR_OPENXR_SDK}/CMakeLists.txt")
	message(STATUS "OpenXR: using ${SINVR_OPENXR_SDK}")
	add_subdirectory("${SINVR_OPENXR_SDK}" "${CMAKE_BINARY_DIR}/openxr" EXCLUDE_FROM_ALL)
else()
	message(STATUS "OpenXR: fetching OpenXR-SDK ${SINVR_OPENXR_TAG}")
	include(FetchContent)
	FetchContent_Declare(openxr
		GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
		GIT_TAG ${SINVR_OPENXR_TAG}
		GIT_SHALLOW TRUE
		EXCLUDE_FROM_ALL
	)
	FetchContent_MakeAvailable(openxr)
endif()
