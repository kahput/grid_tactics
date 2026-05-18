#include <stdbool.h>
#include <game_interface.h>

#include "debug.h"
#include "common.h"

#include "globals.h"
#include "geometry.h"
#include "imgui.h"

#include <raylib.h>
#include <rlgl.h>
#include <raymath.h>
#include <stdlib.h>

typedef struct {
	uint8_t *base;
	uint64_t offset, capacity;
} ArenaAllocator;

ArenaAllocator arena_make(uint64_t size) {
	ArenaAllocator result = { .base = malloc(size), .capacity = size };

	return result;
}

void *arena_allocate(ArenaAllocator *allocator, uint64_t size, uint64_t align) {
	ASSERT(allocator->offset + size < allocator->capacity);

	uint64_t aligned_offset = alignup(allocator->offset, align);
	void *result = allocator->base + aligned_offset;
	allocator->offset += (aligned_offset - allocator->offset) + size;

	memory_zero(result, size);

	return result;
}

#define arena_push_struct(arena, T) arena_allocate((arena), sizeof(T), alignof(T))
#define arena_push_count(arena, count, T) (T *)arena_allocate((arena), sizeof(T) * (count), alignof(T))

typedef enum {
	GAME_STATE_MENU,
	GAME_STATE_PLAY,
} GameScene;

typedef struct {
	ArenaAllocator arena;
	ArenaAllocator frame;

	Model selection;

	Vector3 selection_position;
	bool selected;

	Vector3 player_position;

	struct {
		Vector3 start, target;
		float duration, t;
	} move_animation;

	GameScene state;
	bool initialized;
} PermanentState;

typedef enum {
	SOUND_EFFECT_BUTTON_HOVER,
	SOUND_EFFECT_BUTTON_PRESS,

	SOUND_EFFECT_MAX,
} SoundEffect;

typedef struct {
	ArenaAllocator arena;

	Camera3D camera;
	Texture2D atlas;
	Model map;

	UIContext gui_state;

	Sound sound_effects[SOUND_EFFECT_MAX];

	bool initialized;
} TransientState;

static PermanentState *pstate = NULL;
static TransientState *tstate = NULL;
static UIContext *gui_state = NULL;

// clang-format off
uint32_t map_data[GRID_SIZE * GRID_SIZE] = {
    1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  
    1, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,  
    0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,  
    0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,  
    0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,  
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  
};
// clang-format on

void game_state_menu(void);
void game_state_play(void);

int32x3 world_to_grid(Vector3 point) {
	float grid_half = GRID_SIZE * 0.5f;

	int32x3 result = {
		CLAMP(floorf(point.x / BLOCK_SIZE), -grid_half, grid_half - 1),
		point.y / BLOCK_SIZE,
		CLAMP(floorf(point.z / BLOCK_SIZE), -grid_half, grid_half - 1),
	};

	return result;
}

typedef struct AStarNode AStarNode;
struct AStarNode {
	int32x3 grid_position;
	int32_t g_score, h_score;

	AStarNode *next, *prev;
};

static inline int32_t manhatten_distance(int32x3 start, int32x3 target) {
	return abs(start.x - target.x) + abs(start.y - target.y) + abs(start.z - target.z);
}

#define dll_push_back(head, element) (                             \
	(head) == 0                                                    \
		? ((head) = (element)->next = (element)->prev = (element)) \
		: ((head)->prev->next = (element), (element)->prev = (head)->prev, (element)->next = (head), (head)->prev = (element)))

#define dll_push_front(head, element) (                            \
	(head) == 0                                                    \
		? ((head) = (element)->next = (element)->prev = (element)) \
		: ((element)->prev = (head)->prev, (element)->next = (head), (head)->prev->next = (element), (head)->prev = (element), (head) = (element)))

#define dll_pop_front(head, popped_element) (               \
	(head) == 0                                             \
		? (head)                                            \
		: ((head)->next == (head)                           \
				  ? ((popped_element) = (head), (head) = 0) \
				  : ((head)->prev->next = (head)->next,     \
						(head)->next->prev = (head)->prev,  \
						(popped_element) = (head),          \
						(head) = (head)->next)))

