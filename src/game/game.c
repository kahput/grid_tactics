#include <stdbool.h>
#include <game_interface.h>

#include "debug.h"
#include "common.h"

#include <raylib.h>
#include <raymath.h>

typedef struct {
	uint8_t *base;
	uint64_t offset, capacity;
} ArenaAllocator;

void *allocate(ArenaAllocator *allocator, uint64_t size) {
	ASSERT(allocator->offset + size < allocator->capacity);
	void *result = allocator->base + allocator->offset;
	allocator->offset += size;

	return result;
}

#define arena_push_struct(arena, T) allocate((arena), sizeof(T))
#define arena_push_count(arena, count, T) allocate((arena), sizeof(T) * (count))

#define BLOCK_SIZE 4
#define GRID_SIZE 16

typedef struct {
	ArenaAllocator arena;
	ArenaAllocator frame;

	Camera3D camera;

	Texture2D atlas;

	Model selection;
	Model map;

	Vector3 selection_position;
	bool selected;

	Vector3 player_position;

	struct {
		Vector3 start, target;
		float duration, t;
	} move_animation;

	bool initialized;
} GameState;

typedef enum {
	CUBE_FACE_RIGHT,
	CUBE_FACE_LEFT,
	CUBE_FACE_TOP,
	CUBE_FACE_BOTTOM,
	CUBE_FACE_FRONT,
	CUBE_FACE_BACK,

	CUBE_FACE_MAX,
} CubeFace;

typedef enum {
	SPRITE_DUNGEON_FLOOR,
	SPRITE_DUNGEON_WALL,
	SPRITE_PLAYER,

	SPRITE_MAX,
} SpriteType;

typedef enum {
	BLOCK_TYPE_DUNGEON_FLOOR,
	BLOCK_TYPE_DUNGEON_WALL,

	BLOCK_TYPE_MAX,
} BlockType;

#define ATLAS_WIDTH 192
#define ATLAS_HEIGHT 176

Rectangle sprite_to_uv_rect[SPRITE_MAX] = {
	[SPRITE_DUNGEON_FLOOR] = { 16.0f, 64.f, 16.0f, 16.0f },
	[SPRITE_DUNGEON_WALL] = { 64.0f, 48.0f, 16.0f, 16.0f },
	[SPRITE_PLAYER] = { 16.0f, 112.0f, 16.0f, 16.0f },
};

typedef Mesh GridBatch;

void push_quad(Mesh *mesh, float x, float y, float z, SpriteType type, CubeFace face) {
	float half_size = BLOCK_SIZE * 0.5f;

	const float32x3 positions[8] = {
		[0] = { x + -half_size, y + half_size, z + half_size },
		[1] = { x + half_size, y + half_size, z + half_size },
		[2] = { x + -half_size, y + -half_size, z + half_size },
		[3] = { x + half_size, y + -half_size, z + half_size },
		[4] = { x + half_size, y + half_size, z + -half_size },
		[5] = { x + -half_size, y + half_size, z + -half_size },
		[6] = { x + half_size, y + -half_size, z + -half_size },
		[7] = { x + -half_size, y + -half_size, z + -half_size }
	};

	const float32x3 normals[6] = {
		[CUBE_FACE_RIGHT] = { 1.0f, 0.0f, 0.0f },
		[CUBE_FACE_LEFT] = { -1.0f, 0.0f, 0.0f },
		[CUBE_FACE_TOP] = { 0.0f, 1.0f, 0.0f },
		[CUBE_FACE_BOTTOM] = { 0.0f, -1.0f, 0.0f },
		[CUBE_FACE_FRONT] = { 0.0f, 0.0f, 1.0f },
		[CUBE_FACE_BACK] = { 0.0f, 0.0f, -1.0f }
	};

	Rectangle src = sprite_to_uv_rect[type];
	float32x2 image_size = { ATLAS_WIDTH, ATLAS_HEIGHT };

	float u0 = src.x / image_size.x;
	float v0 = src.y / image_size.y;
	float u1 = (src.x + src.width) / image_size.x;
	float v1 = (src.y + src.height) / image_size.y;

	const float32x2 uvs[6] = {
		{ u0, v0 },
		{ u0, v1 },
		{ u1, v0 },
		{ u1, v0 },
		{ u0, v1 },
		{ u1, v1 }
	};

	const uint8_t indices[6][6] = {
		[CUBE_FACE_RIGHT] = { 1, 3, 4, 4, 3, 6 },
		[CUBE_FACE_LEFT] = { 5, 7, 0, 0, 7, 2 },
		[CUBE_FACE_TOP] = { 1, 4, 0, 0, 4, 5 },
		[CUBE_FACE_BOTTOM] = { 2, 7, 3, 3, 7, 6 },
		[CUBE_FACE_FRONT] = { 0, 2, 1, 1, 2, 3 },
		[CUBE_FACE_BACK] = { 4, 6, 5, 5, 6, 7 }
	};

	float32x3 *position_vertices = ((float32x3 *)mesh->vertices) + mesh->vertexCount;
	float32x3 *normal_vertices = ((float32x3 *)mesh->normals) + mesh->vertexCount;
	float32x2 *uv_vertices = ((float32x2 *)mesh->texcoords) + mesh->vertexCount;

	for (int face_index = 0; face_index < 6; face_index++) {
		int index = indices[face][face_index];

		position_vertices[face_index] = positions[index];
		normal_vertices[face_index] = normals[face];
		uv_vertices[face_index] = uvs[face_index];
	}

	mesh->vertexCount += 6;
}

