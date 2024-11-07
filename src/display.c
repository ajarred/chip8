#ifndef _DISPLAY_H
#define _DISPLAY_H

#include "display.h"

bool initialize_sdl(sdl_t* sdl) {
	if (SDL_SetAppMetadata("CHIP8", "1.0", "chip8-emu") == 0) {
		return SDL_SetError("Metadata not set: %s",
				SDL_GetError());
	}

	// debug
	#ifdef DEBUG
		printf("sdl metadata set\n");
	#endif

	if (SDL_InitSubSystem(SDL_INIT_VIDEO) == 0) {
		return SDL_SetError("Failed to initialize SDL. %s\n",
				SDL_GetError());
	}

	#ifdef DEBUG
		printf("sdl initialized\n");
	#endif

	const uint16_t window_width = 1280;
	const uint16_t window_height = 640;
	sdl->window = SDL_CreateWindow(
			"CHIP8",
			window_width,
			window_height,
			SDL_WINDOW_OPENGL
	);

	if (sdl->window == NULL) {
		return SDL_SetError("Could not create window: %s\n",
				SDL_GetError());
	}

	#ifdef DEBUG
		printf("window initialized\n");
	#endif

	sdl->renderer = SDL_CreateRenderer(sdl->window, NULL);

	if (sdl->renderer == NULL) {
		return SDL_SetError("Could not create renderer: %s\n",
				SDL_GetError());
	}

	#ifdef DEBUG
		printf("renderer initialized\n");
	#endif

	SDL_RenderClear(sdl->renderer);

	return true;
}

#define RGBA_WHITE 0xFF, 0xFF, 0xFF, 0xFF
#define RGBA_BLACK 0x0, 0x0, 0x0, 0xFF

void clear_screen(const sdl_t sdl) {
	SDL_SetRenderDrawColor(sdl.renderer, RGBA_BLACK);
	SDL_RenderClear(sdl.renderer);
}

void update_screen(const sdl_t sdl, const chip8_t* chip8) {
        const int rows = 32;
        const int columns = 64;
        SDL_FRect rect;
        SDL_Rect darea;

        SDL_GetRenderViewport(sdl.renderer, &darea);
        SDL_SetRenderDrawColor(sdl.renderer, RGBA_BLACK);
        SDL_RenderClear(sdl.renderer);

	rect.w = (float)darea.w / columns;
	rect.h = (float)darea.h / rows;

        for (int row = 0; row < rows; row++) {
                int x = 0;
                for (int column = 0; column < columns; column++) {
                        if (chip8->display[row][column] == 1)
                                SDL_SetRenderDrawColor(sdl.renderer,
						RGBA_WHITE);
                        else
                                SDL_SetRenderDrawColor(sdl.renderer,
						RGBA_BLACK);

			rect.x = x * rect.w;
			rect.y = row * rect.h;

                        x++;

                        SDL_RenderFillRect(sdl.renderer, &rect);
                }
        }

        SDL_RenderPresent(sdl.renderer);
}

void terminate_sdl(const sdl_t sdl) {
	SDL_DestroyRenderer(sdl.renderer);
	SDL_DestroyWindow(sdl.window);
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
	SDL_Quit();

	#ifdef DEBUG
		printf("sdl terminated\n");
	#endif
}

#endif
