
#include "game/common.h"
#ifdef PLATFORM_DESKTOP
	#include <raylib.h>
	#include <dlfcn.h>
	#include <time.h>
	#include <stdlib.h>
	#include <stdint.h>
	#include <game_interface.h>

static void *game_handle;
static uint64_t game_last_write;
static void (*update_and_draw)(GameContext *ctx);
static void (*unload)(GameContext *ctx);

static bool game_reload(void) {
	if (game_handle)
		dlclose(game_handle);
	if (FileExists("loaded_game.so"))
		FileRemove("loaded_game.so");
	FileCopy("libgame.so", "loaded_game.so");

	game_handle = dlopen("./loaded_game.so", RTLD_NOW);
	if (!game_handle) {
		TraceLog(LOG_ERROR, "dlopen: %s", dlerror());
		return false;
	}

	game_last_write = GetFileModTime("libgame.so");
	*(void **)(&update_and_draw) = dlsym(game_handle, "update_and_draw");
	if (!update_and_draw) {
		TraceLog(LOG_ERROR, "dlsym: %s", dlerror());
		return false;
	}
	*(void **)(&unload) = dlsym(game_handle, "unload");
	if (!unload) {
		TraceLog(LOG_ERROR, "dlsym: %s", dlerror());
		return false;
	}
	return true;
}

int main(void) {
	SetConfigFlags(FLAG_WINDOW_RESIZABLE);
	InitWindow(800, 450, "game");
	SetTargetFPS(60);

	GameContext ctx = {
		.memory_size = MiB(8),
		.memory = calloc(1, MiB(8)),
	};
	if (game_reload() == false)
		return -1;

	while (!WindowShouldClose()) {
		uint64_t mtime = GetFileModTime("libgame.so");
		if (mtime != game_last_write) {
			nanosleep(&(struct timespec){ .tv_nsec = 100000000 }, NULL);
			game_reload();
		}
		if (update_and_draw)
			update_and_draw(&ctx);
	}

	unload(&ctx);
	free(ctx.memory);
	CloseWindow();
}

#endif
