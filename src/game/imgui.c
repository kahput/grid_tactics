#include "imgui.h"
#include "common.h"
#include "debug.h"
#include <ctype.h>
#include <float.h>
#include <math.h>
#include <raylib.h>

UIContext *context = NULL;

UIWidget *widget_peek(void) {
	return &context->widgets[context->depth_parent[context->current_depth]];
}
UIWidget *find_widget(uint64_t id) {
	for (uint32_t index = 0; index < context->widget_count; ++index) {
		UIWidget *widget = &context->widgets[index];
		if (widget->id == id)
			return widget;
	}

	return NULL;
}

uint32_t widget_peek_index(void) {
	return context->depth_parent[context->current_depth];
}

UIWidgetCache *find_cached_widget(uint64_t id) {
	for (uint32_t index = 0; index < context->cached_widget_count; ++index) {
		UIWidgetCache *cache = &context->cached_widgets[index];
		if (cache->id == id)
			return cache;
	}

	return NULL;
}

void imgui_frame_begin(UIContext *ctx) {
	context = ctx;
	context->widget_count = 1;
	context->hot_item = 0;
}

static inline void remaining_size(UIWidget *widget, float size[AXIS2_MAX]);
static void fit_children(UIWidget *widget, Axis2 axis, bool is_main);
static void shrink_and_grow_children(UIWidget *widget, Axis2 axis, bool is_main);
static inline void wrap_text(UIWidget *widget);

