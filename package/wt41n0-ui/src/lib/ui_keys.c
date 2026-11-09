/*
 * Keypad: the same layers as the console keymap (support/keymap.py, generated into
 * keymap.h). The big GRAY and big ORANGE keys and BLUE are sticky: one press applies
 * to the next key, a second press locks the layer, a third unlocks it. ORANGE + GRAY
 * gives the punctuation layer. SHIFT acts as GRAY while held.
 */
#ifdef __linux__
#include <linux/input-event-codes.h>
#else
#define KEY_MAX 0x2ff		/* host harness */
#endif
#include "ui_internal.h"
#include "keymap.h"

enum { OFF, ONCE, LOCKED };

static int gray, orange, blue;		/* OFF / ONCE / LOCKED */
static void (*back_cb)(void);
static void (*power_cb)(void);	/* the power button (KEY_POWER) */
#ifndef KEY_POWER
#define KEY_POWER 116		/* the host harness's key-code header lacks it */
#endif
static unsigned int back_code;		/* Esc press handled as "back": swallow its release */
static bool shift_held;
static lv_group_t *keys_group;
static uint32_t pressed_as[KEY_MAX + 1];	/* LVGL key sent for each pressed code */

#define QUEUE_LEN 32
static struct { uint32_t key; lv_indev_state_t state; } queue[QUEUE_LEN];
static unsigned int q_head, q_tail;

static void push(uint32_t key, lv_indev_state_t state)
{
	unsigned int next = (q_head + 1) % QUEUE_LEN;

	if (next == q_tail)
		return;		/* full: drop */
	queue[q_head].key = key;
	queue[q_head].state = state;
	q_head = next;
}

static void read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
	static uint32_t last_key;

	(void)indev;
	if (q_tail == q_head) {
		data->key = last_key;
		data->state = LV_INDEV_STATE_RELEASED;
		return;
	}
	data->key = last_key = queue[q_tail].key;
	data->state = queue[q_tail].state;
	q_tail = (q_tail + 1) % QUEUE_LEN;
	data->continue_reading = q_tail != q_head;
}

static void cycle(int *layer)
{
	*layer = *layer == OFF ? ONCE : *layer == ONCE ? LOCKED : OFF;
}

/*
 * The LVGL key for a symbol; 0 = nothing to send. LVGL's keypad mode sends keys to
 * the focused widget and moves the focus on NEXT/PREV. Up and Down move the focus,
 * except on the on-screen keyboard (rows; 4 and 6 move left and right, the plain
 * keypad has no Left/Right keys) and on a slider being adjusted (Enter starts and
 * ends adjusting; the group's "editing" flag marks it).
 */
static uint32_t lv_key_for(int sym)
{
	lv_obj_t *f = keys_group ? lv_group_get_focused(keys_group) : NULL;
	bool kb = f && lv_obj_check_type(f, &lv_keyboard_class);
	bool slider = f && lv_obj_check_type(f, &lv_slider_class);
	/* "editing": a slider being adjusted, a slider card adjusting, a button armed */
	bool adjusting = f && lv_group_get_editing(keys_group);
	bool arrows = kb || adjusting;

	if (kb && (sym == '4' || sym == '6'))
		return sym == '4' ? LV_KEY_LEFT : LV_KEY_RIGHT;
	if (sym > 0)
		return (uint32_t)sym;
	switch (sym) {
	case UI_SYM_ESC:	return LV_KEY_ESC;
	case UI_SYM_TAB:	return LV_KEY_NEXT;
	case UI_SYM_BACKSPACE:	return LV_KEY_BACKSPACE;
	case UI_SYM_ENTER:
		if (slider) {
			lv_group_set_editing(keys_group, !adjusting);
			return 0;
		}
		return LV_KEY_ENTER;
	case UI_SYM_UP:		return arrows ? LV_KEY_UP : LV_KEY_PREV;
	case UI_SYM_DOWN:	return arrows ? LV_KEY_DOWN : LV_KEY_NEXT;
	case UI_SYM_LEFT:	return arrows ? LV_KEY_LEFT : LV_KEY_PREV;
	case UI_SYM_RIGHT:	return arrows ? LV_KEY_RIGHT : LV_KEY_NEXT;
	default:		return 0;	/* F1..F10, Ctrl: unused */
	}
}

/* Esc: closes the on-screen keyboard, ends adjusting a slider, cancels what the focused
 * element is doing (a slider card adjusting, a button armed: it gets the ESC key and
 * clears the editing flag itself), else goes back */
