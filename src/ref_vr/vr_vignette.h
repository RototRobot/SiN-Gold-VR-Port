/*
 * The comfort vignette: the edge of the view darkens while the stick moves
 * or smoothly turns the player. See vr_vignette.c.
 */
#ifndef VR_VIGNETTE_H
#define VR_VIGNETTE_H

#include "vr_view.h"

/* The setting (vr_vignette), whether or not the headset is used. */
void vr_vignette_startup(void);

/* How much the stick is moving or turning the player this frame, 0-1. */
void vr_vignette_motion(float amount);

/* Over the eye just rendered, after the world and before the HUD; first_eye
   is set for the first of a frame's two, which moves the fade on. */
void vr_vignette_draw(const vr_eye_t *eye, int first_eye);

/* Eye images it has been drawn over, for the log. */
unsigned vr_vignette_frames(void);

#endif /* VR_VIGNETTE_H */
