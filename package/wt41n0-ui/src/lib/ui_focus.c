/*
 * Focus model (SPEC.md, Animation): one focus ring per screen, the accent border with
 * the glow line outside it, moved to the focused element over WT_ANIM_MS (ease-out),
 * and screens sliding in on ENT and out on ESC.
 */
#include "ui_internal.h"

static lv_obj_t *ring;			/* the current screen's focus ring */
static bool ring_override;		/* a colour set by ui_focus_ring_color() */
static lv_color_t ring_color;
static lv_area_t ring_from, ring_to;
static enum ui_transition next_transition = UI_SLIDE_NONE;

/* A screen being built: focus changes until it is done (the first element added to the
 * group takes the focus by itself, then the screen focuses the one it wants) place the
 * ring at once, so it does not glide over from the top-left element. Cleared on the
 * next timer pass, after the building code has returned. */
static bool ring_settling;

static void ring_settled(void *arg)
{
	(void)arg;
	ring_settling = false;
}

static void ring_deleted(lv_event_t *e)
{
	if (lv_event_get_target(e) == ring)
		ring = NULL;
}

void focus_ring_create(lv_obj_t *scr)
{
	ring = lv_obj_create(scr);
	lv_obj_remove_style_all(ring);
	ring_override = false;
	lv_obj_add_flag(ring, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_HIDDEN);
	lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_event_cb(ring, ring_deleted, LV_EVENT_DELETE, NULL);
	ring_settling = true;
	lv_async_call(ring_settled, NULL);
}

/* areas are screen-relative: the screen itself moves while it slides in */
static void screen_relative(lv_obj_t *obj, lv_area_t *a)
{
	lv_area_t s;

	lv_obj_get_coords(obj, a);
	lv_obj_get_coords(lv_obj_get_screen(obj), &s);
	lv_area_move(a, -s.x1, -s.y1);
}

static void ring_set(const lv_area_t *a)
{
	lv_obj_set_pos(ring, a->x1, a->y1);
	lv_obj_set_size(ring, lv_area_get_width(a), lv_area_get_height(a));
}

static void ring_anim_cb(void *var, int32_t v)
{
	lv_area_t a;

	(void)var;
	a.x1 = ring_from.x1 + (ring_to.x1 - ring_from.x1) * v / 1024;
	a.y1 = ring_from.y1 + (ring_to.y1 - ring_from.y1) * v / 1024;
	a.x2 = ring_from.x2 + (ring_to.x2 - ring_from.x2) * v / 1024;
	a.y2 = ring_from.y2 + (ring_to.y2 - ring_from.y2) * v / 1024;
	if (ring)
		ring_set(&a);
}

/* elements that show focus their own way (a key of the keyboard, the old sliders) */
static bool ring_skips(lv_obj_t *obj)
{
	return lv_obj_check_type(obj, &lv_keyboard_class) || lv_obj_check_type(obj, &lv_slider_class);
}

static bool focusing;			/* in group_focus_cb: its own scrolling */

static void group_focus_cb(lv_group_t *g)
{
	lv_obj_t *obj = lv_group_get_focused(g);
	bool first;

	if (!ring || !obj || lv_obj_get_screen(obj) != lv_obj_get_parent(ring))
		return;
	if (ring_skips(obj)) {
		lv_obj_add_flag(ring, LV_OBJ_FLAG_HIDDEN);
		return;
	}
	/* scroll it into view first, so the ring goes where the element ends up */
	focusing = true;
	lv_obj_scroll_to_view_recursive(obj, LV_ANIM_OFF);
	focusing = false;
	lv_obj_update_layout(obj);
	first = lv_obj_has_flag(ring, LV_OBJ_FLAG_HIDDEN);
	style_ring(ring, obj, ring_override ? &ring_color : NULL);
	screen_relative(ring, &ring_from);
	screen_relative(obj, &ring_to);
	lv_area_increase(&ring_to, -style_ring_inset(), -style_ring_inset());
	lv_obj_remove_flag(ring, LV_OBJ_FLAG_HIDDEN);
	lv_obj_move_foreground(ring);
	if (first || ring_settling) {
		lv_anim_delete(ring, ring_anim_cb);
		ring_set(&ring_to);
		return;
	}
	lv_anim_t a;

	lv_anim_init(&a);
	lv_anim_set_var(&a, ring);
	lv_anim_set_exec_cb(&a, ring_anim_cb);
	lv_anim_set_values(&a, 0, 1024);
	lv_anim_set_duration(&a, WT_ANIM_MS);
	lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
	lv_anim_delete(ring, ring_anim_cb);
	lv_anim_start(&a);
}

