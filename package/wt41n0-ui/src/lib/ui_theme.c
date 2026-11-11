/*
 * Theme (SPEC.md, Colour tokens, Typography, Theming in code): three settings in
 * /etc/wt41n0/ui.conf, "style=", "palette=" and "accent=". Palette and accent may be
 * "auto", resolving to the style's defaults (WT_STYLE_DEFAULTS). NUKE derives its
 * neutrals from the accent and takes only the background from the palette.
 *
 * Fonts per style and role come from the typography table; the faces are TTF files in
 * /usr/share/wt41n0/fonts (WT41N0_UI_FONTS), rendered by LVGL's TinyTTF. Share Tech Mono
 * is also built in, as the fallback; LVGL's Montserrat supplies the LV_SYMBOL_* icons.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui_internal.h"

extern const unsigned char ui_font_ttf[];	/* ShareTechMono-Regular.ttf, embedded */
extern const unsigned long ui_font_ttf_size;

#define N(a) (sizeof(a) / sizeof((a)[0]))

static const char *const style_names[] = { "cyber", "aero", "deck", "nuke", NULL };
static const char *const palette_names[] = { "night", "void", "dusk", "aero", "tape", NULL };
static const char *const accent_names[] = { "cyan", "magenta", "green", "amber", "red", NULL };
static const wt_palette_t palettes[] = { WT_PAL_NIGHT, WT_PAL_VOID, WT_PAL_DUSK, WT_PAL_AERO, WT_PAL_TAPE };
static const wt_accent_t accents[] = { WT_ACC_CYAN, WT_ACC_MAGENTA, WT_ACC_GREEN, WT_ACC_AMBER, WT_ACC_RED };
static const struct { wt_palette_id_t palette; wt_accent_id_t accent; } defaults[] = WT_STYLE_DEFAULTS;
static const wt_style_metrics_t metrics[] = WT_STYLE_METRICS;

/* the three settings: style index, palette and accent as id (0 = AUTO) */
static wt_style_t cur_style;
static wt_palette_id_t sel_palette;
static wt_accent_id_t sel_accent;
static int res_palette, res_accent;	/* resolved indexes into palettes[] / accents[] */
static ui_palette_t current;

static lv_color_t over(lv_color_t c, lv_opa_t opa, lv_color_t under)
{
	return lv_color_mix(c, under, opa);
}

static void apply(void)
{
	const wt_palette_t *p;
	const wt_accent_t *a;

	res_palette = (sel_palette == WT_PALETTE_AUTO ? defaults[cur_style].palette : sel_palette) - 1;
	res_accent = (sel_accent == WT_ACCENT_AUTO ? defaults[cur_style].accent : sel_accent) - 1;
	p = &palettes[res_palette];
	a = &accents[res_accent];

	current.bg = lv_color_hex(p->bg);
	current.accent = lv_color_hex(a->accent);
	current.warn = lv_color_hex(a->warn);
	if (cur_style == WT_STYLE_NUKE) {
		/* single hue: everything is a tint of the accent over the background */
		current.text = current.accent;
		current.dim = over(current.accent, WT_OPA_NUKE_DIM, current.bg);
		current.line = over(current.accent, WT_OPA_NUKE_LINE, current.bg);
		current.panel = over(current.accent, WT_OPA_NUKE_SURFACE, current.bg);
		current.highlight = current.line;
		current.dark = current.bg;
		current.accent2 = current.accent;
		current.accent_fill = over(current.accent, WT_OPA_NUKE_FILL, current.bg);
	} else {
		current.panel = lv_color_hex(p->surface);
		current.line = lv_color_hex(p->line);
		current.text = lv_color_hex(p->text);
		current.dim = lv_color_hex(p->dim);
		current.highlight = lv_color_hex(p->highlight);
		current.dark = lv_color_hex(p->dark);
		current.accent2 = lv_color_hex(a->accent2);
		current.accent_fill = over(current.accent, WT_OPA_ACCENT_FILL, current.panel);
	}
	current.accent_pressed = over(current.accent, WT_OPA_ACCENT_PRESSED, current.panel);
	current.glow = over(current.accent, WT_OPA_ACCENT_GLOW, current.bg);
	current.text_hilight = over(current.text, WT_OPA_TEXT_HILIGHT, current.panel);
	current.text_edge = over(current.text, WT_OPA_TEXT_EDGE, current.bg);
	current.text_bright = over(current.text, WT_OPA_TEXT_BRIGHT, current.bg);
	current.text_tick = over(current.text, WT_OPA_TEXT_TICK, current.panel);
	current.text_white = over(current.text, WT_OPA_TEXT_WHITE, current.panel);
}

static int find(const char *const *names, const char *name)
{
	int i;

	for (i = 0; names[i]; i++)
		if (!strcmp(names[i], name))
			return i;
	return -1;
}

