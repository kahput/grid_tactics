#ifndef IMGUI_H_
#define IMGUI_H_

#include "common.h"
#include <raylib.h>

#define MAX_UI_ELEMENTS 1024

typedef enum {
	AXIS2_X,
	AXIS2_Y,

	AXIS2_MAX,
} Axis2;

typedef enum {
	UI_SIZE_FIT,
	UI_SIZE_FIXED,
	UI_SIZE_GROW,
	UI_SIZE_PERCENT
} UISizeType;

typedef struct UISize {
	UISizeType type;
	float min, max;
} UIAxisSize;

typedef enum {
	UI_ALIGN_LEFT,
	UI_ALIGN_RIGHT,
	UI_ALIGN_CENTER,

	UI_ALIGN_TOP = UI_ALIGN_LEFT,
	UI_ALIGN_BOTTOM = UI_ALIGN_RIGHT,
} UIAlign;

typedef enum {
	WIDGET_FLAG_CLICKABLE = 1 << 0,
	WIDGET_FLAG_SCROLLABLE = 1 << 1,
	WIDGET_FLAG_DRAGGABLE = 1 << 2,
	WIDGET_FLAG_RESIZABLE = 1 << 3,

	WIDGET_FLAG_INTERACTABLE =
		WIDGET_FLAG_CLICKABLE |
		WIDGET_FLAG_SCROLLABLE |
		WIDGET_FLAG_DRAGGABLE |
		WIDGET_FLAG_RESIZABLE,

	WIDGET_FLAG_ABSOLUTE = 1 << 4,
	WIDGET_FLAG_BACKGROUND = 1 << 5,
	WIDGET_FLAG_ROUNDED = 1 << 6,
	WIDGET_FLAG_BORDER = 1 << 7,
	WIDGET_FLAG_TEXT = 1 << 8,
	WIDGET_FLAG_IMAGE = 1 << 9,

	WIDGET_FLAG_ANIMATE_HOT = 1 << 10,
	WIDGET_FLAG_ANIMATE_ACTIVE = 1 << 11,

	WIDGET_FLAG_ANIMATE =
		WIDGET_FLAG_ANIMATE_HOT | WIDGET_FLAG_ANIMATE_ACTIVE,
} UIWidgetFlags;

#define MAX_CHILDREN 32
typedef struct {
	uint64_t id;

	// Hierarchy
	uint32_t parent;
	uint32_t children[MAX_CHILDREN];
	uint32_t children_count;

	float child_offset_accumulator[AXIS2_MAX];

	// Passed
	UIWidgetFlags flags;

	char text[256];
	uint32_t font_size;

	Color background_color, text_color;
	Axis2 orientation;
	UIAlign align[AXIS2_MAX];
	uint16_t padding[2][2];
	uint32_t child_gap;
	UIAxisSize semantic_size[AXIS2_MAX];

	// TEMPORARY

	// Computed
	Rectangle rect;
	float offset[AXIS2_MAX];
	float size[AXIS2_MAX];
} UIWidget;

typedef struct {
	uint64_t id;
	Rectangle outer, inner;
	bool hovered;
} UIWidgetCache;

typedef struct {
	bool hovering, hover_entered;
	bool held;
	bool pressed[MOUSE_BUTTON_BACK + 1];
	bool clicked, right_clicked;
} UIInteraction;

#define MAX_DEPTH 8
typedef struct {
	Vector2 mouse_position;
	bool mouse_left, mouse_right;

	uint32_t depth_parent[MAX_DEPTH];
	uint32_t current_depth;

	UIWidget widgets[MAX_UI_ELEMENTS];
	uint32_t widget_count;

	UIWidgetCache cached_widgets[MAX_UI_ELEMENTS];
	uint32_t cached_widget_count;

	uint64_t hot_item;
	uint64_t active_item;
} UIContext;

#define FIXED(n) ((UIAxisSize){ .type = UI_SIZE_FIXED, .min = (n), .max = (n) })
#define FIT(...) ((UIAxisSize){ .type = UI_SIZE_FIT, __VA_ARGS__ })
#define GROW(...) ((UIAxisSize){ .type = UI_SIZE_GROW, __VA_ARGS__ })
#define PERCENT(...) ((UIAxisSize){ .type = UI_SIZE_PERCENT, __VA_ARGS__ })

void imgui_frame_begin(UIContext *context);
void imgui_frame_end(void);

UIInteraction imgui_interact(uint64_t id, Rectangle area, UIWidgetFlags flags);

void imgui_layout_begin(uint64_t id, UIAxisSize width, UIAxisSize height, UIWidgetFlags flags);
void imgui_layout_end(void);

void imgui_background_color(Color color);
void imgui_absolute_position(float x, float y);
void imgui_offset(float x, float y);
void imgui_orientation(Axis2 axis);

void imgui_align_x(UIAlign align);
void imgui_align_y(UIAlign align);

void imgui_padding(uint16_t left, uint16_t right, uint16_t top, uint16_t bottom);
void imgui_padding_x(uint16_t padding);
void imgui_padding_y(uint16_t padding);
void imgui_padding_xy(uint16_t padding);
void imgui_child_gap(uint16_t gap);

Rectangle imgui_content_region(uint64_t id);
Rectangle imgui_rect_last_frame(uint64_t id);

bool imgui_active(void);
bool imgui_hot(void);

Vector2 imgui_mouse_position(void);

void imgui_rect(uint64_t id, float width, float height, Color color);
void imgui_text(const char *text, uint32_t font_size, Color color);

UIInteraction imgui_button(const char *label, uint32_t font_size);
bool imgui_scrollbar(uint64_t id, float *value, float min, float max);

#endif /* IMGUI_H_ */