#define dll_pop_back(head, popped_element) (                \
	(head) == 0                                             \
		? (head)                                            \
		: ((head)->prev == (head)                           \
				  ? ((popped_element) = (head), (head) = 0) \
				  : ((head)->prev->prev->next = (head),     \
						(popped_element) = (head)->prev,    \
						(head)->prev = (head)->prev->prev)))

void _astar_insert_neighbour(AStarNode *list, AStarNode *neighbour) {
	AStarNode *itr = list;
	bool inserted = false;
	do {
		if (neighbour->g_score + neighbour->h_score < itr->g_score + itr->h_score) {
			dll_push_front(itr, neighbour);
			inserted = true;
			break;
		}
	} while (itr != list);

	if (inserted == false)
		dll_push_back(list, neighbour);
}

/* int32x3 *astar_path(ArenaAllocator *arena, Vector3 start, Vector3 target) { */
/* 	int32x3 start_grid_position = world_to_grid(start); */
/* 	int32x3 target_grid_position = world_to_grid(target); */

/* 	ArenaAllocator *scratch = &pstate->frame; */
/* 	uint64_t scratch_mark = scratch->offset; */

/* 	AStarNode *open_set = arena_push_struct(scratch, AStarNode); */
/* 	AStarNode *closed_set = NULL; */

/* 	dll_push_back(open_set, open_set); */
/* 	open_set->g_score = 0; */
/* 	open_set->grid_position = start_grid_position; */
/* 	open_set->h_score = manhatten_distance(start_grid_position, target_grid_position); */

/* 	while (open_set) { */
/* 		AStarNode *current = open_set; */
/* 		dll_pop_front(open_set, current); */
/* 		dll_push_front(closed_set, current); */

/* 		if (current->h_score == 0) */
/* 			break; */

/* 		AStarNode *neighbour_list = NULL; */
/* 		if (current->grid_position.x > 0) { */
/* 			int32x3 neighbour_grid_position = current->grid_position; */
/* 			neighbour_grid_position.x -= 1; */

/* 			AStarNode *neighbour = arena_push_struct(scratch, AStarNode); */
/* 			neighbour->grid_position = neighbour_grid_position; */
/* 			neighbour->g_score = manhatten_distance(start_grid_position, neighbour_grid_position); */
/* 			neighbour->h_score = manhatten_distance(target_grid_position, neighbour_grid_position); */

/* 			dll_push_front(neighbour_list, neighbour); */
/* 		} */
/* 		if (current->grid_position.x < GRID_SIZE - 1) { */
/* 			int32x3 neighbour_grid_position = current->grid_position; */
/* 			neighbour_grid_position.x += 1; */

/* 			AStarNode *neighbour = arena_push_struct(scratch, AStarNode); */
/* 			neighbour->grid_position = neighbour_grid_position; */
/* 			neighbour->g_score = manhatten_distance(start_grid_position, neighbour_grid_position); */
/* 			neighbour->h_score = manhatten_distance(target_grid_position, neighbour_grid_position); */

/* 			dll_push_front(neighbour_list, neighbour); */
/* 		} */
/* 		if (current->grid_position.z > 0) { */
/* 			int32x3 neighbour_grid_position = current->grid_position; */
/* 			neighbour_grid_position.z -= 1; */

/* 			AStarNode *neighbour = arena_push_struct(scratch, AStarNode); */
/* 			neighbour->grid_position = neighbour_grid_position; */
/* 			neighbour->g_score = manhatten_distance(start_grid_position, neighbour_grid_position); */
/* 			neighbour->h_score = manhatten_distance(target_grid_position, neighbour_grid_position); */

/* 			dll_push_front(neighbour_list, neighbour); */
/* 		} */
/* 		if (current->grid_position.z < GRID_SIZE - 1) { */
/* 			int32x3 neighbour_grid_position = current->grid_position; */
/* 			neighbour_grid_position.z += 1; */

/* 			AStarNode *neighbour = arena_push_struct(scratch, AStarNode); */
/* 			neighbour->grid_position = neighbour_grid_position; */
/* 			neighbour->g_score = manhatten_distance(start_grid_position, neighbour_grid_position); */
/* 			neighbour->h_score = manhatten_distance(target_grid_position, neighbour_grid_position); */

