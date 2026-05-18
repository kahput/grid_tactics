#include "geometry.h"
#include "debug.h"
#include "globals.h"

void push_cube_face(Mesh *mesh, float x, float y, float z, SpriteType type, CubeFace face) {
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
		[CUBE_FACE_TOP] = { 5, 0, 4, 4, 0, 1 },
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

void push_block(Mesh *mesh, float x, float y, float z, BlockType type) {
	switch (type) {
		case BLOCK_TYPE_FLOOR:
			push_cube_face(mesh, x, y, z, SPRITE_DUNGEON_FLOOR, CUBE_FACE_RIGHT);
			push_cube_face(mesh, x, y, z, SPRITE_DUNGEON_FLOOR, CUBE_FACE_LEFT);
			push_cube_face(mesh, x, y, z, SPRITE_DUNGEON_FLOOR, CUBE_FACE_TOP);
			push_cube_face(mesh, x, y, z, SPRITE_DUNGEON_FLOOR, CUBE_FACE_BOTTOM);
			push_cube_face(mesh, x, y, z, SPRITE_DUNGEON_FLOOR, CUBE_FACE_FRONT);
			push_cube_face(mesh, x, y, z, SPRITE_DUNGEON_FLOOR, CUBE_FACE_BACK);
			break;
		case BLOCK_TYPE_WALL:
			push_cube_face(mesh, x, y, z, SPRITE_DUNGEON_WALL, CUBE_FACE_RIGHT);
			push_cube_face(mesh, x, y, z, SPRITE_DUNGEON_WALL, CUBE_FACE_LEFT);
			push_cube_face(mesh, x, y, z, SPRITE_DUNGEON_WALL, CUBE_FACE_TOP);
			push_cube_face(mesh, x, y, z, SPRITE_DUNGEON_WALL, CUBE_FACE_BOTTOM);
			push_cube_face(mesh, x, y, z, SPRITE_DUNGEON_WALL, CUBE_FACE_FRONT);
			push_cube_face(mesh, x, y, z, SPRITE_DUNGEON_WALL, CUBE_FACE_BACK);
			break;
		case BLOCK_TYPE_SLOPE:
			push_cube_face(mesh, x, y, z, SPRITE_DUNGEON_WALL, CUBE_FACE_RIGHT);
			push_cube_face(mesh, x, y, z, SPRITE_DUNGEON_WALL, CUBE_FACE_LEFT);
			push_cube_face(mesh, x, y, z, SPRITE_DUNGEON_WALL, CUBE_FACE_TOP);
			push_cube_face(mesh, x, y, z, SPRITE_DUNGEON_WALL, CUBE_FACE_BOTTOM);
			push_cube_face(mesh, x, y, z, SPRITE_DUNGEON_WALL, CUBE_FACE_FRONT);
			push_cube_face(mesh, x, y, z, SPRITE_DUNGEON_WALL, CUBE_FACE_BACK);
			break;
		case BLOCK_TYPE_MAX:
			ASSERT(false);
			break;
	}
}

void push_block3(Mesh *mesh, float32x3 position, BlockType type) {
	push_block(mesh, position.x, position.y, position.z, type);
}