void imgui_frame_end(void) {
	// Fit Sizing Width
	for (uint32_t index = context->widget_count - 1; index >= 1; --index) {
		UIWidget *widget = &context->widgets[index];
		fit_children(widget, AXIS2_X, widget->orientation == AXIS2_X);

		widget->size[AXIS2_X] = MAX(widget->size[AXIS2_X], widget->semantic_size[AXIS2_X].min);
	}

	// Grow Sizing Width
	for (uint32_t index = 1; index < context->widget_count; ++index) {
		UIWidget *widget = &context->widgets[index];
		shrink_and_grow_children(widget, AXIS2_X, widget->orientation == AXIS2_X);
	}

	// Wrap
	for (uint32_t index = 1; index < context->widget_count; ++index) {
		UIWidget *widget = &context->widgets[index];
		if (FLAG_GET(widget->flags, WIDGET_FLAG_TEXT) == false)
			continue;

		wrap_text(widget);
	}

	// Fit Sizing Height
	for (uint32_t index = context->widget_count - 1; index >= 1; --index) {
		UIWidget *widget = &context->widgets[index];
		fit_children(widget, AXIS2_Y, widget->orientation == AXIS2_Y);

		widget->size[AXIS2_Y] = MAX(widget->size[AXIS2_Y], widget->semantic_size[AXIS2_Y].min);
	}

	// Grow Sizing Height
	for (uint32_t index = 1; index < context->widget_count; ++index) {
		UIWidget *widget = &context->widgets[index];
		shrink_and_grow_children(widget, AXIS2_Y, widget->orientation == AXIS2_Y);
	}

	// Position & Align
	for (uint32_t index = 1; index < context->widget_count; ++index) {
		UIWidget *widget = &context->widgets[index];
		if (widget->parent == 0)
			continue;

		UIWidget *parent = &context->widgets[widget->parent];

		widget->offset[AXIS2_X] += parent->offset[AXIS2_X] + parent->padding[AXIS2_X][0] + parent->child_offset_accumulator[AXIS2_X];
		widget->offset[AXIS2_Y] += parent->offset[AXIS2_Y] + parent->padding[AXIS2_Y][0] + parent->child_offset_accumulator[AXIS2_Y];

		if (widget->id == 10143616117064879316ULL) {
			uint32_t x = 0;
			(void)x;
		}

		// Alignment
		float remaining[AXIS2_MAX] = { 0 };
		remaining_size(parent, remaining);

		uint32_t main = parent->orientation;
		uint32_t cross = !parent->orientation;
		ASSERT(parent->anchor >= IMGUI_ANCHOR_TOPLEFT && parent->anchor < IMGUI_ANCHOR_MAX);
		uint32_t align_x = (parent->anchor % 3);
		uint32_t align_y = (parent->anchor / 3);

		float scalar[AXIS2_MAX] = {
			align_x * 0.5f,
			align_y * 0.5f,
		};
		widget->offset[main] += remaining[main] * scalar[main];
		widget->offset[cross] += (remaining[cross] - widget->size[cross]) * scalar[cross];

		parent->child_offset_accumulator[main] += widget->size[main] + parent->child_gap;
	};

	// Draw
	Font font = GetFontDefault();
	for (uint32_t index = 1; index < context->widget_count; ++index) {
		UIWidget *widget = &context->widgets[index];

		Vector2 position = { widget->offset[AXIS2_X], widget->offset[AXIS2_Y] };
		Vector2 size = { widget->size[AXIS2_X], widget->size[AXIS2_Y] };
		widget->rect = (Rectangle){
			position.x, position.y, size.x, size.y
		};

		if (FLAG_GET(widget->flags, WIDGET_FLAG_BACKGROUND)) {
			bool is_hot = widget->id == context->hot_item;
			bool is_active = widget->id == context->active_item;

			if (is_hot && is_active && widget->flags & WIDGET_FLAG_ANIMATE) {
				position.x += 2;
				position.y += 2;

				DrawRectangleV(position, size, widget->background_color);
			} else if (is_active && FLAG_GET(widget->flags, WIDGET_FLAG_ANIMATE_ACTIVE)) {
				position.x += 2;
				position.y += 2;

				DrawRectangleV(position, size, widget->background_color);
			} else if (is_hot && FLAG_GET(widget->flags, WIDGET_FLAG_ANIMATE_HOT)) {
				DrawRectangleV(position, size, widget->background_color);
			} else {
				DrawRectangleRec(widget->rect, widget->background_color);
			}
		}

		if (FLAG_GET(widget->flags, WIDGET_FLAG_TEXT)) {
			Vector2 padded_text_position = {
				position.x + widget->padding[AXIS2_X][0],
				position.y + widget->padding[AXIS2_Y][0],
			};
			DrawText(widget->text, padded_text_position.x, padded_text_position.y, widget->font_size, widget->text_color);
		}
	}

	// Cache
	context->cached_widget_count = 0;
	for (uint32_t index = 0; index < context->widget_count; ++index) {
		UIWidget *widget = &context->widgets[index];
		if (widget->flags & WIDGET_FLAG_INTERACTABLE) {
			UIWidgetCache *cached = &context->cached_widgets[context->cached_widget_count++];
			cached->id = widget->id;
			cached->outer = widget->rect;

			cached->inner = widget->rect;

			cached->inner.x -= widget->padding[AXIS2_X][0];
			cached->inner.y -= widget->padding[AXIS2_Y][0];

			cached->inner.width -= widget->padding[AXIS2_X][0] + widget->padding[AXIS2_X][1];
			cached->inner.height -= widget->padding[AXIS2_Y][0] + widget->padding[AXIS2_Y][1];

			context->cached_widget_count++;
		}
	}

	if (context->mouse_left == 0 && context->mouse_right == 0)
		context->active_item = 0;
	else if (context->active_item == 0)
		context->active_item = -1;

	memory_zero_array(context->widgets);
	context = NULL;
}

UIWidget *widget_push(uint64_t id, UIWidgetFlags flags) {
	uint32_t current_index = context->widget_count++;
	UIWidget *widget = &context->widgets[current_index];
	widget->id = id;
	widget->flags = flags;

	if (FLAG_GET(flags, WIDGET_FLAG_ABSOLUTE) == false)
		if (context->current_depth) {
			UIWidget *parent = widget_peek();

			parent->children[parent->children_count++] = current_index;
			widget->parent = widget_peek_index();
		}

	context->depth_parent[++context->current_depth] = current_index;

	return widget;
}

void widget_pop(void) {
	context->current_depth--;
}

void imgui_layout_begin(uint64_t id, UIAxisSize width, UIAxisSize height, UIWidgetFlags flags) {
	UIWidget *widget = widget_push(id, flags);

	widget->semantic_size[AXIS2_X] = width;
	widget->semantic_size[AXIS2_Y] = height;

	widget->semantic_size[AXIS2_X].max = widget->semantic_size[AXIS2_X].max <= 0.0f ? FLT_MAX : widget->semantic_size[AXIS2_X].max;
	widget->semantic_size[AXIS2_Y].max = widget->semantic_size[AXIS2_Y].max <= 0.0f ? FLT_MAX : widget->semantic_size[AXIS2_Y].max;
}

