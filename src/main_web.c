
#include "game/common.h"
#include <game_interface.h>
static GameContext context;

#ifdef PLATFORM_WEB
	#include <raylib.h>
	#include <stdlib.h>

	#include <emscripten/emscripten.h>
void update_and_draw(GameContext *ctx);

static GameContext ctx = { 0 };

static void loop(void) {
	update_and_draw(&ctx);
}

int main(void) {
	ctx.memory_size = MiB(8);
	ctx.memory = calloc(1, ctx.memory_size);
	InitWindow(800, 450, "game");
	emscripten_set_main_loop(loop, 0, 1);
}
#endif
