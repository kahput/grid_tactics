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

typedef enum {
	ENTITY_TRAIT_MOVABLE = 0x1,
	ENTITY_TRAIT_DRAGGABLE = 0x2,
	ENTITY_TRAIT_BILLBOARD = 0x4,
} EntityTraitFlags;

typedef struct {
	EntityTraitFlags trait;

	Vector3 position;

	bool moving;
	struct {
		Vector3 start, target;
		float duration, t;
	} move_animation;

	uint32_t next_tether_target, prev_tether_target;
} Entity;

#define MAX_ENTITIES 8
typedef struct {
	ArenaAllocator arena;
	ArenaAllocator frame;

	Model selection;

	Vector3 selection_position;

	uint32_t active_entity;
	uint32_t hot_entity;
	uint32_t last_moved;

	Entity entities[MAX_ENTITIES];
	uint32_t entity_count;

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

	// assets
	Sound sound_effects[SOUND_EFFECT_MAX];
	Shader billboard_shader;

	// :init

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

#define GET_MACRO3(_1, _2, _3, NAME, ...) NAME

#define vec3_3(x, y, z) \
	(Vector3) { (x), (y), (z) }
#define vec3_2(x, y) vec3_3(x, y, 0)
#define vec3_1(v) vec3_3(v, v, v)

#define vec3(...) GET_MACRO3(__VA_ARGS__, vec3_3, vec3_2, vec3_1, _)(__VA_ARGS__)

static inline BoundingBox box_from_size(float width, float height, float depth) {
	return (BoundingBox){ { -(width * 0.5f), -(height * 0.5f), -(depth * 0.5f) }, { (width * 0.5f), (height * 0.5f), (depth * 0.5f) } };
}
static inline BoundingBox box_offset(BoundingBox box, float x, float y, float z) {
	BoundingBox result = { 0 };
	result.min.x = box.min.x + x;
	result.min.y = box.min.y + y;
	result.min.z = box.min.z + z;

	result.max.x = box.max.x + x;
	result.max.y = box.max.y + y;
	result.max.z = box.max.z + z;

	return result;
}

static inline BoundingBox box_offset3v(BoundingBox box, Vector3 offset) {
	return box_offset(box, offset.x, offset.y, offset.z);
}

int32x3 world_to_grid(Vector3 point) {
	float grid_half = GRID_SIZE * 0.5f;

	int32x3 result = {
		CLAMP(point.x / BLOCK_SIZE, -grid_half, grid_half - 1),
		CLAMP(point.y / BLOCK_SIZE, 0, 1),
		CLAMP(point.z / BLOCK_SIZE, -grid_half, grid_half - 1),
	};

	return result;
}

Vector3 grid_to_world(int32_t x, int32_t y, int32_t z) {
	float grid_half = GRID_SIZE * 0.5f;
	float block_half = BLOCK_SIZE * 0.5f;

	Vector3 result = {
		.x = CLAMP(x * BLOCK_SIZE + block_half, -grid_half * BLOCK_SIZE, grid_half * BLOCK_SIZE),
		.y = CLAMP(y * BLOCK_SIZE + block_half, 0, 1),
		.z = CLAMP(z * BLOCK_SIZE + block_half, -grid_half * BLOCK_SIZE, grid_half * BLOCK_SIZE),
	};

	return result;
}

static inline Entity *entity_make(int32_t x, int32_t y, int32_t z, EntityTraitFlags trait_flags) {
	Entity *result = &pstate->entities[pstate->entity_count++];
	result->trait = trait_flags;
	result->position = grid_to_world(x, y, z);

	return result;
}

static inline uint32_t manhatten_distance(int32x3 a, int32x3 b) {
	return abs(a.x - b.x) + abs(a.y - b.y) + abs(a.z - b.z);
}

void ensure_distance(Entity *moved, uint32_t max_distance) {
	if (moved->next_tether_target == indexof(pstate->entities, moved))
		return;

	Entity *prev = moved;
	Entity *curr = &pstate->entities[moved->next_tether_target];
	do {
		int32x3 prev_grid = world_to_grid(prev->position);
		int32x3 curr_grid = world_to_grid(curr->position);

		uint32_t distance = manhatten_distance(prev_grid, curr_grid);

		if (curr->moving == false && distance > max_distance) {
			int dx = prev_grid.x - curr_grid.x;
			int dz = prev_grid.z - curr_grid.z;

			Vector3 target = curr->position;

			// 1. Calculate which axis the leader ('prev') moved along
			float prev_anim_dx = fabsf(prev->move_animation.target.x - prev->move_animation.start.x);
			float prev_anim_dz = fabsf(prev->move_animation.target.z - prev->move_animation.start.z);

			bool prioritize_x = true;
			if (prev_anim_dx == 0.0f && prev_anim_dz == 0.0f) {
				// Fallback if the leader has no animation data: use the larger delta
				prioritize_x = (abs(dx) <= abs(dz));
			} else {
				prioritize_x = (prev_anim_dx <= prev_anim_dz);
			}

			// 2. Step 1 block along the prioritized axis if a gap exists
			if (prioritize_x) {
				if (dx != 0) {
					target.x += (dx > 0 ? BLOCK_SIZE : -BLOCK_SIZE);
				} else if (dz != 0) {
					target.z += (dz > 0 ? BLOCK_SIZE : -BLOCK_SIZE);
				}
			} else {
				if (dz != 0) {
					target.z += (dz > 0 ? BLOCK_SIZE : -BLOCK_SIZE);
				} else if (dx != 0) {
					target.x += (dx > 0 ? BLOCK_SIZE : -BLOCK_SIZE);
				}
			}

			curr->move_animation.start = curr->position;
			curr->move_animation.target = target;

			float dist = Vector3Length(Vector3Subtract(curr->move_animation.target, curr->move_animation.start));
			curr->move_animation.duration = dist / 8.0f;
			curr->move_animation.t = 0.0f;
			curr->moving = true;
		}

		prev = curr;
		curr = &pstate->entities[curr->next_tether_target];
	} while (curr != moved);
}

void update_and_draw(GameContext *context) {
	pstate = (PermanentState *)context->memory;
	tstate = (TransientState *)context->transient_memory;

	if (pstate->initialized == false) {
		pstate->arena.base = context->memory + sizeof(PermanentState);
		pstate->arena.capacity = context->memory_size - sizeof(PermanentState);

		pstate->frame.capacity = MiB(4);
		pstate->frame.base = malloc(pstate->frame.capacity);

		pstate->selection = LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f));

		pstate->entity_count++; // 0 == invalid entity

		Entity *player = entity_make(0, 0, 0, ENTITY_TRAIT_MOVABLE | ENTITY_TRAIT_BILLBOARD);
		Entity *block0 = entity_make(0, 0, 0, ENTITY_TRAIT_DRAGGABLE);
		Entity *block1 = entity_make(4, 0, 2, ENTITY_TRAIT_DRAGGABLE);
		block0->next_tether_target = block0->prev_tether_target = indexof(pstate->entities, block1);
		block1->next_tether_target = block1->prev_tether_target = indexof(pstate->entities, block0);

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

		tstate->billboard_shader = LoadShader(
			NULL,
			TextFormat("assets/shaders/glsl%i/billboard.fs", GLSL_VERSION));
		// :shader

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

	UnloadShader(tstate->billboard_shader);

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
		imgui_anchor(IMGUI_ANCHOR_TOP);

		uint64_t bar_id = ID(TextFormat("%s_bar", label));
		imgui_layout_begin(bar_id, FIT(4), GROW(), 0);
		{
			Color bar_color = interact.hovering ? COL_ACCENT : COL_TEXT_DIM;
			imgui_background_color(bar_color);
		}
		imgui_layout_end();

		if (interact.held)
			imgui_offset(2, 2);

		imgui_anchor(IMGUI_ANCHOR_LEFT);
		imgui_padding_x(24);
		imgui_padding_y(14);

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
		imgui_anchor(IMGUI_ANCHOR_CENTER);
		imgui_background_color(COL_BG_DARK);
		imgui_orientation(AXIS2_Y);
		imgui_child_gap(48);

		imgui_layout_begin(hash_array("title"), FIT(), FIT(), 0);
		{
			imgui_orientation(AXIS2_Y);
			imgui_anchor(IMGUI_ANCHOR_TOP);
			imgui_child_gap(8);

			imgui_text("GRID TACTICS", 80, WHITE);

			imgui_layout_begin(hash_array("rule"), GROW(), FIT(2), 0);
			{
				imgui_background_color(COL_ACCENT);
			}
			imgui_layout_end();

			imgui_text("Turn-Based Strategy", 20, COL_TEXT_DIM);
		}

		imgui_layout_end();

		imgui_layout_begin(hash_array("buttons_container"), FIT(320), FIT(), 0);
		{
			imgui_orientation(AXIS2_Y);
			imgui_padding_xy(20);
			imgui_child_gap(4);

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

	// :play
	DrawModel(tstate->map, (Vector3){ 0 }, 1.0f, WHITE);

	Ray camera_ray = GetScreenToWorldRay(GetMousePosition(), tstate->camera);

	bool mouse_pressed = IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
	bool mouse_down = IsMouseButtonDown(MOUSE_LEFT_BUTTON);

	BoundingBox map_bounding_box = box_from_size(GRID_SIZE * BLOCK_SIZE, BLOCK_SIZE, GRID_SIZE * BLOCK_SIZE);
	map_bounding_box.max.y -= BLOCK_SIZE * 0.5f;
	map_bounding_box.min.y -= BLOCK_SIZE * 0.5f;

	if (pstate->last_moved)
		ensure_distance(&pstate->entities[pstate->last_moved], 4);

	for (uint32_t index = 0; index < countof(pstate->entities); ++index) {
		Entity *entity = &pstate->entities[index];

		if (entity->moving) {
			entity->move_animation.t += GetFrameTime();

			float t = entity->move_animation.t / entity->move_animation.duration;
			if (t >= 1.0f) {
				t = 1.0f;
				entity->moving = false;
			}

			entity->position = Vector3Lerp(entity->move_animation.start, entity->move_animation.target, t);

		}

		else if (FLAG_GET(entity->trait, ENTITY_TRAIT_DRAGGABLE)) {
			if (pstate->active_entity == index) {
				int32x3 mouse_grid = world_to_grid(GetRayCollisionBox(camera_ray, map_bounding_box).point);
				Vector3 target = grid_to_world(mouse_grid.x, mouse_grid.y, mouse_grid.z);

				Vector3 difference = Vector3Subtract(entity->position, target);
				if (fabsf(difference.x) > fabsf(difference.z))
					target.z = entity->position.z;
				else
					target.x = entity->position.x;

				DrawLine3D(entity->position, target, GREEN);
			} else {
				BoundingBox block_bounds = box_offset3v(box_from_size(1.0f, 1.0f, 1.0f), entity->position);
				RayCollision ray_box_collision = GetRayCollisionBox(camera_ray, block_bounds);

				if (ray_box_collision.hit) {
					pstate->hot_entity = index;

					if (pstate->active_entity == 0 && IsMouseButtonDown(MOUSE_LEFT_BUTTON))
						pstate->active_entity = index;
				}
			}

			if (pstate->active_entity == index && mouse_down == false) {
				int32x3 mouse_grid = world_to_grid(GetRayCollisionBox(camera_ray, map_bounding_box).point);
				Vector3 target = grid_to_world(mouse_grid.x, mouse_grid.y, mouse_grid.z);

				Vector3 difference = Vector3Subtract(entity->position, target);
				if (fabsf(difference.x) > fabsf(difference.z))
					target.z = entity->position.z;
				else
					target.x = entity->position.x;

				entity->move_animation.start = entity->position;
				entity->move_animation.target = target;

				float distance = Vector3Length(Vector3Subtract(entity->move_animation.target, entity->move_animation.start));
				entity->move_animation.duration = distance / 8.0f;
				entity->move_animation.t = 0.0f;

				entity->moving = true;
				pstate->last_moved = index;
			}
		} /*else if (FLAG_GET(entity->trait, ENTITY_TRAIT_MOVABLE)) { */
		/* 	if (mouse_pressed && pstate->hot_entity == 0 && pstate->active_entity == 0) { */
		/* 		float grid_half = GRID_SIZE * 0.5f; */
		/* 		float block_half = BLOCK_SIZE * 0.5f; */

		/* 		RayCollision ray_map_collision = GetRayCollisionBox(camera_ray, map_bounding_box); */

		/* 		if (ray_map_collision.hit) { */
		/* 			int32x3 grid_pos = world_to_grid(ray_map_collision.point); */

		/* 			TraceLog(LOG_INFO, "Grid position of hit: %d, %d, %d", grid_pos.x, grid_pos.y, grid_pos.z); */

		/* 			pstate->selection_position = (Vector3){ */
		/* 				.x = grid_pos.x * BLOCK_SIZE + block_half, */
		/* 				.y = grid_pos.y * BLOCK_SIZE - block_half, */
		/* 				.z = grid_pos.z * BLOCK_SIZE + block_half, */
		/* 			}; */

		/* 			entity->move_animation.start = entity->position; */
		/* 			entity->move_animation.target = pstate->selection_position; */
		/* 			entity->move_animation.target.y += block_half; */

		/* 			float distance = Vector3Length(Vector3Subtract(entity->move_animation.target, entity->move_animation.start)); */
		/* 			entity->move_animation.duration = distance / 8.0f; */
		/* 			entity->move_animation.t = 0.0f; */

		/* 			entity->moving = true; */
		/* 		} */
		/* 	} */
		/* } */
	}

	for (uint32_t index = 1; index < pstate->entity_count; ++index) {
		Entity entity = pstate->entities[index];

		if (FLAG_GET(entity.trait, ENTITY_TRAIT_BILLBOARD)) {
			BeginShaderMode(tstate->billboard_shader);
			DrawBillboardPro(tstate->camera, tstate->atlas, sprite_to_uv_rect[SPRITE_PLAYER], entity.position, tstate->camera.up, (Vector2){ BLOCK_SIZE, BLOCK_SIZE }, (Vector2){ BLOCK_SIZE * 0.5f, 0.0f }, 0.0f, WHITE);
			EndShaderMode();
		} else {
			Color box_color = RED;
			if (pstate->hot_entity == index)
				box_color = GREEN;
			if (pstate->active_entity == index)
				box_color = DARKGREEN;

			DrawCubeV(entity.position, vec3(BLOCK_SIZE), box_color);
		}
	}

	/* if (pstate->selected) { */
	/* 	Vector3 pos = pstate->selection_position; */

	/* 	pos.y += (BLOCK_SIZE * 0.5f) + (BLOCK_SIZE * 0.05f); */

	/* 	Vector3 mesh_scale = Vector3Scale((Vector3){ 0.1f, 0.05f, 0.3f }, BLOCK_SIZE); */
	/* 	Color selection_color = WHITE; */

	/* 	DrawModelEx(pstate->selection, Vector3Add(pos, Vector3Scale((Vector3){ 0.5f, 0.0f, 0.4f }, BLOCK_SIZE)), (Vector3){ 0 }, 0.0f, mesh_scale, selection_color); */
	/* 	DrawModelEx(pstate->selection, Vector3Add(pos, Vector3Scale((Vector3){ 0.4f, 0.0f, -0.5f }, BLOCK_SIZE)), (Vector3){ 0.0f, 1.0f, 0.0f }, 90.0f, mesh_scale, selection_color); */
	/* 	DrawModelEx(pstate->selection, Vector3Add(pos, Vector3Scale((Vector3){ -0.5f, 0.0f, -0.4f }, BLOCK_SIZE)), (Vector3){ 0.0f, 1.0f, 0.0f }, -180.0f, mesh_scale, selection_color); */
	/* 	DrawModelEx(pstate->selection, Vector3Add(pos, Vector3Scale((Vector3){ -0.4f, 0.0f, 0.5f }, BLOCK_SIZE)), (Vector3){ 0.0f, 1.0f, 0.0f }, -90.0f, mesh_scale, selection_color); */
	/* 	DrawModelEx(pstate->selection, Vector3Add(pos, Vector3Scale((Vector3){ 0.4f, 0.0f, 0.5f }, BLOCK_SIZE)), (Vector3){ 0.0f, 1.0f, 0.0f }, 90.0f, mesh_scale, selection_color); */
	/* 	DrawModelEx(pstate->selection, Vector3Add(pos, Vector3Scale((Vector3){ 0.5f, 0.0f, -0.4f }, BLOCK_SIZE)), (Vector3){ 0.0f, 1.0f, 0.0f }, -180.0f, mesh_scale, selection_color); */
	/* 	DrawModelEx(pstate->selection, Vector3Add(pos, Vector3Scale((Vector3){ -0.4f, 0.0f, -0.5f }, BLOCK_SIZE)), (Vector3){ 0.0f, 1.0f, 0.0f }, -90.0f, mesh_scale, selection_color); */
	/* 	DrawModelEx(pstate->selection, Vector3Add(pos, Vector3Scale((Vector3){ -0.5f, 0.0f, 0.4f }, BLOCK_SIZE)), (Vector3){ 0 }, 0.0f, mesh_scale, selection_color); */
	/* } */

	EndMode3D();

	pstate->hot_entity = 0;
	if (pstate->active_entity && IsMouseButtonDown(MOUSE_LEFT_BUTTON) == false)
		pstate->active_entity = 0;
	else if (pstate->active_entity == false && IsMouseButtonDown(MOUSE_LEFT_BUTTON) == true)
		pstate->active_entity = (uint32_t)-1;
}