static bool handle_esc(void)
{
	lv_obj_t *f = keys_group ? lv_group_get_focused(keys_group) : NULL;

	if (f && lv_obj_check_type(f, &lv_keyboard_class)) {
		lv_obj_send_event(f, LV_EVENT_CANCEL, NULL);
		return true;
	}
	if (f && lv_obj_check_type(f, &lv_slider_class) && lv_group_get_editing(keys_group)) {
		lv_group_set_editing(keys_group, false);
		return true;
	}
	if (f && lv_group_get_editing(keys_group)) {
		uint32_t esc = LV_KEY_ESC;

		lv_obj_send_event(f, LV_EVENT_KEY, &esc);
		lv_group_set_editing(keys_group, false);
		return true;
	}
	if (back_cb) {
		ui_screen_transition(UI_SLIDE_OUT);
		back_cb();
		ui_screen_transition(UI_SLIDE_IN);
		return true;
	}
	return false;
}

void keys_event(unsigned int code, int value)
{
	const struct ui_key *k = NULL;
	unsigned int i;
	int layer, sym;
	uint32_t key;

	if (code == KEY_POWER) {		/* not in the keymap: the app decides */
		if (value == 1 && power_cb)
			power_cb();
		return;
	}
	for (i = 0; i < sizeof(ui_keymap) / sizeof(ui_keymap[0]); i++)
		if (ui_keymap[i].code == code)
			k = &ui_keymap[i];
	if (!k || code > KEY_MAX)
		return;
	sym = k->sym[UI_LAYER_PLAIN];

	switch (sym) {
	case UI_SYM_SHIFT:
		shift_held = value != 0;
		return;
	case UI_SYM_GRAY:
	case UI_SYM_ORANGE:
	case UI_SYM_BLUE:
		if (value == 1) {
			cycle(sym == UI_SYM_GRAY ? &gray : sym == UI_SYM_ORANGE ? &orange : &blue);
			status_refresh();
		}
		return;
	case UI_SYM_CTRL:
		return;
	}

	if (value == 0) {
		if (code == back_code) {
			back_code = 0;
			return;
		}
		if (pressed_as[code])
			push(pressed_as[code], LV_INDEV_STATE_RELEASED);
		pressed_as[code] = 0;
		return;
	}
	if (value == 2) {		/* autorepeat: only the navigation keys repeat */
		key = pressed_as[code];
		if (key == LV_KEY_PREV || key == LV_KEY_NEXT || key == LV_KEY_UP ||
		    key == LV_KEY_DOWN || key == LV_KEY_LEFT || key == LV_KEY_RIGHT ||
		    key == LV_KEY_BACKSPACE) {
			push(key, LV_INDEV_STATE_RELEASED);
			push(key, LV_INDEV_STATE_PRESSED);
		}
		return;
	}

	if (blue)
		layer = UI_LAYER_BLUE;
	else if (orange && (gray || shift_held))
		layer = UI_LAYER_BOTH;
	else if (orange)
		layer = UI_LAYER_ORANGE;
	else if (gray || shift_held)
		layer = UI_LAYER_GRAY;
	else
		layer = UI_LAYER_PLAIN;
	key = lv_key_for(k->sym[layer]);
	if (key == LV_KEY_ESC && handle_esc()) {
		back_code = code;	/* swallow its release too */
		return;
	}
	/* one-shot layers end with the key they applied to */
	if (gray == ONCE)
		gray = OFF;
	if (orange == ONCE)
		orange = OFF;
	if (blue == ONCE)
		blue = OFF;
	status_refresh();
	if (!key)
		return;
	pressed_as[code] = key;
	push(key, LV_INDEV_STATE_PRESSED);
}

void ui_on_power(void (*cb)(void))
{
	power_cb = cb;
}

void keys_set_back(void (*cb)(void))
{
	back_cb = cb;
}

void ui_on_back(void (*cb)(void))
{
	back_cb = cb;
}

int ui_key_layers(void)
{
	return (gray ? UI_LAYERS_GRAY : 0) | (orange ? UI_LAYERS_ORANGE : 0) |
	       (blue ? UI_LAYERS_BLUE : 0) |
	       (gray == LOCKED || orange == LOCKED || blue == LOCKED ? UI_LAYERS_LOCKED : 0);
}

void keys_init(lv_group_t *group)
{
	lv_indev_t *indev = lv_indev_create();

	keys_group = group;
	lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
	lv_indev_set_read_cb(indev, read_cb);
	lv_indev_set_group(indev, group);
}