static void load_conf(void)
{
	char v[32];
	int i;

	cur_style = WT_STYLE_CYBER;
	sel_palette = WT_PALETTE_AUTO;
	sel_accent = WT_ACCENT_AUTO;
	if (!conf_get("style", v, sizeof(v)) && (i = find(style_names, v)) >= 0)
		cur_style = (wt_style_t)i;
	if (!conf_get("palette", v, sizeof(v)) && (i = find(palette_names, v)) >= 0)
		sel_palette = (wt_palette_id_t)(i + 1);
	if (!conf_get("accent", v, sizeof(v)) && (i = find(accent_names, v)) >= 0)
		sel_accent = (wt_accent_id_t)(i + 1);
}

void theme_init(void)
{
	load_conf();
	apply();
}

/* NULL keeps a setting; "auto" for palette or accent follows the style */
void ui_theme_set(const char *style, const char *palette, const char *accent, bool save)
{
	int i;

	if (style && (i = find(style_names, style)) >= 0)
		cur_style = (wt_style_t)i;
	if (palette)
		sel_palette = !strcmp(palette, "auto") ? WT_PALETTE_AUTO :
			      (i = find(palette_names, palette)) >= 0 ? (wt_palette_id_t)(i + 1) : sel_palette;
	if (accent)
		sel_accent = !strcmp(accent, "auto") ? WT_ACCENT_AUTO :
			     (i = find(accent_names, accent)) >= 0 ? (wt_accent_id_t)(i + 1) : sel_accent;
	apply();
	if (save) {
		conf_set("style", style_names[cur_style]);
		conf_set("palette", sel_palette == WT_PALETTE_AUTO ? "auto" : palette_names[sel_palette - 1]);
		conf_set("accent", sel_accent == WT_ACCENT_AUTO ? "auto" : accent_names[sel_accent - 1]);
	}
}

void ui_set_theme(const char *palette, const char *accent)
{
	ui_theme_set(NULL, palette, accent, true);
}

const ui_palette_t *ui_palette(void) { return &current; }
const char *const *ui_style_names(void) { return style_names; }
const char *const *ui_palette_names(void) { return palette_names; }
const char *const *ui_accent_names(void) { return accent_names; }
wt_style_t ui_theme_style(void) { return cur_style; }
const char *ui_theme_style_name(void) { return style_names[cur_style]; }
const char *ui_theme_palette(void) { return palette_names[res_palette]; }
const char *ui_theme_accent(void) { return accent_names[res_accent]; }
const char *ui_theme_palette_choice(void)
{
	return sel_palette == WT_PALETTE_AUTO ? "auto" : palette_names[sel_palette - 1];
}
const char *ui_theme_accent_choice(void)
{
	return sel_accent == WT_ACCENT_AUTO ? "auto" : accent_names[sel_accent - 1];
}
const wt_style_metrics_t *ui_style_metrics(void) { return &metrics[cur_style]; }

/* ---- fonts ---------------------------------------------------------------------- */

enum face { F_STM, F_AUDIOWIDE, F_CHAKRA, F_ALDRICH, F_SPACE, F_SPACE_BOLD, F_DOT, F_FACES };

static const char *const face_files[F_FACES] = {
	NULL,	/* built in */
	"Audiowide-Regular.ttf", "ChakraPetch-SemiBold.ttf", "Aldrich-Regular.ttf",
	"SpaceMono-Regular.ttf", "SpaceMono-Bold.ttf", "DotGothic16-Regular.ttf",
};

static struct { const void *data; size_t size; bool tried; } faces[F_FACES];

/* role -> face, size, tracking, per style (SPEC.md, Typography) */
struct role_font { uint8_t face, size, track; };
static const struct role_font roles[4][UI_ROLE_COUNT] = {
	/* title, name, data, hint, big, key, key small, body */
	[WT_STYLE_CYBER] = { { F_STM, 16, 1 }, { F_STM, 14, 1 }, { F_STM, 12, 0 }, { F_STM, 12, 1 },
			     { F_STM, 28, 0 }, { F_STM, 16, 0 }, { F_STM, 12, 0 }, { F_STM, 14, 0 } },
	[WT_STYLE_AERO] = { { F_AUDIOWIDE, 12, 1 }, { F_AUDIOWIDE, 12, 0 }, { F_CHAKRA, 12, 0 },
			    { F_CHAKRA, 12, 0 }, { F_AUDIOWIDE, 26, 0 }, { F_CHAKRA, 14, 0 },
			    { F_CHAKRA, 12, 0 }, { F_CHAKRA, 14, 0 } },
	[WT_STYLE_DECK] = { { F_ALDRICH, 14, 1 }, { F_ALDRICH, 13, 1 }, { F_SPACE, 12, 0 },
			    { F_SPACE, 12, 0 }, { F_SPACE_BOLD, 28, 0 }, { F_SPACE_BOLD, 16, 0 },
			    { F_SPACE_BOLD, 12, 0 }, { F_SPACE, 14, 0 } },
	[WT_STYLE_NUKE] = { { F_DOT, 16, 0 }, { F_DOT, 16, 0 }, { F_STM, 12, 0 }, { F_STM, 12, 0 },
			    { F_DOT, 32, 0 }, { F_DOT, 16, 0 }, { F_DOT, 12, 0 }, { F_STM, 14, 0 } },
};