void imgui_layout_end(void) {
	widget_pop();
}

void imgui_background_color(Color color) {
	UIWidget *widget = widget_peek();

	widget->flags |= WIDGET_FLAG_BACKGROUND;
	widget->background_color = color;
}

void imgui_absolute_position(float x, float y) {
	UIWidget *widget = widget_peek();

	widget->flags |= WIDGET_FLAG_ABSOLUTE;
	widget->offset[AXIS2_X] = x;
	widget->offset[AXIS2_Y] = y;
}

void imgui_offset(float x, float y) {
	UIWidget *widget = widget_peek();

	widget->offset[AXIS2_X] = x;
	widget->offset[AXIS2_Y] = y;
}

void imgui_orientation(Axis2 axis) {
	UIWidget *widget = widget_peek();

	widget->orientation = axis;
}

void imgui_anchor(ImguiAnchor anchor) {
	widget_peek()->anchor = anchor;
}

void imgui_padding(uint16_t left, uint16_t right, uint16_t top, uint16_t bottom) {
	UIWidget *widget = widget_peek();

	widget->padding[AXIS2_X][0] = left;
	widget->padding[AXIS2_X][1] = right;
	widget->padding[AXIS2_Y][0] = top;
	widget->padding[AXIS2_Y][1] = bottom;
}

void imgui_padding_x(uint16_t padding) {
	UIWidget *widget = widget_peek();
	imgui_padding(padding, padding, widget->padding[AXIS2_Y][0], widget->padding[AXIS2_Y][1]);
}
void imgui_padding_y(uint16_t padding) {
	UIWidget *widget = widget_peek();
	imgui_padding(widget->padding[AXIS2_X][0], widget->padding[AXIS2_X][1], padding, padding);
}

void imgui_padding_xy(uint16_t padding) {
	imgui_padding(padding, padding, padding, padding);
}

void imgui_child_gap(uint16_t gap) {
	UIWidget *widget = widget_peek();

	widget->child_gap = gap;
}

Rectangle imgui_content_region(uint64_t id) {
	UIWidget *widget = find_widget(id);
	UIWidgetCache *cache = find_cached_widget(id);
	Rectangle result = { 0 };

	if (widget && cache) {
		result = cache->outer;
	} else if (cache)
		result = cache->outer;

	return result;
}

Rectangle imgui_rect_last_frame(uint64_t id) {
	UIWidgetCache *cache = find_cached_widget(id);
	if (cache)
		return cache->outer;

	return (Rectangle){ 0 };
}

bool imgui_active(void) {
	return widget_peek()->id == context->active_item;
}
bool imgui_hot(void) {
	return widget_peek()->id == context->hot_item;
}

Vector2 imgui_mouse_position(void) {
	return context->mouse_position;
}

void imgui_rect(uint64_t id, float width, float height, Color color) {
	imgui_layout_begin(id, FIXED(width), FIXED(height), WIDGET_FLAG_BACKGROUND);
	imgui_background_color(color);
	imgui_layout_end();
}

static void measure_text(char *text, uint32_t font_size, uint32_t *min_width, uint32_t *preferred_width, uint32_t *height) {
	Font font = GetFontDefault();
	float scale = (float)font_size / (float)font.baseSize;
	*height = font.baseSize * scale;

	uint32_t text_length = string_length(text);
	float current_word = 0, largest_word = 0, total_width = 0;
	for (uint32_t index = 0; index < text_length; ++index) {
		char c = text[index];
		ASSERT((c >= 32 && c < 127) || c == '\n');
		if (c == '\n') {
			*height += font.baseSize * scale;
			continue;
		}

		GlyphInfo glyph = GetGlyphInfo(font, (int32_t)c);
		Rectangle rec = GetGlyphAtlasRec(font, (int32_t)c);

		if (index < text_length - 1 && c == '#' && text[index + 1] == '#') {
			text[index] = '\0';
			break;
		}

		int32_t default_font_size = 10;
		if (font_size < 10)
			font_size = 10;
		int32_t spacing = font_size / default_font_size;
		float advance = (uint32_t)glyph.advanceX ? glyph.advanceX : (rec.width + glyph.offsetX);
		if (index + 1 == text_length)
			spacing = 0;
		total_width += (advance * scale) + spacing;

		if (isalnum(c))
			current_word += (advance * scale) + spacing;
		else {
			largest_word = MAX(current_word, largest_word);
			current_word = 0;
		}
	}

	*preferred_width = floorf(total_width);
	*min_width = MAX(current_word, largest_word);
}

