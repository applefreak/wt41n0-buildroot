/*
 * Theme: a palette (backgrounds and text) plus an accent colour, chosen in
 * /etc/wt41n0/ui.conf ("palette=" and "accent=" lines) and changed by Settings.
 * Text uses Share Tech Mono through LVGL's TinyTTF renderer, with LVGL's Montserrat
 * fonts as fallback for the LV_SYMBOL_* icons.
 */
#include <stdio.h>
#include <string.h>
#include "ui_internal.h"

extern const unsigned char ui_font_ttf[];	/* ShareTechMono-Regular.ttf, embedded */
extern const unsigned long ui_font_ttf_size;

static const struct palette {
	const char *name;
	uint32_t bg, panel, line, text, dim;
} palettes[] = {
	{ "night", 0x05070d, 0x0b1020, 0x1d2742, 0xe4f0ff, 0x6f7d9c },
	{ "void",  0x000000, 0x080808, 0x1e1e1e, 0xf0f0f0, 0x777777 },
	{ "dusk",  0x0a0512, 0x150b24, 0x2d1a4a, 0xf2e8ff, 0x8a74ad },
};

static const struct accent {
	const char *name;
	uint32_t color, second;
} accents[] = {
	{ "cyan",    0x00e5ff, 0xff2bd6 },
	{ "magenta", 0xff2bd6, 0x00e5ff },
	{ "green",   0x39ff6a, 0x00e5ff },
	{ "amber",   0xffb000, 0xff3d6e },
	{ "red",     0xff3355, 0xffb000 },
};

#define N(a) (sizeof(a) / sizeof((a)[0]))

static const char *palette_names[N(palettes) + 1];
static const char *accent_names[N(accents) + 1];
static unsigned int cur_palette, cur_accent;
static ui_palette_t current;
static lv_font_t *mono[3];		/* 12, 16, 20 px */

static void apply(void)
{
	const struct palette *p = &palettes[cur_palette];
	const struct accent *a = &accents[cur_accent];

	current.bg = lv_color_hex(p->bg);
	current.panel = lv_color_hex(p->panel);
	current.line = lv_color_hex(p->line);
	current.text = lv_color_hex(p->text);
	current.dim = lv_color_hex(p->dim);
	current.accent = lv_color_hex(a->color);
	current.accent2 = lv_color_hex(a->second);
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
	char value[64];
	int i;

	if (!conf_get("palette", value, sizeof(value)) && (i = find(palette_names, value)) >= 0)
		cur_palette = i;
	if (!conf_get("accent", value, sizeof(value)) && (i = find(accent_names, value)) >= 0)
		cur_accent = i;
}

static void save_conf(void)
{
	conf_set("palette", palettes[cur_palette].name);
	conf_set("accent", accents[cur_accent].name);
}

void theme_init(void)
{
	unsigned int i;

	for (i = 0; i < N(palettes); i++)
		palette_names[i] = palettes[i].name;
	for (i = 0; i < N(accents); i++)
		accent_names[i] = accents[i].name;
	load_conf();
	apply();
}

void ui_set_theme(const char *palette, const char *accent)
{
	int i;

	if (palette && (i = find(palette_names, palette)) >= 0)
		cur_palette = i;
	if (accent && (i = find(accent_names, accent)) >= 0)
		cur_accent = i;
	apply();
	save_conf();
}

const ui_palette_t *ui_palette(void) { return &current; }
const char *const *ui_accent_names(void) { return accent_names; }
const char *const *ui_palette_names(void) { return palette_names; }
const char *ui_theme_accent(void) { return accents[cur_accent].name; }
const char *ui_theme_palette(void) { return palettes[cur_palette].name; }

void fonts_init(void)
{
	static const int sizes[3] = { 12, 16, 20 };
	static const lv_font_t *const symbols[3] = {
		&lv_font_montserrat_12, &lv_font_montserrat_14, &lv_font_montserrat_14,
	};
	int i;

	for (i = 0; i < 3; i++) {
		mono[i] = lv_tiny_ttf_create_data(ui_font_ttf, ui_font_ttf_size, sizes[i]);
		if (mono[i])
			mono[i]->fallback = symbols[i];
	}
}

const lv_font_t *ui_font(int px)
{
	int i = px <= 12 ? 0 : px <= 16 ? 1 : 2;

	return mono[i] ? mono[i] : &lv_font_montserrat_14;
}

const lv_font_t *ui_font_icons(void)
{
	return &lv_font_montserrat_28;
}