/* 			dll_push_front(neighbour_list, neighbour); */
/* 		} */

/* 		AStarNode *neighbour = neighbour_list; */
/* 		do { */
/* 			AStarNode *closed = closed_set; */
/* 			bool found = false; */
/* 			do { */
/* 				if (neighbour == closed) */
/* 					found = true; */
/* 			} while (closed != closed_set); */

/* 			if (found) */
/* 				continue; */
/* 		} while (neighbour != neighbour_list); */
/* 	} */
/* 	int32x3 *result = NULL; */

/* 	scratch->offset = scratch_mark; */
/* 	return result; */
/* } */

void update_and_draw(GameContext *context) {
	pstate = (PermanentState *)context->memory;
	tstate = (TransientState *)context->transient_memory;

	if (pstate->initialized == false) {
		pstate->arena.base = context->memory + sizeof(PermanentState);
		pstate->arena.capacity = context->memory_size - sizeof(PermanentState);

		pstate->frame.capacity = MiB(4);
		pstate->frame.base = malloc(pstate->frame.capacity);

		pstate->selection = LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f));

		pstate->initialized = true;
	}

	if (tstate->initialized == false) {
		tstate->arena.base = context->transient_memory + sizeof(TransientState);
		tstate->arena.capacity = context->transient_memory_size - sizeof(TransientState);
		float yaw = DEG2RAD * 54.736f;
		float pitch = DEG2RAD * 45;
		float arm_length = 2.0f;
		tstate->camera = (Camera3D){
			.position = {
			  sinf(pitch) * cosf(yaw) * arm_length,
			  sinf(pitch) * arm_length,
			  sinf(pitch) * sinf(yaw) * arm_length,
			},
			.up = { 0.0f, 1.0f, 0.0f },
			.target = { 0.0f, 0.0f, 0.0f },
			.fovy = 16.f,

			.projection = CAMERA_ORTHOGRAPHIC
		};
		tstate->camera.position = (Vector3){ 1, 1, 1 };

		rlSetClipPlanes(-100.0f, 100.0f);
		tstate->atlas = LoadTexture("assets/Tilemap/tilemap_packed.png");

		Mesh map_mesh = { 0 };

		uint32_t max_vertices = GRID_SIZE * GRID_SIZE * GRID_SIZE * 36;
		map_mesh.vertices = (float *)arena_push_count(&tstate->arena, max_vertices, float32x3);
		map_mesh.normals = (float *)arena_push_count(&tstate->arena, max_vertices, float32x3);
		map_mesh.texcoords = (float *)arena_push_count(&tstate->arena, max_vertices, float32x2);

		/* push_slope(&map_mesh, 4.0f, 0.5f, 0.0f, SPRITE_DUNGEON_WALL); */

		for (uint32_t index = 0; index < GRID_SIZE * GRID_SIZE; ++index) {
			uint32_t x = index % GRID_SIZE;
			uint32_t z = index / GRID_SIZE;

			float grid_half = GRID_SIZE * 0.5f;
			float block_half = BLOCK_SIZE * 0.5f;

			float32x3 cube_pos = {
				(x - grid_half) * BLOCK_SIZE, -block_half, (z - grid_half) * BLOCK_SIZE
			};
			cube_pos.x += block_half;
			cube_pos.z += block_half;

			uint32_t id = map_data[index];
			push_block3(&map_mesh, cube_pos, id);

			if (id == BLOCK_TYPE_WALL) {
				cube_pos.y += BLOCK_SIZE;
				push_block3(&map_mesh, cube_pos, BLOCK_TYPE_WALL);
				/* cube_pos.y += BLOCK_SIZE; */
				/* push_cube3(&map_mesh, cube_pos, BLOCK_TYPE_DUNGEON_WALL); */
			}
		}

		UploadMesh(&map_mesh, true);
		tstate->map = LoadModelFromMesh(map_mesh);
		tstate->map.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = tstate->atlas;
		pstate->state = GAME_STATE_PLAY;

		gui_state = &tstate->gui_state;
		*gui_state = (UIContext){ 0 };

		tstate->sound_effects[SOUND_EFFECT_BUTTON_HOVER] = LoadSound("assets/sound/sfx/button_hover.mp3");
		tstate->sound_effects[SOUND_EFFECT_BUTTON_PRESS] = LoadSound("assets/sound/sfx/button_press.mp3");
		// :sound

		// :init

		tstate->initialized = true;
	}

	// TODO: Update your variables here

	// Draw
	BeginDrawing();

	ClearBackground(RAYWHITE);

	switch (pstate->state) {
		case GAME_STATE_MENU:
			game_state_menu();
			break;
		case GAME_STATE_PLAY:
			game_state_play();
			break;
		default:
			ASSERT(false);
			break;
	}

	EndDrawing();

	pstate->frame.offset = 0;
}