void imgui_text(const char *text, uint32_t font_size, Color color) {
	UIWidget *widget = widget_push(hash_count(text, string_length(text)), WIDGET_FLAG_TEXT);
	memory_copy(widget->text, text, string_size(text));

	widget->text_color = color;
	widget->font_size = font_size;

	uint32_t preferred_width = 0, minimum_width = 0, height = 0;

	measure_text(widget->text, font_size, &minimum_width, &preferred_width, &height);

	widget->semantic_size[AXIS2_X] = GROW(.min = minimum_width, .max = preferred_width);
	widget->semantic_size[AXIS2_Y] = FIXED(height);

	widget->size[AXIS2_X] = preferred_width;
	widget->size[AXIS2_Y] = height;

	imgui_layout_end();
}

UIInteraction imgui_button(const char *label, uint32_t font_size) {
	uint64_t id = hash_count(label, string_length(label));

	UIWidget *widget = widget_push(id,
		WIDGET_FLAG_CLICKABLE |
			WIDGET_FLAG_BACKGROUND |
			WIDGET_FLAG_BORDER |
			WIDGET_FLAG_TEXT |
			WIDGET_FLAG_ANIMATE_HOT |
			WIDGET_FLAG_ANIMATE_ACTIVE);

	memory_copy(widget->text, label, string_size(label));
	widget->font_size = font_size;
	widget->background_color = rgb(0, 0, 0);
	widget->text_color = rgb(255, 255, 255);
	uint32_t padding = 8;
	for (uint32_t index = 0; index < 4; ++index)
		*(uint32_t *)widget->padding[index] = padding;

	uint32_t preferred_width = 0, minimum_width = 0, height = 0;
	measure_text(widget->text, font_size, &minimum_width, &preferred_width, &height);

	widget->semantic_size[AXIS2_X] = GROW(.min = minimum_width + widget->padding[AXIS2_X][0] + widget->padding[AXIS2_X][1]);
	widget->semantic_size[AXIS2_Y] = FIXED(height + widget->padding[AXIS2_Y][0] + widget->padding[AXIS2_Y][0]);

	/* widget->semantic_size[AXIS2_X].max = widget->semantic_size[AXIS2_X].max <= 0.0f ? FLOAT_MAX : widget->semantic_size[AXIS2_X].max; */
	/* widget->semantic_size[AXIS2_Y].max = widget->semantic_size[AXIS2_Y].max <= 0.0f ? FLOAT_MAX : widget->semantic_size[AXIS2_Y].max; */

	widget->size[AXIS2_X] = preferred_width;
	widget->size[AXIS2_Y] = height;

	UIInteraction interaction = { 0 };

	UIWidgetCache *cache = find_cached_widget(id);
	if (cache)
		interaction = imgui_interact(id, cache->outer, widget->flags);
	widget_pop();

	return interaction;
}

bool imgui_scrollbar(uint64_t id, float *value, float min, float max) {
	uint64_t track_id = id;
	uint64_t thumb_id = hash64_combine(id, hash_array("thumb"));

	bool changed = false;

	UIWidgetCache *cached = find_cached_widget(track_id);
	UIInteraction interaction = { 0 };
	if (cached) {
		interaction = imgui_interact(id, cached->outer, WIDGET_FLAG_INTERACTABLE);

		if (context->active_item == id) {
			float draggable_height = cached->inner.height;
			float offset_y = CLAMP(context->mouse_position.y - cached->outer.y, 64, draggable_height - 64);

			*value = (offset_y / draggable_height) * max;
			changed = true;
		}
	}

	imgui_layout_begin(track_id, FIT(8), GROW(), WIDGET_FLAG_CLICKABLE);
	{
		Color track_color = rgb(80, 80, 80);
		Color thumb_color = rgb(30, 30, 30);

		/* imgui_background_color(track_color); */
		imgui_anchor(IMGUI_ANCHOR_TOPRIGHT);

		float t_slider = CLAMP(*value, min, max) / max;

		imgui_layout_begin(thumb_id, FIXED(4), FIXED(128), 0);
		{
			float draggable_height = cached ? cached->inner.height : 0.0f;
			float y = CLAMP((t_slider * draggable_height) - 64, 0.0f, draggable_height);
			imgui_offset(0.0f, y);
			imgui_background_color(thumb_color);
		}
		imgui_layout_end(); // thumb

		imgui_layout_end(); // track
		UIWidget *widget = find_widget(thumb_id);
		if (interaction.hovering || interaction.held) {
			widget->background_color.r -= 10;
			widget->background_color.g -= 10;
			widget->background_color.b -= 10;
			/* widget->offset[AXIS2_X] -= 2; */
			widget->size[AXIS2_X] += 8;
		}
	}

	return changed;
}

