cmake_minimum_required(VERSION 3.28)
project(SiNGoldVR VERSION 1.0.1 LANGUAGES C CXX)

if(NOT MSVC)
	message(FATAL_ERROR "SiN Gold VR builds with MSVC only: the game DLL has to match the engine's ABI.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
	message(FATAL_ERROR "SiN is a 32-bit game. Configure for Win32: cmake --preset win32")
endif()

# Release builds keep PDBs: this mod gets debugged against a closed engine.
add_compile_options("$<$<CONFIG:Release>:/Zi>")
add_link_options("$<$<CONFIG:Release>:/DEBUG>" "$<$<CONFIG:Release>:/OPT:REF>" "$<$<CONFIG:Release>:/OPT:ICF>")

# Everything the game needs lands in a staging tree laid out like the game folder:
#   stage/<config>/ref_vr.dll, openxr_loader.dll   beside sin.exe
#   stage/<config>/sinvr/gamex86.dll               the SiN mod folder
#   stage/<config>/sinvr2015/gamex86.dll           Wages of Sin
#   stage/<config>/tools/                          diagnostics
set(SINVR_STAGE_DIR "${CMAKE_BINARY_DIR}/stage")

function(sinvr_stage target subdir)
	set_target_properties(${target} PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${SINVR_STAGE_DIR}/$<CONFIG>/${subdir}"
		ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<CONFIG>/${target}"
	)
endfunction()

include(cmake/openxr.cmake)

add_subdirectory(game)
add_subdirectory(src/ref_vr)
add_subdirectory(tools/xrprobe)

# A DLL names its PDB; by default with the full path it was built at, which would
# put the builder's folders - their user name - into every released file. Only
# the file's own name instead (a debugger finds it beside the DLL).
foreach(target ref_vr game_sin game_wos openxr_loader xrprobe)
	if(TARGET ${target})
		target_link_options(${target} PRIVATE "/PDBALTPATH:$<TARGET_PDB_FILE_NAME:${target}>")
	endif()
endforeach()