/*
 * A list scrolled by touch (or its scroll throw): the ring stays on the focused row,
 * at once, and hides while that row is not wholly inside the scrolling area (it would
 * otherwise float over the status or hint bar). Keypad focus moves do their own
 * scrolling and animate the ring as before.
 */
void focus_ring_follow(lv_obj_t *scroller)
{
	lv_obj_t *obj = lv_group_get_focused(ui_group());
	lv_area_t a, view;

	if (focusing || !ring || !obj || lv_obj_get_parent(obj) != scroller || ring_skips(obj) ||
	    lv_obj_get_screen(obj) != lv_obj_get_parent(ring))
		return;
	lv_anim_delete(ring, ring_anim_cb);
	lv_obj_get_coords(obj, &a);
	lv_obj_get_coords(scroller, &view);
	if (a.y1 < view.y1 || a.y2 > view.y2) {
		lv_obj_add_flag(ring, LV_OBJ_FLAG_HIDDEN);
		return;
	}
	screen_relative(obj, &ring_to);
	lv_area_increase(&ring_to, -style_ring_inset(), -style_ring_inset());
	ring_set(&ring_to);
	lv_obj_remove_flag(ring, LV_OBJ_FLAG_HIDDEN);
}

void focus_init(lv_group_t *g)
{
	lv_group_set_focus_cb(g, group_focus_cb);
}

/* The ring in another colour (an armed button: warning), or back to the accent */
void ui_focus_ring_color(const lv_color_t *color)
{
	lv_obj_t *f = lv_group_get_focused(ui_group());

	ring_override = color != NULL;
	if (color)
		ring_color = *color;
	if (ring && f)
		style_ring(ring, f, color);
}

/* ---- screen transitions ------------------------------------------------------------ */

void ui_screen_transition(enum ui_transition t)
{
	next_transition = t;
}

/* The transition for the screen being loaded now (then back to the default) */
lv_screen_load_anim_t focus_take_transition(void)
{
	enum ui_transition t = next_transition;

	next_transition = UI_SLIDE_IN;
	return t == UI_SLIDE_IN ? LV_SCR_LOAD_ANIM_MOVE_LEFT :
	       t == UI_SLIDE_OUT ? LV_SCR_LOAD_ANIM_MOVE_RIGHT : LV_SCR_LOAD_ANIM_NONE;
}

/* App launch (SPEC.md, Animation): the focus ring grows to the screen edge, then done() */
static void (*grow_done)(void *arg);
static void *grow_arg;

static void grow_ready(lv_anim_t *a)
{
	void (*done)(void *) = grow_done;

	(void)a;
	grow_done = NULL;
	if (done)
		done(grow_arg);
}

bool ui_focus_ring_grow(void (*done)(void *arg), void *arg)
{
	lv_anim_t a;

	if (grow_done)
		return false;		/* one launch at a time */
	grow_done = done;
	grow_arg = arg;
	if (!ring || lv_obj_has_flag(ring, LV_OBJ_FLAG_HIDDEN)) {
		grow_ready(NULL);
		return true;
	}
	lv_anim_delete(ring, ring_anim_cb);
	screen_relative(ring, &ring_from);
	ring_to.x1 = 0;
	ring_to.y1 = 0;
	ring_to.x2 = WT_SCREEN_W - 1;
	ring_to.y2 = WT_SCREEN_H - 1;
	lv_obj_move_foreground(ring);
	lv_anim_init(&a);
	lv_anim_set_var(&a, ring);
	lv_anim_set_exec_cb(&a, ring_anim_cb);
	lv_anim_set_values(&a, 0, 1024);
	lv_anim_set_duration(&a, WT_ANIM_MS);
	lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
	lv_anim_set_completed_cb(&a, grow_ready);
	lv_anim_start(&a);
	return true;
}

/* back onto the focused element (after a launch that failed) */
void ui_focus_ring_refresh(void)
{
	group_focus_cb(ui_group());
}
