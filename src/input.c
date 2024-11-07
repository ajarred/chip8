#ifndef _INPUT_H
#define _INPUT_H

#include "input.h"

/*
chip8 keypad
1 2 3 C
4 5 6 D
7 8 9 E
A 0 B F
-------
mapped
1 2 3 4
Q W E R
A S D F
Z X C V
*/

void handle_input_event(chip8_t* chip8, const char file_path[]) {
	SDL_Event event;

	while (SDL_PollEvent(&event)) {
		switch (event.type) {
			case SDL_EVENT_QUIT:
				chip8->state = QUIT;
				break;

			case SDL_EVENT_KEY_DOWN:
				switch(event.key.scancode) {
					case SDL_SCANCODE_ESCAPE:
						chip8->state = QUIT;
						break;

					case SDL_SCANCODE_SPACE:
						if (chip8->state == RUNNING)
							chip8->state = PAUSED;
						else
							chip8->state = RUNNING;
						break;

					case SDL_SCANCODE_EQUALS:
						chip8_initialize(file_path, chip8);
						#ifdef DEBUG
							printf("reset\n");
						#endif 
						break;

					case SDL_SCANCODE_1:
						chip8->keypad[0x1] = 1;
						break;
					case SDL_SCANCODE_2:
						chip8->keypad[0x2] = 1;
						break;
					case SDL_SCANCODE_3:
						chip8->keypad[0x3] = 1;
						break;
					case SDL_SCANCODE_4:
						chip8->keypad[0xC] = 1;
						break;

					case SDL_SCANCODE_Q:
						chip8->keypad[0x4] = 1;
						break;
					case SDL_SCANCODE_W:
						chip8->keypad[0x5] = 1;
						break;
					case SDL_SCANCODE_E:
						chip8->keypad[0x6] = 1;
						break;
					case SDL_SCANCODE_R:
						chip8->keypad[0xD] = 1;
						break;

					case SDL_SCANCODE_A:
						chip8->keypad[0x7] = 1;
						break;
					case SDL_SCANCODE_S:
						chip8->keypad[0x8] = 1;
						break;
					case SDL_SCANCODE_D:
						chip8->keypad[0x9] = 1;
						break;
					case SDL_SCANCODE_F:
						chip8->keypad[0xE] = 1;
						break;

					case SDL_SCANCODE_Z:
						chip8->keypad[0xA] = 1;
						break;
					case SDL_SCANCODE_X:
						chip8->keypad[0x0] = 1;
						break;
					case SDL_SCANCODE_C:
						chip8->keypad[0xB] = 1;
						break;
					case SDL_SCANCODE_V:
						chip8->keypad[0xF] = 1;
						break;

					default:
						break;
				}
				break;

			case SDL_EVENT_KEY_UP:
				switch(event.key.scancode) {
					case SDL_SCANCODE_1:
						chip8->keypad[0x1] = 0;
						break;
					case SDL_SCANCODE_2:
						chip8->keypad[0x2] = 0;
						break;
					case SDL_SCANCODE_3:
						chip8->keypad[0x3] = 0;
						break;
					case SDL_SCANCODE_4:
						chip8->keypad[0xC] = 0;
						break;

					case SDL_SCANCODE_Q:
						chip8->keypad[0x4] = 0;
						break;
					case SDL_SCANCODE_W:
						chip8->keypad[0x5] = 0;
						break;
					case SDL_SCANCODE_E:
						chip8->keypad[0x6] = 0;
						break;
					case SDL_SCANCODE_R:
						chip8->keypad[0xD] = 0;
						break;

					case SDL_SCANCODE_A:
						chip8->keypad[0x7] = 0;
						break;
					case SDL_SCANCODE_S:
						chip8->keypad[0x8] = 0;
						break;
					case SDL_SCANCODE_D:
						chip8->keypad[0x9] = 0;
						break;
					case SDL_SCANCODE_F:
						chip8->keypad[0xE] = 0;
						break;

					case SDL_SCANCODE_Z:
						chip8->keypad[0xA] = 0;
						break;
					case SDL_SCANCODE_X:
						chip8->keypad[0x0] = 0;
						break;
					case SDL_SCANCODE_C:
						chip8->keypad[0xB] = 0;
						break;
					case SDL_SCANCODE_V:
						chip8->keypad[0xF] = 0;
						break;

					default:
						break;
				}
				break;

			default:
				break;
		}
	}
}

#endif
