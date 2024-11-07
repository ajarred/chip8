#include "memory.h"
#include "display.h"
#include "input.h"

int main(int argc, char** argv) {
	(void) argc;

	if (argc < 2) {
		fprintf(stderr,
			"Error: invalid arguments. "
		        "Usage: %s [rom]\n",
		        argv[0]);
		return 1;
	}

	chip8_t chip8 = {0};
	if (!chip8_initialize(argv[1], &chip8))
		return 1;

	sdl_t sdl = {0};
	if (!initialize_sdl(&sdl))
		return 1;

	clear_screen(sdl);

	srand(time(NULL));
	
	while (chip8.state != QUIT) {
		handle_input_event(&chip8, argv[1]);

		if (chip8.state == PAUSED)
			continue;

		const uint16_t insts_per_second = 700 / 60;

		const uint64_t start = SDL_GetPerformanceCounter();
		
		for (uint32_t i = 0; i < insts_per_second; i++) 
			run_instruction(&chip8); 
		
		const uint64_t end = SDL_GetPerformanceCounter();
		const double time_elapsed = (double) ((end - start) * 1000) / SDL_GetPerformanceFrequency();

		const double delay_60hz = 16.67 > time_elapsed ? 16.67 - time_elapsed : 0;
		SDL_Delay(delay_60hz);

		if (chip8.draw) {
			update_screen(sdl, &chip8);
			chip8.draw = false;
		}

		update_timers(&chip8);
	}

	terminate_sdl(sdl);	

	return 0;
}
