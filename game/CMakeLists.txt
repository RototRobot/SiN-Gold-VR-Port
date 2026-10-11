# SiN Gold's game logic - Night Dive Studios' source release, under the licence
# in license.txt beside this file - built as the two gamex86.dll files the
# engine loads from a game folder.
#
# Mirrors src/code/vc2015/*.vcxproj (Release|Win32), which are kept for
# reference. The file lists are theirs exactly: several sources in these
# folders are deliberately not part of each build.

set(SINVR_GAME_ELIB_SOURCES
	src/code/elib/emisc.cpp
	src/code/elib/qstring.cpp
	src/code/elib/zone.cpp
)

set(SINVR_GAME_SIN_SOURCES
	actor.cpp ammo.cpp animals.cpp archive.cpp areaportal.cpp armor.cpp assaultrifle.cpp
	bacrodai.cpp behavior.cpp bouncingbetty.cpp box.cpp bspline.cpp bullet.cpp camera.cpp
	camgun.cpp chaingun.cpp class.cpp console.cpp ctf.cpp ctf_turret.cpp deadbody.cpp doors.cpp
	earthquake.cpp entity.cpp eonandpeon.cpp explosion.cpp fists.cpp gamescript.cpp
	genericbullet.cpp genericrocket.cpp gibs.cpp glowstick.cpp grapple.cpp gravpath.cpp g_main.cpp
	g_phys.cpp g_spawn.cpp g_utils.cpp hammer.cpp health.cpp heligun.cpp inventoryitem.cpp
	item.cpp keys.cpp launcher.cpp lensflare.cpp light.cpp listener.cpp magnum.cpp misc.cpp
	mover.cpp mutanthands.cpp navigate.cpp object.cpp path.cpp peon.cpp player.cpp
	PlayerStart.cpp powerups.cpp pulserifle.cpp quantumd.cpp q_shared.c reactiveshields.cpp
	rocketlauncher.cpp rocket_turret.cpp script.cpp scriptmaster.cpp scriptslave.cpp
	scriptvariable.cpp secgun.cpp securityturret.cpp sentient.cpp shield.cpp shotgun.cpp
	shotrocketlauncher.cpp silencer.cpp skeet.cpp sniperrifle.cpp speargun.cpp specialfx.cpp
	spidermine.cpp steering.cpp str.cpp stungun.cpp surface.cpp testweapon.cpp thrall.cpp
	trigger.cpp turret.cpp vehicle.cpp viewthing.cpp weapon.cpp worldspawn.cpp wrench.cpp
)
list(TRANSFORM SINVR_GAME_SIN_SOURCES PREPEND src/code/game/)

set(SINVR_GAME_WOS_SOURCES
	actor.cpp ammo.cpp animals.cpp arcade_comm.cpp archive.cpp areaportal.cpp armor.cpp
	assaultrifle.cpp bacrodai.cpp behavior.cpp bob.cpp bouncingbetty.cpp box.cpp bspline.cpp
	bullet.cpp camera.cpp camgun.cpp ceilingsteering.cpp chaingun.cpp checkpoints.cpp class.cpp
	concussion.cpp console.cpp crawler.cpp crossbow.cpp ctf.cpp ctf_turret.cpp deadbody.cpp
	doors.cpp dualmagnum.cpp earthquake.cpp entity.cpp eonandpeon.cpp explosion.cpp fists.cpp
	flamethrower.cpp flashlight.cpp gamescript.cpp genericbullet.cpp genericrocket.cpp gibs.cpp
	glowstick.cpp goliath.cpp grapple.cpp gravpath.cpp guidedmissile.cpp g_main.cpp g_phys.cpp
	g_spawn.cpp g_utils.cpp hammer.cpp health.cpp heligun.cpp hoverbike.cpp hoverweap.cpp
	informergun.cpp inventoryitem.cpp item.cpp jitter.cpp keys.cpp launcher.cpp lensflare.cpp
	light.cpp listener.cpp magnum.cpp maneroboss.cpp misc.cpp mortician.cpp movecapture.cpp
	mover.cpp mutanthands.cpp navigate.cpp nuke.cpp object.cpp path.cpp peon.cpp player.cpp
	PlayerStart.cpp powerups.cpp pulserifle.cpp quantumd.cpp q_shared.c reactiveshields.cpp
	rocketlauncher.cpp rocketpack.cpp rocket_turret.cpp rope.cpp script.cpp scriptmaster.cpp
	scriptslave.cpp scriptvariable.cpp secgun.cpp securityturret.cpp sentient.cpp shield.cpp
	shotgun.cpp shotrocketlauncher.cpp silencer.cpp skeet.cpp sniperrifle.cpp speargun.cpp
	specialfx.cpp spidermine.cpp splitter.cpp spritegun.cpp steering.cpp str.cpp stungun.cpp
	surface.cpp testweapon.cpp thrall.cpp thug.cpp trigger.cpp turret.cpp vehicle.cpp
	viewthing.cpp weapon.cpp worldspawn.cpp wrench.cpp
)
list(TRANSFORM SINVR_GAME_WOS_SOURCES PREPEND src/code/game2015/)

# Ours, not Night Dive's: the link to the VR renderer's head and hand poses.
set(SINVR_GAME_VR_SOURCES
	"${CMAKE_SOURCE_DIR}/src/game_vr/vr_link.cpp"
	"${CMAKE_SOURCE_DIR}/src/game_vr/vr_link.h"
	"${CMAKE_SOURCE_DIR}/src/game_vr/vr_game.cpp"
	"${CMAKE_SOURCE_DIR}/src/game_vr/vr_game.h"
	"${CMAKE_SOURCE_DIR}/src/shared/sinvr_poses.h"
)

function(sinvr_game_dll target game_dir sources def_file extra_definitions stage_subdir)
	add_library(${target} SHARED ${SINVR_GAME_ELIB_SOURCES} ${sources} ${SINVR_GAME_VR_SOURCES} ${def_file})

	# So that the game sources can say #include "game_vr/vr_link.h", and the
	# shared VR game code (vr_game.cpp) finds this game's own headers.
	target_include_directories(${target} PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/${game_dir}")

	target_compile_definitions(${target} PRIVATE
		WIN32 _WINDOWS _USRDLL GAMEX86_EXPORTS BUILDING_REF_GL SIN ELIB_GAMEDLL
		_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE ${extra_definitions}
	)
	target_compile_options(${target} PRIVATE /W3 /Gy /Oi)
	target_link_libraries(${target} PRIVATE winmm)

	set_target_properties(${target} PROPERTIES
		OUTPUT_NAME gamex86
		INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
	)
	sinvr_stage(${target} ${stage_subdir})
endfunction()

sinvr_game_dll(game_sin src/code/game "${SINVR_GAME_SIN_SOURCES}" src/code/game/game.def "" sinvr)
# Wages of Sin's goes in sinvr2015: sin.exe adds the 2015 folder - the expansion's
# data - under any game folder whose name has "2015" in it, and runs as the
# expansion there (docs/ENGINE_NOTES.md).
sinvr_game_dll(game_wos src/code/game2015 "${SINVR_GAME_WOS_SOURCES}" src/code/game2015/game.def "GAMEX86_WOS_EXPORTS" sinvr2015)
