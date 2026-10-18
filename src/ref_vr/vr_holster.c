#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "ref_vr.h"
#include "vr_holster.h"
#include "vrlog.h"
#include "../shared/sinvr_poses.h"

/*
 * The holster menus (vr_holster.h). The game sends what the player carries
 * every server frame; a menu is a row of the HUD pictures of one list, drawn
 * over the HUD panel in front of the player, the one picked bigger and named
 * underneath. Taking one is "use <name>", which switches to a weapon and - in
 * this mod's game code - uses an inventory item.
 */

#define STALE_MS 1000        /* the game has stopped sending: a level change, a menu */
#define ICON 40              /* in the HUD's 2D units, which are the window's */
#define PICKED 64            /* the one picked */
#define GAP 12
#define VISIBLE 7            /* the row is this many wide, round the one picked */
#define BACK_COLOUR 0        /* a palette index: black */

static sinvr_holsters_t latest;
static DWORD latest_at;
static int have_latest;

static struct
{
	int kind;
	int picked;
	int count;
	int current;
	sinvr_holster_entry_t entries[SINVR_HOLSTER_MAX];
} menu;

static vr_draw_stretch_pic_t stretch_pic;
static vr_draw_char_t draw_char;
static vr_draw_fill_t fill;
static vr_draw_get_pic_size_t get_pic_size;
static int said_list;
static cvar_t *vr_fake_holster;

__declspec(dllexport) void SinVrHolsters(const sinvr_holsters_t *holsters)
{
	if (!holsters || holsters->version != SINVR_HOLSTERS_VERSION)
	{
		return;
	}

	latest = *holsters;
	latest.weapon_count = latest.weapon_count < 0 ? 0 :
		(latest.weapon_count > SINVR_HOLSTER_MAX ? SINVR_HOLSTER_MAX : latest.weapon_count);
	latest.item_count = latest.item_count < 0 ? 0 :
		(latest.item_count > SINVR_HOLSTER_MAX ? SINVR_HOLSTER_MAX : latest.item_count);
	latest_at = GetTickCount();
	have_latest = 1;

	if (!said_list)
	{
		said_list = 1;
		vrlog("VR: the game is sending what the player carries: %d weapons, %d items",
			latest.weapon_count, latest.item_count);
	}
}

void vr_holster_startup(void)
{
	vr_fake_holster = vr_cvar("vr_fake_holster", "", 0);
}

/* Desk testing, with no hands to hold a grip: "vr_fake_holster weapons" or
   "items" opens that menu, "next" steps, "take" and "close" close it. */
static void fake_holster(void)
{
	const char *s = vr_fake_holster ? vr_fake_holster->string : NULL;

	if (!s || !s[0])
	{
		return;
	}
	vrlog("VR: vr_fake_holster %s", s);
	if (_stricmp(s, "weapons") == 0)
	{
		vr_holster_open(VR_HOLSTER_WEAPONS);
	}
	else if (_stricmp(s, "items") == 0)
	{
		vr_holster_open(VR_HOLSTER_ITEMS);
	}
	else if (_stricmp(s, "next") == 0)
	{
		vr_holster_next();
	}
	else
	{
		vr_holster_close(_stricmp(s, "take") == 0);
	}
	vr_cvar_set("vr_fake_holster", "");
}

void vr_holster_set_draw(vr_draw_stretch_pic_t s, vr_draw_char_t c, vr_draw_fill_t f, vr_draw_get_pic_size_t g)
{
	stretch_pic = s;
	draw_char = c;
	fill = f;
	get_pic_size = g;
}