UIInteraction imgui_interact(uint64_t id, Rectangle area, UIWidgetFlags flags) {
	UIInteraction interact = { 0 };
	if ((flags & WIDGET_FLAG_INTERACTABLE) == 0)
		return interact;

	Vector2 mouse = context->mouse_position;
	bool hovered = CheckCollisionPointRec(mouse, area);

	if (hovered) {
		context->hot_item = id;
		interact.hovering = true;

		if (context->active_item == 0 && (context->mouse_left || context->mouse_right)) {
			interact.pressed[MOUSE_BUTTON_LEFT] = context->mouse_left;
			interact.pressed[MOUSE_BUTTON_RIGHT] = context->mouse_right;

			context->active_item = id;
		}
	}

	UIWidgetCache *cache = find_cached_widget(id);
	if (cache) {
		interact.hover_entered = cache->hovered == false && hovered;

		cache->hovered = interact.hovering;
	}

	if (context->active_item == id)
		interact.held = true;

	if (context->mouse_left == 0 &&
		context->hot_item == id &&
		context->active_item == id) {
		interact.clicked = true;
	}

	return interact;
}

void remaining_size(UIWidget *widget, float size[2]) {
	uint32_t main = widget->orientation;
	uint32_t cross = !widget->orientation;

	size[main] = widget->size[main];
	size[main] -= widget->padding[main][0] + widget->padding[main][1];
	size[cross] = widget->size[cross];
	size[cross] -= widget->padding[cross][0] + widget->padding[cross][1];

	for (uint32_t index = 0; index < widget->children_count; ++index) {
		UIWidget *child = &context->widgets[widget->children[index]];
		size[main] -= child->size[main];
	}
	size[main] -= (widget->children_count - 1) * widget->child_gap;
}

void fit_children(UIWidget *widget, Axis2 axis, bool is_main) {
	bool fit = widget->semantic_size[axis].type == UI_SIZE_FIT || widget->semantic_size[axis].type == UI_SIZE_GROW;
	if (fit == false)
		return;

	// Fit
	uint32_t padding = widget->padding[axis][0] + widget->padding[axis][1];
	uint32_t child_gap = widget->children_count ? ((widget->children_count - 1) * widget->child_gap) * is_main : 0;

	float new_size = 0;
	float new_min = 0;

	for (uint32_t index = 0; index < widget->children_count; ++index) {
		UIWidget *child = &context->widgets[widget->children[index]];

		if (is_main) {
			new_size += child->size[axis];
			new_min += child->semantic_size[axis].min;
		} else {
			new_size = MAX(new_size, child->size[axis]);
			new_min = MAX(new_min, child->semantic_size[axis].min);
		}
	}

	new_size += (padding + child_gap);
	new_min += (padding + child_gap);

	widget->size[axis] = MAX(widget->size[axis], new_size);
	widget->semantic_size[axis].min = MAX(widget->semantic_size[axis].min, new_min);

	if (widget->semantic_size[axis].max > 0.1f) {
		widget->size[axis] = MIN(widget->semantic_size[axis].max, widget->size[axis]);
		widget->semantic_size[axis].min = MIN(widget->semantic_size[axis].max, widget->semantic_size[axis].min);
	}
}

