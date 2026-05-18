#pragma once
#include <raylib.h>
#include "common.h"

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
	BLOCK_TYPE_FLOOR,
	BLOCK_TYPE_WALL,
    BLOCK_TYPE_SLOPE,

	BLOCK_TYPE_MAX,
} BlockType;

static Rectangle sprite_to_uv_rect[SPRITE_MAX] = {
	[SPRITE_DUNGEON_FLOOR] = { 16.0f, 64.f, 16.0f, 16.0f },
	[SPRITE_DUNGEON_WALL] = { 64.0f, 48.0f, 16.0f, 16.0f },
	[SPRITE_PLAYER] = { 16.0f, 112.0f, 16.0f, 16.0f },
};

void push_slope(Mesh *mesh, float x, float y, float z, SpriteType type);
void push_cube_face(Mesh *mesh, float x, float y, float z, SpriteType type, CubeFace face);
void push_block(Mesh *mesh, float x, float y, float z, BlockType type);
void push_block3(Mesh *mesh, float32x3 position, BlockType type); 
