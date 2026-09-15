/*
 * The OpenXR side of ref_vr. Every call is made on the engine's main thread
 * with ref_gl's GL context current.
 */
#ifndef VR_XR_H
#define VR_XR_H

/*
 * Once per engine frame, with the finished frame still in the back buffer.
 * Starts OpenXR on first use, then shows that frame as a virtual screen
 * screen_width metres wide, screen_distance metres in front of where the
 * headset was when the session began. If OpenXR cannot start it says why once
 * and does nothing more until vr_xr_shutdown() is called.
 */
void vr_xr_frame(float screen_distance, float screen_width);

/* Destroys everything and allows a fresh start. Safe when nothing was started. */
void vr_xr_shutdown(void);

#endif /* VR_XR_H */