void unload(GameContext *context) {
	tstate = (TransientState *)context->transient_memory;
	TraceLog(LOG_INFO, "Unloading assets");
	ASSERT(tstate->map.meshCount == 1);
	Mesh *mesh = &tstate->map.meshes[0];
	rlUnloadVertexArray(mesh->vaoId);
	if (mesh->vboId != NULL) {
		for (int i = 0; i < 9; i++)
			rlUnloadVertexBuffer(mesh->vboId[i]);

		RL_FREE(mesh->vboId);
		mesh->vboId = NULL;
	}

	for (uint32_t index = 0; index < SOUND_EFFECT_MAX; ++index)
		UnloadSound(tstate->sound_effects[index]);

	*mesh = (Mesh){ 0 };
	UnloadModel(tstate->map);
}

// ─── Palette ────────────────────────────────────────────────────────────────
#define COL_BG_DARK rgb(24, 20, 37) // 181425 — near-black panel
#define COL_BG_MID rgb(38, 43, 68) // 262b44 — button resting
#define COL_BG_HOVER rgb(58, 68, 102) // 3a4466 — button hover
#define COL_BG_HELD rgb(18, 60, 137) // 124e89 — button pressed (blue shift)
#define COL_ACCENT rgb(0, 153, 219) // 2ce8f5 — cyan highlight
#define COL_TEXT rgb(255, 255, 255) // ffffff — white
#define COL_TEXT_DIM rgb(139, 155, 180) // 8b9bb4 — blue-grey

UIInteraction menu_button(const char *label) {
#define ID(s) hash_count((s), string_length((s)))
	const char *container_str = TextFormat("%s_container", label);
	uint64_t id = ID(container_str);
	UIInteraction interact = imgui_interact(id, imgui_content_region(id), WIDGET_FLAG_CLICKABLE);

	imgui_layout_begin(id, GROW(), FIT(), WIDGET_FLAG_CLICKABLE);
	{
		// Background shifts on state
		Color bg = COL_BG_MID;
		if (interact.hovering) {
			bg = COL_BG_HOVER;
		}
		if (interact.hover_entered) {
			PlaySound(tstate->sound_effects[SOUND_EFFECT_BUTTON_HOVER]);
		}
		if (interact.held)
			bg = COL_BG_HELD;
		if (interact.clicked)
			PlaySound(tstate->sound_effects[SOUND_EFFECT_BUTTON_PRESS]);

		imgui_background_color(bg);
		imgui_align_y(UI_ALIGN_CENTER);

		// Left accent bar: swap color on hover, shift on press
		uint64_t bar_id = ID(TextFormat("%s_bar", label));
		imgui_layout_begin(bar_id, FIT(4), GROW(), 0);
		{
			Color bar_color = interact.hovering ? COL_ACCENT : COL_TEXT_DIM;
			imgui_background_color(bar_color);
		}
		imgui_layout_end();

		// Nudge content down-right on press for tactile feel
		if (interact.held)
			imgui_offset(2, 2);

		imgui_align_y(UI_ALIGN_CENTER);
		imgui_align_x(UI_ALIGN_LEFT); // left-aligned text looks cleaner here
		imgui_padding_x(24);
		imgui_padding_y(14);

		// Label color brightens on hover
		Color text_col = interact.hovering ? WHITE : COL_TEXT;
		imgui_text(TextFormat("%s##3", label), 28, text_col);
	}
	imgui_layout_end();
#undef ID
	return interact;
}