void push_cube(Mesh *mesh, float x, float y, float z, BlockType type) {
	push_quad(mesh, x, y, z, SPRITE_DUNGEON_WALL, CUBE_FACE_RIGHT);
	push_quad(mesh, x, y, z, SPRITE_DUNGEON_WALL, CUBE_FACE_LEFT);
	push_quad(mesh, x, y, z, SPRITE_DUNGEON_FLOOR, CUBE_FACE_TOP);
	push_quad(mesh, x, y, z, SPRITE_DUNGEON_FLOOR, CUBE_FACE_BOTTOM);
	push_quad(mesh, x, y, z, SPRITE_DUNGEON_WALL, CUBE_FACE_FRONT);
	push_quad(mesh, x, y, z, SPRITE_DUNGEON_WALL, CUBE_FACE_BACK);
}

void push_cube3(Mesh *mesh, float32x3 position, BlockType type) {
	push_cube(mesh, position.x, position.y, position.z, type);
}

void update_and_draw(GameContext *context) {
	GameState *state = (GameState *)context->memory;

	if (state->initialized == false) {
		state->arena.base = context->memory + sizeof(GameState);
		state->arena.capacity = context->memory_size - sizeof(GameState);

		state->frame.capacity = MiB(4);
		state->frame.base = malloc(state->frame.capacity);

		state->camera.position = (Vector3){ 64.0f, 56.0f, 64.0f }; // Camera position
		state->camera.target = (Vector3){ 0.0f, 0.0f, 0.0f }; // Camera looking at point
		state->camera.up = (Vector3){ 0.0f, 1.0f, 0.0f }; // Camera up vector (rotation towards target)

		state->atlas = LoadTexture("assets/Tilemap/tilemap_packed.png");

		Mesh map_mesh = { 0 };

		uint32_t max_vertices = GRID_SIZE * GRID_SIZE * GRID_SIZE * 36;
		map_mesh.vertices = arena_push_count(&state->arena, max_vertices, float32x3);
		map_mesh.normals = arena_push_count(&state->arena, max_vertices, float32x3);
		map_mesh.texcoords = arena_push_count(&state->arena, max_vertices, float32x2);

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

			push_cube3(&map_mesh, cube_pos, BLOCK_TYPE_DUNGEON_WALL);

			if (x == 0 || z == 0) {
				cube_pos.y += BLOCK_SIZE;
				push_cube3(&map_mesh, cube_pos, BLOCK_TYPE_DUNGEON_WALL);
				cube_pos.y += BLOCK_SIZE;
				push_cube3(&map_mesh, cube_pos, BLOCK_TYPE_DUNGEON_WALL);
			}
		}

		UploadMesh(&map_mesh, true);
		state->map = LoadModelFromMesh(map_mesh);
		state->map.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = state->atlas;

		state->selection = LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f));

		state->initialized = true;
	}

	state->camera.projection = CAMERA_ORTHOGRAPHIC; // Camera projection type
	state->camera.fovy = 64; // Camera field-of-view Y
	// TODO: Update your variables here

	// Draw
	BeginDrawing();

	ClearBackground(RAYWHITE);

	BeginMode3D(state->camera);

	DrawGrid(16, BLOCK_SIZE);

	float time = GetTime();

	DrawModel(state->map, (Vector3){ 0 }, 1.0f, WHITE);

	if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && state->move_animation.duration == 0.0f) {
		Ray ray = GetScreenToWorldRay(GetMousePosition(), state->camera);

		int32_t grid_half = GRID_SIZE * 0.5f;
		int32_t block_half = BLOCK_SIZE * 0.5f;

		BoundingBox box = {
			.min = { -grid_half * BLOCK_SIZE, -BLOCK_SIZE, -grid_half * BLOCK_SIZE },
			.max = { grid_half * BLOCK_SIZE, 0.0f, grid_half * BLOCK_SIZE }
		};
		RayCollision collision = GetRayCollisionBox(ray, box);

		if (collision.hit) {
			TraceLog(LOG_INFO, "Hit");

			int32_t grid_position[3] = {
				CLAMP(floorf(collision.point.x / BLOCK_SIZE), -grid_half + 1, grid_half - 1),
				collision.point.y / BLOCK_SIZE,
				CLAMP(floorf(collision.point.z / BLOCK_SIZE), -grid_half + 1, grid_half - 1),
			};

			TraceLog(LOG_INFO, "Grid position of hit: %d, %d, %d", grid_position[0], grid_position[1], grid_position[2]);

			state->selection_position = (Vector3){
				.x = grid_position[0] * BLOCK_SIZE + block_half,
				.y = grid_position[1] * BLOCK_SIZE - block_half,
				.z = grid_position[2] * BLOCK_SIZE + block_half,
			};

			state->move_animation.start = state->player_position;
			state->move_animation.target = state->selection_position;
			state->move_animation.target.y += block_half;

			float distance = Vector3Length(Vector3Subtract(state->move_animation.target, state->move_animation.start));
			state->move_animation.duration = distance / 16.0f;
			state->move_animation.t = 0.0f;

			state->selected = true;
		} else {
			TraceLog(LOG_INFO, "No Hit");
			state->selected = false;
		}
	}

	if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) {
		state->selected = false;
	}

	if (state->selected) {
		Vector3 pos = state->selection_position;

		pos.y += (BLOCK_SIZE * 0.5f) + (BLOCK_SIZE * 0.05f);

		float scale = BLOCK_SIZE - 1.0f;

		Vector3 mesh_scale = Vector3Scale((Vector3){ 0.1f, 0.05f, 0.3f }, BLOCK_SIZE);
		Color selection_color = WHITE;

		DrawModelEx(state->selection, Vector3Add(pos, Vector3Scale((Vector3){ 0.5f, 0.0f, 0.4f }, BLOCK_SIZE)), (Vector3){ 0 }, 0.0f, mesh_scale, selection_color);
		DrawModelEx(state->selection, Vector3Add(pos, Vector3Scale((Vector3){ 0.4f, 0.0f, -0.5f }, BLOCK_SIZE)), (Vector3){ 0.0f, 1.0f, 0.0f }, 90.0f, mesh_scale, selection_color);
		DrawModelEx(state->selection, Vector3Add(pos, Vector3Scale((Vector3){ -0.5f, 0.0f, -0.4f }, BLOCK_SIZE)), (Vector3){ 0.0f, 1.0f, 0.0f }, -180.0f, mesh_scale, selection_color);
		DrawModelEx(state->selection, Vector3Add(pos, Vector3Scale((Vector3){ -0.4f, 0.0f, 0.5f }, BLOCK_SIZE)), (Vector3){ 0.0f, 1.0f, 0.0f }, -90.0f, mesh_scale, selection_color);
		DrawModelEx(state->selection, Vector3Add(pos, Vector3Scale((Vector3){ 0.4f, 0.0f, 0.5f }, BLOCK_SIZE)), (Vector3){ 0.0f, 1.0f, 0.0f }, 90.0f, mesh_scale, selection_color);
		DrawModelEx(state->selection, Vector3Add(pos, Vector3Scale((Vector3){ 0.5f, 0.0f, -0.4f }, BLOCK_SIZE)), (Vector3){ 0.0f, 1.0f, 0.0f }, -180.0f, mesh_scale, selection_color);
		DrawModelEx(state->selection, Vector3Add(pos, Vector3Scale((Vector3){ -0.4f, 0.0f, -0.5f }, BLOCK_SIZE)), (Vector3){ 0.0f, 1.0f, 0.0f }, -90.0f, mesh_scale, selection_color);
		DrawModelEx(state->selection, Vector3Add(pos, Vector3Scale((Vector3){ -0.5f, 0.0f, 0.4f }, BLOCK_SIZE)), (Vector3){ 0 }, 0.0f, mesh_scale, selection_color);
	}

	if (state->move_animation.duration > 0.0f) {
		state->move_animation.t += GetFrameTime();

		float t = state->move_animation.t / state->move_animation.duration;
		if (t >= 1.0f) {
			t = 1.0f;
			state->move_animation.duration = 0.0f;
		}

		state->player_position = Vector3Lerp(state->move_animation.start, state->move_animation.target, t);
		DrawBillboardPro(state->camera, state->atlas, sprite_to_uv_rect[SPRITE_PLAYER], state->player_position, state->camera.up, (Vector2){ BLOCK_SIZE, BLOCK_SIZE }, (Vector2){ BLOCK_SIZE * 0.5f, 0.0f }, 0.0f, WHITE);

	} else {
		DrawBillboardPro(state->camera, state->atlas, sprite_to_uv_rect[SPRITE_PLAYER], state->player_position, state->camera.up, (Vector2){ BLOCK_SIZE, BLOCK_SIZE }, (Vector2){ BLOCK_SIZE * 0.5f, 0.0f }, 0.0f, WHITE);
	}

	EndMode3D();

	EndDrawing();

	state->frame.offset = 0;
}

void unload(GameContext *context) {
	GameState *state = (GameState *)context->memory;

	TraceLog(LOG_INFO, "Unloading assets");

	UnloadTexture(state->atlas);
}
