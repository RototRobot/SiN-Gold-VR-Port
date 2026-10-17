/*
 * The OpenXR side of ref_vr. Every call is made on the engine's main thread
 * with ref_gl's GL context current.
 *
 * Each engine frame is shown one of two ways:
 *   - in stereo: the engine's two BeginFrame passes render the two eyes,
 *     vr_xr_begin_eye() / vr_xr_end_eye() around each, vr_xr_end_stereo_frame()
 *     at EndFrame;
 *   - on the theatre screen: vr_xr_frame() at EndFrame shows the finished window
 *     image on a quad (menus, loading, and whenever stereo is off).
 */
#ifndef VR_XR_H
#define VR_XR_H

#include "vr_view.h"

/*
 * Theatre screen, at EndFrame with the finished frame in the back buffer.
 * Starts OpenXR on first use. The screen is screen_width metres wide,
 * screen_distance metres in front of where the headset was when the session
 * began. If OpenXR cannot start, it says why once and does nothing more until
 * vr_xr_shutdown().
 *
 * force_render submits a picture even while the runtime says the headset does
 * not need one, which is how it reports an idle headset lying on the desk.
 */
void vr_xr_frame(float screen_distance, float screen_width, int force_render);

/*
 * Once per engine frame, before anything is drawn: brings the session up if it
 * is not, handles its events, and reads the controllers (vr_input.c).
 */
void vr_xr_input(void);

/* True while stereo frames can be rendered: a session running and eye targets
   possible. */
int vr_xr_stereo_possible(void);

/*
 * Stereo, straight after ref_gl's BeginFrame for pass 0 (left eye) and pass 1
 * (right eye). Pass 0 starts the OpenXR frame and locates both eyes. Returns the
 * eye to render with its target bound and ref_gl's GL calls redirected, or NULL
 * to let the pass draw into the window as usual - which is what an idle headset
 * gets, unless force_render says to render for it anyway.
 *
 * resolution scales the size each eye is rendered at, on each side, from the
 * one the runtime recommends; 1 is that size. A new value takes effect at the
 * next pass 0, where the eye buffers are made again.
 */
const vr_eye_t *vr_xr_begin_eye(int pass, int window_width, int window_height, int force_render,
	float resolution);

/* Finishes the bound eye, if any. The left eye is mirrored to the window first. */
void vr_xr_end_eye(void);

/* At EndFrame: submits the stereo frame begun by pass 0. Returns 0 if there was
   none, in which case the theatre screen should show this frame. */
int vr_xr_end_stereo_frame(void);

/* The size of the eye images being rendered into, in pixels. */
void vr_xr_eye_size(int *width, int *height);

/*
 * Measures the head afresh on the next stereo frame, so that where the player
 * is standing and facing now becomes where the game puts their eyes. Done by
 * itself when the session starts, when the headset is put on, and when the
 * runtime moves the tracking space.
 */
void vr_xr_request_recenter(void);

/*
 * Writes the next stereo frame's two eye images beside sin.exe, as
 * sinvr_eye0.tga and sinvr_eye1.tga. For checking at the desk what only the
 * headset would otherwise show: where the HUD panel sits, and whether the two
 * eyes agree about it.
 */
void vr_xr_request_eye_dump(void);

/*
 * For when the engine has stopped drawing without stopping its loop - SiN's
 * "press any key" card does exactly that. Keeps the OpenXR frame loop going, so
 * that the headset goes on showing the last theatre picture instead of the
 * runtime's own loading grid, and so that the controllers can answer the card.
 * Does nothing if the last thing shown was a stereo frame, which the runtime
 * reprojects better than anything this could offer.
 */
void vr_xr_idle(int force_render);

/* Destroys everything and allows a fresh start. Safe when nothing was started. */
void vr_xr_shutdown(void);

#endif /* VR_XR_H */