void game_state_menu(void) {
	imgui_frame_begin(gui_state);
	gui_state->mouse_left = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
	gui_state->mouse_right = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
	gui_state->mouse_position = GetMousePosition();

	imgui_layout_begin(hash_array("root"), FIT(GetScreenWidth()), FIT(GetScreenHeight()), 0);
	{
		imgui_align_x(UI_ALIGN_CENTER);
		imgui_align_y(UI_ALIGN_CENTER);
		imgui_background_color(COL_BG_DARK);
		imgui_orientation(AXIS2_Y);
		imgui_child_gap(48);

		// ── Title block ──────────────────────────────────────────────────────
		imgui_layout_begin(hash_array("title"), FIT(), FIT(), 0);
		{
			imgui_orientation(AXIS2_Y);
			imgui_align_x(UI_ALIGN_CENTER);
			imgui_child_gap(8);

			// Main title — no background, large type
			imgui_text("GRID TACTICS", 80, WHITE);

			// Thin accent rule under the title
			imgui_layout_begin(hash_array("rule"), GROW(), FIT(2), 0);
			{
				imgui_background_color(COL_ACCENT);
			}
			imgui_layout_end();

			// Subtitle / tagline
			imgui_text("Turn-Based Strategy", 20, COL_TEXT_DIM);
		}

		imgui_layout_end();

		// ── Button panel ─────────────────────────────────────────────────────
		imgui_layout_begin(hash_array("buttons_container"), FIT(320), FIT(), 0);
		{
			imgui_orientation(AXIS2_Y);
			imgui_padding_xy(20);
			imgui_child_gap(4); // tight gaps — the bar does the visual separation

			if (menu_button("Play").clicked)
				pstate->state = GAME_STATE_PLAY;
			if (menu_button("Settings").clicked)
				TraceLog(LOG_INFO, "Go to settings");
			if (menu_button("Credits").clicked)
				TraceLog(LOG_INFO, "Go to credits");
		}
		imgui_layout_end();
	}
	imgui_layout_end();
	imgui_frame_end();
}

