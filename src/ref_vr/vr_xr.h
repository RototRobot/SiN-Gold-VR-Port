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

/* True while stereo frames can be rendered: a session running and eye targets
   possible. */
int vr_xr_stereo_possible(void);

/*
 * Stereo, straight after ref_gl's BeginFrame for pass 0 (left eye) and pass 1
 * (right eye). Pass 0 starts the OpenXR frame and locates both eyes. Returns the
 * eye to render with its target bound and ref_gl's GL calls redirected, or NULL
 * to let the pass draw into the window as usual - which is what an idle headset
 * gets, unless force_render says to render for it anyway.
 */
const vr_eye_t *vr_xr_begin_eye(int pass, int window_width, int window_height, int force_render);

/* Finishes the bound eye, if any. The left eye is mirrored to the window first. */
void vr_xr_end_eye(void);

/* At EndFrame: submits the stereo frame begun by pass 0. Returns 0 if there was
   none, in which case the theatre screen should show this frame. */
int vr_xr_end_stereo_frame(void);

/* Destroys everything and allows a fresh start. Safe when nothing was started. */
void vr_xr_shutdown(void);

#endif /* VR_XR_H */