static void load_face(int f)
{
	const char *dir = getenv("WT41N0_UI_FONTS");
	char path[256];
	FILE *fp;
	long n;
	void *buf;

	faces[f].tried = true;
	if (f == F_STM) {
		faces[f].data = ui_font_ttf;
		faces[f].size = ui_font_ttf_size;
		return;
	}
	snprintf(path, sizeof(path), "%s/%s", dir ? dir : "/usr/share/wt41n0/fonts", face_files[f]);
	fp = fopen(path, "rb");
	if (!fp)
		return;
	if (fseek(fp, 0, SEEK_END) == 0 && (n = ftell(fp)) > 0 && fseek(fp, 0, SEEK_SET) == 0 &&
	    (buf = malloc((size_t)n)) != NULL) {
		if (fread(buf, 1, (size_t)n, fp) == (size_t)n) {
			faces[f].data = buf;
			faces[f].size = (size_t)n;
		} else {
			free(buf);
		}
	}
	fclose(fp);
}

static const lv_font_t *symbols_for(int size)
{
	return size <= 12 ? &lv_font_montserrat_12 : size >= 28 ? &lv_font_montserrat_28 :
	       size >= 18 ? &lv_font_montserrat_18 : &lv_font_montserrat_14;
}

/* a face at a size, created once; falls back to Share Tech Mono */
static const lv_font_t *face_font(int f, int size)
{
	static struct { int face, size; lv_font_t *font; } cache[40];
	static int n;
	int i;

	for (i = 0; i < n; i++)
		if (cache[i].face == f && cache[i].size == size)
			return cache[i].font;
	if (!faces[f].tried)
		load_face(f);
	if (!faces[f].data)
		return f == F_STM ? &lv_font_montserrat_14 : face_font(F_STM, size);
	if (n == (int)N(cache))
		return &lv_font_montserrat_14;
	cache[n].face = f;
	cache[n].size = size;
	/* no kerning: TinyTTF looks it up for every glyph on every draw, which was three
	 * quarters of the drawing time on the unit; the UI's capitals do not need it */
	cache[n].font = lv_tiny_ttf_create_data_ex(faces[f].data, faces[f].size, size, LV_FONT_KERNING_NONE,
						   LV_TINY_TTF_CACHE_GLYPH_CNT);
	if (cache[n].font)
		cache[n].font->fallback = symbols_for(size);
	else
		cache[n].font = (lv_font_t *)symbols_for(size);
	return cache[n++].font;
}

void fonts_init(void)
{
}

/* the body face of the current style at a size */
const lv_font_t *ui_font(int px)
{
	return face_font(roles[cur_style][UI_ROLE_BODY].face, px);
}

const lv_font_t *ui_font_icons(void) { return &lv_font_montserrat_28; }
const lv_font_t *ui_font_icons24(void) { return &lv_font_montserrat_24; }

/*
 * Vertical padding for a label, allowing for its face. Aldrich's capitals reach 2 px
 * above its line box, and the box keeps 4 px of descender room below them that
 * upper-case text never uses: with an even pad the text sits at the top of a label's
 * background (DECK's chips and name strips). So Aldrich gets 4 px more at the top and
 * 2 px less at the bottom: the box is 2 px taller than its line, the capitals centred.
 */
void ui_text_pad_ver(lv_obj_t *label, int pad)
{
	const lv_font_t *f = lv_obj_get_style_text_font(label, 0);
	bool aldrich = cur_style == WT_STYLE_DECK &&
		       (f == face_font(F_ALDRICH, 13) || f == face_font(F_ALDRICH, 14));

	lv_obj_set_style_pad_top(label, pad + (aldrich ? 4 : 0), 0);
	lv_obj_set_style_pad_bottom(label, pad - (aldrich ? 2 : 0), 0);
}

const lv_font_t *ui_font_role(ui_role_t role)
{
	const struct role_font *r = &roles[cur_style][role];

	return face_font(r->face, r->size);
}

void ui_text_role(lv_obj_t *label, ui_role_t role)
{
	lv_obj_set_style_text_font(label, ui_font_role(role), 0);
	lv_obj_set_style_text_letter_space(label, roles[cur_style][role].track, 0);
	if (roles[cur_style][role].face == F_ALDRICH)
		ui_text_pad_ver(label, 0);
}