void game_state_play(void) {
	BeginMode3D(tstate->camera);
	float time = GetTime();

	DrawModel(tstate->map, (Vector3){ 0 }, 1.0f, WHITE);
	if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && pstate->move_animation.duration == 0.0f) {
		Ray ray = GetScreenToWorldRay(GetMousePosition(), tstate->camera);

		float grid_half = GRID_SIZE * 0.5f;
		float block_half = BLOCK_SIZE * 0.5f;

		BoundingBox box = {
			.min = { -grid_half * BLOCK_SIZE, -BLOCK_SIZE, -grid_half * BLOCK_SIZE },
			.max = { grid_half * BLOCK_SIZE, 0.0f, grid_half * BLOCK_SIZE }
		};
		RayCollision collision = GetRayCollisionBox(ray, box);

		if (collision.hit) {
			TraceLog(LOG_INFO, "Hit");

			int32x3 grid_pos = world_to_grid(collision.point);

			/* astar_path(&pstate->frame, pstate->player_position, collision.point); */
			TraceLog(LOG_INFO, "Grid position of hit: %d, %d, %d", grid_pos.x, grid_pos.y, grid_pos.z);

			pstate->selection_position = (Vector3){
				.x = grid_pos.x * BLOCK_SIZE + block_half,
				.y = grid_pos.y * BLOCK_SIZE - block_half,
				.z = grid_pos.z * BLOCK_SIZE + block_half,
			};

			pstate->move_animation.start = pstate->player_position;
			pstate->move_animation.target = pstate->selection_position;
			pstate->move_animation.target.y += block_half;

			float distance = Vector3Length(Vector3Subtract(pstate->move_animation.target, pstate->move_animation.start));
			pstate->move_animation.duration = distance / 16.0f;
			pstate->move_animation.t = 0.0f;

			pstate->selected = true;
		} else {
			TraceLog(LOG_INFO, "No Hit");
			pstate->selected = false;
		}
	}

	if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) {
		pstate->selected = false;
	}

	if (pstate->move_animation.duration > 0.0f) {
		pstate->move_animation.t += GetFrameTime();

		float t = pstate->move_animation.t / pstate->move_animation.duration;
		if (t >= 1.0f) {
			t = 1.0f;
			pstate->move_animation.duration = 0.0f;
		}

		pstate->player_position = Vector3Lerp(pstate->move_animation.start, pstate->move_animation.target, t);
		DrawBillboardPro(tstate->camera, tstate->atlas, sprite_to_uv_rect[SPRITE_PLAYER], pstate->player_position, tstate->camera.up, (Vector2){ BLOCK_SIZE, BLOCK_SIZE }, (Vector2){ BLOCK_SIZE * 0.5f, 0.0f }, 0.0f, WHITE);

	} else {
		DrawBillboardPro(tstate->camera, tstate->atlas, sprite_to_uv_rect[SPRITE_PLAYER], pstate->player_position, tstate->camera.up, (Vector2){ BLOCK_SIZE, BLOCK_SIZE }, (Vector2){ BLOCK_SIZE * 0.5f, 0.0f }, 0.0f, WHITE);
	}

	if (pstate->selected) {
		Vector3 pos = pstate->selection_position;

		pos.y += (BLOCK_SIZE * 0.5f) + (BLOCK_SIZE * 0.05f);

		Vector3 mesh_scale = Vector3Scale((Vector3){ 0.1f, 0.05f, 0.3f }, BLOCK_SIZE);
		Color selection_color = WHITE;

		DrawModelEx(pstate->selection, Vector3Add(pos, Vector3Scale((Vector3){ 0.5f, 0.0f, 0.4f }, BLOCK_SIZE)), (Vector3){ 0 }, 0.0f, mesh_scale, selection_color);
		DrawModelEx(pstate->selection, Vector3Add(pos, Vector3Scale((Vector3){ 0.4f, 0.0f, -0.5f }, BLOCK_SIZE)), (Vector3){ 0.0f, 1.0f, 0.0f }, 90.0f, mesh_scale, selection_color);
		DrawModelEx(pstate->selection, Vector3Add(pos, Vector3Scale((Vector3){ -0.5f, 0.0f, -0.4f }, BLOCK_SIZE)), (Vector3){ 0.0f, 1.0f, 0.0f }, -180.0f, mesh_scale, selection_color);
		DrawModelEx(pstate->selection, Vector3Add(pos, Vector3Scale((Vector3){ -0.4f, 0.0f, 0.5f }, BLOCK_SIZE)), (Vector3){ 0.0f, 1.0f, 0.0f }, -90.0f, mesh_scale, selection_color);
		DrawModelEx(pstate->selection, Vector3Add(pos, Vector3Scale((Vector3){ 0.4f, 0.0f, 0.5f }, BLOCK_SIZE)), (Vector3){ 0.0f, 1.0f, 0.0f }, 90.0f, mesh_scale, selection_color);
		DrawModelEx(pstate->selection, Vector3Add(pos, Vector3Scale((Vector3){ 0.5f, 0.0f, -0.4f }, BLOCK_SIZE)), (Vector3){ 0.0f, 1.0f, 0.0f }, -180.0f, mesh_scale, selection_color);
		DrawModelEx(pstate->selection, Vector3Add(pos, Vector3Scale((Vector3){ -0.4f, 0.0f, -0.5f }, BLOCK_SIZE)), (Vector3){ 0.0f, 1.0f, 0.0f }, -90.0f, mesh_scale, selection_color);
		DrawModelEx(pstate->selection, Vector3Add(pos, Vector3Scale((Vector3){ -0.5f, 0.0f, 0.4f }, BLOCK_SIZE)), (Vector3){ 0 }, 0.0f, mesh_scale, selection_color);
	}
	DrawBillboardPro(tstate->camera, tstate->atlas, sprite_to_uv_rect[SPRITE_PLAYER], pstate->player_position, tstate->camera.up, (Vector2){ BLOCK_SIZE, BLOCK_SIZE }, (Vector2){ BLOCK_SIZE * 0.5f, 0.0f }, 0.0f, WHITE);

	EndMode3D();
}
