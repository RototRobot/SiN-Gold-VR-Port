/*
 * OpenGL for ref_vr: the framebuffer entry points the Windows GL 1.1 headers
 * lack, and interception of ref_gl's own GL calls while an eye is rendered.
 */
#ifndef VR_GL_H
#define VR_GL_H

#include <windows.h>
#include <GL/gl.h>

#define GL_SRGB8_ALPHA8 0x8C43
#define GL_FRAMEBUFFER_SRGB 0x8DB9
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_RENDERBUFFER 0x8D41
#define GL_DEPTH24_STENCIL8 0x88F0
#define GL_BGR_EXT 0x80E0

typedef struct
{
	void(APIENTRY *GenFramebuffers)(GLsizei n, GLuint *framebuffers);
	void(APIENTRY *DeleteFramebuffers)(GLsizei n, const GLuint *framebuffers);
	void(APIENTRY *BindFramebuffer)(GLenum target, GLuint framebuffer);
	void(APIENTRY *FramebufferTexture2D)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture,
		GLint level);
	void(APIENTRY *FramebufferRenderbuffer)(GLenum target, GLenum attachment, GLenum renderbuffertarget,
		GLuint renderbuffer);
	GLenum(APIENTRY *CheckFramebufferStatus)(GLenum target);
	void(APIENTRY *BlitFramebuffer)(GLint src_x0, GLint src_y0, GLint src_x1, GLint src_y1, GLint dst_x0,
		GLint dst_y0, GLint dst_x1, GLint dst_y1, GLbitfield mask, GLenum filter);
	void(APIENTRY *GenRenderbuffers)(GLsizei n, GLuint *renderbuffers);
	void(APIENTRY *DeleteRenderbuffers)(GLsizei n, const GLuint *renderbuffers);
	void(APIENTRY *BindRenderbuffer)(GLenum target, GLuint renderbuffer);
	void(APIENTRY *RenderbufferStorage)(GLenum target, GLenum internalformat, GLsizei width, GLsizei height);
} vr_gl_t;

extern vr_gl_t vrgl;

/* Fills vrgl from the current context. Returns 0 if anything is missing. */
int vr_gl_load(void);

/*
 * Call before ref_gl's Init. ref_gl looks every GL function up by name through
 * GetProcAddress (its QGL layer); this answers glFrustum, glOrtho, glViewport
 * and glScissor with versions that follow vr_gl_eye_begin/end, and glTranslatef
 * and glPopMatrix with ones that can cut a model (vr_gl_clip_model_at). Returns
 * 0 if the import could not be patched.
 */
int vr_gl_hook_refgl(HMODULE refgl);

/*
 * The HUD panel: ref_gl draws its 2D - HUD, crosshair, console - in window
 * coordinates, and stretching that over the whole eye would push it to the edge
 * of vision and, because the eye frustums are asymmetric, put the crosshair at a
 * different angle in each eye, which is seen as two crosshairs. Instead the 2D
 * goes into a panel of fov degrees across, centred on the eye's own axis and
 * shifted so that it sits depth metres away. A depth of 0 puts it at infinity.
 */
void vr_gl_set_hud(float fov, float depth);

/*
 * Around a 2D call that is meant to cover the view rather than sit in the HUD
 * panel, such as the full-screen blend.
 */
void vr_gl_full_screen_2d(int on);

/*
 * Draws the model placed at origin - to within a hundredth of a unit - with
 * everything on the far side of a plane cut away: the plane is a, b, c, d in
 * the world, and what is kept is where a x + b y + c z + d >= 0.
 */
void vr_gl_clip_model_at(const float origin[3], const float plane[4]);

/*
 * Draws the model placed at origin mirrored down its own middle - the plane of
 * its forward and up - which is what a weapon built for one hand has to be to
 * look right in the other. The winding is swapped with it, so the model is not
 * turned inside out.
 */
void vr_gl_mirror_model_at(const float origin[3]);

/*
 * Draws the model placed at origin without the parts made only of the given
 * vertices: count pairs of the first and last vertex of a run, numbered as the
 * model's own files number them, which is how ref_gl draws them. That is how
 * the arm SiN built into a weapon's model, all one mesh with the weapon, is
 * left out. The ranges must stay valid until the frame is over.
 */
void vr_gl_hide_model_parts_at(const float origin[3], const int (*ranges)[2], int count);

/*
 * Draws the model placed at origin with its missing side filled in: a view
 * model was built to be seen from the left and above, and most of its right
 * side was never made. Each triangle facing left, on the left of the plane
 * y = middle (in the model's own frame, at the size it is drawn), is drawn a
 * second time reflected across that plane, turned to face right.
 */
void vr_gl_fill_model_at(const float origin[3], float middle);

/* Whether ref_gl is the build whose vertices vr_gl_hide_model_parts_at can
   recognise - and whose strips vr_gl_fill_model_at can copy. */
int vr_gl_can_hide_parts(void);

/* Up to four models a frame between them; this forgets them all. */
void vr_gl_clear_models(void);

/*
 * How far from the middle of the window a 2D thing must be drawn to appear in a
 * given direction from the eye, once the 2D has been mapped into the HUD panel.
 * The tangents are from the eye's own axis, as vr_gl_eye_begin takes them, and
 * the answer is in the window coordinates ref_gl draws 2D in, y downwards. That
 * is how the crosshair follows the gun. Returns 0 if no eye is being drawn.
 */
int vr_gl_panel_offset(float tan_x, float tan_y, float *dx, float *dy);

/*
 * Draws something of this mod's own in world coordinates, after ref_gl has
 * finished with an eye and while its framebuffer is still bound: the eye's own
 * frustum and the view the refdef holds are set up again, the whole eye is the
 * viewport, and texturing, the depth test and the scissor are off, so plain
 * coloured lines land where the world is and nothing is hidden behind it.
 * Returns 0 if no eye is being drawn, or if ref_gl never asked for one of the
 * GL entry points this needs, in which case nothing was changed.
 * vr_gl_world_end puts it all back.
 */
int vr_gl_world_begin(const float vieworg[3], const float viewangles[3]);
void vr_gl_world_end(void);

/*
 * While an eye is rendered, ref_gl's viewports and scissors, which are in window
 * coordinates, are mapped onto the eye's target - the whole of it for the world,
 * the HUD panel for 2D - and every projection it sets up is replaced by the eye's
 * asymmetric frustum. Tangents are of the angles from the view axis to each edge:
 * left and down negative. offset_x is how far this eye is to the right of the
 * head, in metres, which is what gives the panel its depth.
 */
void vr_gl_eye_begin(int target_width, int target_height, int window_width, int window_height,
	float tan_left, float tan_right, float tan_down, float tan_up, float offset_x);
void vr_gl_eye_end(void);

#endif /* VR_GL_H */
