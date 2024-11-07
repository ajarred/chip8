#include <SDL3/SDL.h>
#include "memory.h"

typedef struct {
	SDL_Window* window;
	SDL_Renderer* renderer;
} sdl_t;

bool initialize_sdl(sdl_t* sdl);

void clear_screen(const sdl_t sdl);

void update_screen(const sdl_t sdl, const chip8_t* chip8);

void terminate_sdl(const sdl_t sdl);