int vr_holster_open(int kind)
{
	const sinvr_holster_entry_t *from;
	int count, current, i;

	if (!have_latest || GetTickCount() - latest_at > STALE_MS)
	{
		vrlog("VR: no %s menu - the game has not said what the player carries for %u ms",
			kind == VR_HOLSTER_WEAPONS ? "weapon" : "inventory", have_latest ? GetTickCount() - latest_at : 0u);
		return 0;
	}

	if (kind == VR_HOLSTER_WEAPONS)
	{
		from = latest.weapons;
		count = latest.weapon_count;
		current = latest.current_weapon;
	}
	else
	{
		from = latest.items;
		count = latest.item_count;
		current = latest.current_item;
	}
	if (count <= 0)
	{
		vrlog("VR: no %s menu - the player carries none", kind == VR_HOLSTER_WEAPONS ? "weapon" : "inventory");
		return 0;
	}

	menu.kind = kind;
	menu.count = count;
	for (i = 0; i < count; ++i)
	{
		menu.entries[i] = from[i];
		menu.entries[i].name[sizeof(menu.entries[i].name) - 1] = '\0';
		menu.entries[i].icon[sizeof(menu.entries[i].icon) - 1] = '\0';
	}
	menu.current = current >= 0 && current < count ? current : -1;
	menu.picked = menu.current >= 0 ? menu.current : 0;
	vrlog("VR: the %s menu is open (%d), on %s", kind == VR_HOLSTER_WEAPONS ? "weapon" : "inventory", count,
		menu.entries[menu.picked].name);
	return 1;
}

int vr_holster_open_kind(void)
{
	return menu.kind;
}

void vr_holster_next(void)
{
	if (menu.kind && menu.count > 0)
	{
		menu.picked = (menu.picked + 1) % menu.count;
	}
}

void vr_holster_close(int take)
{
	char command[64];
	const sinvr_holster_entry_t *e;

	if (!menu.kind)
	{
		return;
	}

	e = &menu.entries[menu.picked];
	if (take && menu.picked >= 0 && menu.picked < menu.count && e->name[0] &&
		!(menu.kind == VR_HOLSTER_WEAPONS && menu.picked == menu.current))
	{
		sprintf(command, "use %.40s", e->name);
		vr_command(command);
		vrlog("VR: from the %s menu: %s", menu.kind == VR_HOLSTER_WEAPONS ? "weapon" : "inventory", command);
	}
	else
	{
		vrlog("VR: the %s menu closed with nothing changed", menu.kind == VR_HOLSTER_WEAPONS ? "weapon" : "inventory");
	}
	menu.kind = 0;
}

static void draw_text(int x, int y, const char *text)
{
	for (; *text; ++text, x += 8)
	{
		draw_char(x, y, (unsigned char)*text);
	}
}

static void draw_centred(int centre, int y, const char *text)
{
	draw_text(centre - (int)strlen(text) * 4, y, text);
}

void vr_holster_draw(int width, int height)
{
	int shown, first, i, x, y0, row;
	char label[64];

	fake_holster();
	if (!menu.kind || !stretch_pic || !draw_char || !fill || !get_pic_size || width <= 0 || height <= 0)
	{
		return;
	}

	shown = menu.count < VISIBLE ? menu.count : VISIBLE;
	first = menu.picked - shown / 2;
	if (first < 0)
	{
		first = 0;
	}
	if (first + shown > menu.count)
	{
		first = menu.count - shown;
	}

	/* the one picked is drawn bigger than the rest, on their middle line */
	row = (shown - 1) * ICON + PICKED + (shown - 1) * GAP;
	x = (width - row) / 2;
	y0 = height / 2 - 60;

	fill(x - 16, y0 - PICKED / 2 - 26, row + 32, PICKED + 52, BACK_COLOUR);
	draw_centred(width / 2, y0 - PICKED / 2 - 16, menu.kind == VR_HOLSTER_WEAPONS ? "WEAPONS" : "INVENTORY");

	for (i = 0; i < shown; ++i)
	{
		const sinvr_holster_entry_t *e = &menu.entries[first + i];
		int size = first + i == menu.picked ? PICKED : ICON;
		int w = 0, h = 0;

		if (e->icon[0])
		{
			get_pic_size(&w, &h, e->icon);
		}
		if (w > 0 && h > 0)
		{
			stretch_pic(x, y0 - size / 2, size, size, e->icon);
		}
		else
		{
			char shortname[7];

			strncpy(shortname, e->name, 6);
			shortname[6] = '\0';
			draw_centred(x + size / 2, y0 - 4, shortname);
		}
		x += size + GAP;
	}

	if (menu.kind == VR_HOLSTER_ITEMS && menu.entries[menu.picked].amount > 1)
	{
		sprintf(label, "> %.40s  %d <", menu.entries[menu.picked].name, menu.entries[menu.picked].amount);
	}
	else
	{
		sprintf(label, "> %.40s <", menu.entries[menu.picked].name);
	}
	draw_centred(width / 2, y0 + PICKED / 2 + 8, label);
}