void shrink_and_grow_children(UIWidget *parent, Axis2 axis, bool is_main) {
	float remaining[AXIS2_MAX] = { 0 };
	remaining_size(parent, remaining);

	UIWidget *resizeable[MAX_CHILDREN] = { 0 };
	uint32_t resizeable_count = { 0 };
	bool is_shrinking = remaining[axis] < 0.0f;

	for (uint32_t index = 0; index < parent->children_count; ++index) {
		UIWidget *child = &context->widgets[parent->children[index]];

		if (child->semantic_size[axis].type == UI_SIZE_GROW)
			resizeable[resizeable_count++] = child;
		else if (is_shrinking && child->semantic_size[axis].type == UI_SIZE_FIT) {
			resizeable[resizeable_count++] = child;
		}
	}

	if (resizeable_count == 0)
		return;

	if (is_main == false) {
		for (uint32_t index = 0; index < resizeable_count; ++index) {
			UIWidget *child = resizeable[index];
			child->size[axis] = MAX(child->size[axis], remaining[axis]);
		}

		return;
	}

	float sign = remaining[axis] > 0.0f ? 1.0f : -1.0f;
	remaining[axis] = fabsf(remaining[axis]);
	while (remaining[axis] > 0.01f && resizeable_count) {
		float smallest = resizeable[0]->size[axis] * sign;
		float second_smallest = INFINITY;
		uint32_t smallest_count = 1;

		for (uint32_t index = 1; index < resizeable_count; ++index) {
			float size = resizeable[index]->size[axis] * sign;

			if (size == smallest) {
				smallest_count++;
			} else if (size < smallest) {
				second_smallest = smallest;
				smallest = size;
				smallest_count = 1;
			} else if (size < second_smallest) {
				second_smallest = size;
			}
		}

		float space_to_add = remaining[axis] / smallest_count;
		if (second_smallest != INFINITY)
			space_to_add = MIN(space_to_add, second_smallest - smallest);

		for (uint32_t index = 0; index < resizeable_count; ++index) {
			UIWidget *child = resizeable[index];
			if (child->size[axis] * sign == smallest) {
				if (sign < 0.0f)
					space_to_add = MIN(space_to_add, child->size[axis] - child->semantic_size[axis].min);
				if (sign > 0.0f && child->semantic_size[axis].max > 0.1f)
					space_to_add = MIN(space_to_add, child->semantic_size[axis].max - child->size[axis]);

				child->size[axis] += space_to_add * sign;
				remaining[axis] -= space_to_add;
			}
		}

		uint32_t active_count = 0;
		for (uint32_t index = 0; index < resizeable_count; ++index) {
			UIWidget *child = resizeable[index];

			bool can_shrink = (sign < 0.0f && child->size[axis] > child->semantic_size[axis].min + 0.01f);
			bool can_grow = (sign > 0.0f && child->size[axis] < child->semantic_size[axis].max - 0.01f);

			if (can_shrink || can_grow) {
				resizeable[active_count++] = child;
			}
		}
		resizeable_count = active_count;
	}
}

void wrap_text(UIWidget *widget) {
	uint32_t current_width = 0;
	int32_t last_space = -1;
	uint32_t width_at_last_space = 0;

	Font font = GetFontDefault();
	float scale = (float)widget->font_size / (float)font.baseSize;

	uint32_t text_length = string_length(widget->text);
	for (uint32_t index = 0; widget->text[index]; ++index) {
		char c = widget->text[index];
		if (index < text_length - 1 && c == '#' && widget->text[index + 1] == '#') {
			widget->text[index] = '\0';
			break;
		}

		if (c == '\n') {
			current_width = 0;
			last_space = -1;
			width_at_last_space = 0;
			continue;
		}

		if (c == ' ') {
			last_space = (int32_t)index;
			width_at_last_space = current_width;
		};

		GlyphInfo glyph = GetGlyphInfo(font, (int32_t)c);
		Rectangle rec = GetGlyphAtlasRec(font, (int32_t)c);

		int32_t default_font_size = 10;
		if (widget->font_size < 10)
			widget->font_size = 10;
		int32_t spacing = widget->font_size / default_font_size;
		uint32_t advance = (uint32_t)glyph.advanceX ? glyph.advanceX : (rec.width + glyph.offsetX);
		if (index + 1 == text_length)
			spacing = 0;
		current_width += (advance * scale) + spacing;

		if (current_width > widget->size[AXIS2_X]) {
			if (last_space >= 0) {
				widget->text[last_space] = '\n';

				current_width -= width_at_last_space;
				last_space = -1;
				width_at_last_space = 0;
				widget->size[AXIS2_Y] += font.baseSize * scale;
			}
		}
	}
}
