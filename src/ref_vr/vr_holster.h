#ifndef VR_HOLSTER_H
#define VR_HOLSTER_H

/*
 * The holster menus: hold the gun hand's grip at a shoulder and the weapons the
 * player carries come up in front of them; hold the other hand's grip at its own
 * hip and the inventory does. That hand's trigger steps along the row, and
 * taking the hand out of the holster takes the one picked - the weapon is
 * switched to, the item used. Letting go of the grip in the holster closes it
 * with nothing changed. The game sends what the player carries (SinVrHolsters);
 * the gestures (vr_gesture.c) open, step and close the menu.
 */

#define VR_HOLSTER_WEAPONS 1
#define VR_HOLSTER_ITEMS 2

/* The engine's own 2D drawing, which the menu is drawn with. */
typedef void (*vr_draw_stretch_pic_t)(int x, int y, int width, int height, const char *name);
typedef void (*vr_draw_char_t)(int x, int y, int c);
typedef void (*vr_draw_fill_t)(int x, int y, int width, int height, int c);
typedef void (*vr_draw_get_pic_size_t)(int *width, int *height, const char *name);

/* Registers vr_fake_holster, for desk tests. */
void vr_holster_startup(void);

void vr_holster_set_draw(vr_draw_stretch_pic_t stretch_pic, vr_draw_char_t draw_char, vr_draw_fill_t fill,
	vr_draw_get_pic_size_t get_pic_size);

/* Opens that menu, on the one the player has now; 0 if there is nothing in it,
   or the game is not sending what the player carries. */
int vr_holster_open(int kind);

/* Which menu is open, or 0. */
int vr_holster_open_kind(void);

/* The trigger: the next one along, round to the first. */
void vr_holster_next(void);

/* Closes the menu, taking the one picked if take is set. */
void vr_holster_close(int take);

/* Over the HUD of the eye being drawn, in the window's 2D coordinates. */
void vr_holster_draw(int width, int height);

#endif /* VR_HOLSTER_H */
