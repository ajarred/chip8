#include "memory.h"

bool chip8_initialize(const char* file_path, chip8_t* chip8) {
	FILE* rom_file;
	const int max_filesize = 4096;

	#ifdef DEBUG
		printf("rom path: %s\n", file_path);
	#endif

	rom_file = fopen(file_path, "rb");

	if (rom_file == NULL) {
		fprintf(stderr, "Error: Cannot open ROM file\n");
		return false;
	}

	fseek(rom_file, 0, SEEK_END);
	const long rom_size = ftell(rom_file);
	rewind(rom_file);

	if (rom_size > max_filesize) {
		fprintf(stderr, "Error: ROM file is too large\n");
		fclose(rom_file);
		return false;
	}

	#ifdef DEBUG
		printf("File length: %ld bytes\n", rom_size);
	#endif
	memset(chip8->ram, 0, max_filesize);

	const uint8_t fonts[] = {
		0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
		0x20, 0x60, 0x20, 0x20, 0x70, // 1
		0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
		0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
		0x90, 0x90, 0xF0, 0x10, 0x10, // 4
		0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
		0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
		0xF0, 0x10, 0x20, 0x40, 0x40, // 7
		0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
		0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
		0xF0, 0x90, 0xF0, 0x90, 0x90, // A
		0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
		0xF0, 0x80, 0x80, 0x80, 0xF0, // C
		0xE0, 0x90, 0x90, 0x90, 0xE0, // D
		0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
		0xF0, 0x80, 0xF0, 0x80, 0x80  // F
	};

	memset(chip8, 0, sizeof(chip8_t));

	const uint8_t font_entry_point = 0x0;

	memcpy(&chip8->ram[font_entry_point], fonts, sizeof(fonts));

	const uint32_t rom_entry_point = 0x200;


    	if (fread(&chip8->ram[rom_entry_point], rom_size, 1, rom_file) != 1)
    	{
		fprintf(stderr,
			"Error: "
			"Could not read ROM file %s into memory\n",
			file_path);
		return false;
    	}

	fclose(rom_file);

	chip8->PC = rom_entry_point;
	chip8->state = RUNNING;
	chip8->SP = &chip8->stack[0];

	#ifdef DEBUG
		printf("font:\n");
		for (int i = 0; i < max_filesize; i++) {
			if (!(chip8->ram[i] == 0))
				printf("[%x] 0x%x\n", i, chip8->ram[i]);
		}
	#endif

	return true;
}

bool fetch_instruction(chip8_t* chip8, instruction_t* inst) {
	#ifdef DEBUG
		printf("PC: %x\n", chip8->PC);
	#endif 

	if (!(chip8->PC % 2 == 0)) {
		fprintf(stderr, "Error: PC is not at an even address\n");
		return false;
	}

	const uint8_t high_byte = chip8->ram[chip8->PC];	
	const uint8_t low_byte = chip8->ram[chip8->PC + 1];

	inst->opcode = (high_byte << 8) | low_byte;

	chip8->PC += 2;

	return true;
}

void decode_instruction(instruction_t* inst) {
	/*
	uint16_t NNN; // address, lowest 12-bit
	uint8_t NN; // lowest 8-bit
	uint8_t N; // nibble, lowest 4-bit
	uint8_t X; // lowest 4-bit of high byte
	uint8_t Y; // higher 4-bit of low byte
	*/

	inst->NNN = inst->opcode & 0xFFF;
	inst->NN = inst->opcode & 0xFF;
	inst->N = inst->opcode & 0xF;
	inst->X = (inst->opcode >> 8) & 0xF;
	inst->Y = (inst->opcode >> 4) & 0xF;
}
void exec_instruction(chip8_t* chip8, const instruction_t* inst) {

	const uint8_t top_nibble = inst->opcode >> 12;
	
	switch(top_nibble) {
		case 0x0:
			// 0NNN- SYS addr
			if (inst->NNN == 0x0E0) {
				memset(chip8->display, 0, sizeof chip8->display);
				chip8->draw = true;
				#ifdef DEBUG
					printf("clear screen\n");
				#endif
			}
			// 00E0 - CLS
			else if (inst->NNN == 0x0EE) {
				chip8->PC = *--chip8->SP;
			}
			break;
		case 0x1:
			// 1NNN - JP addr
			chip8->PC = inst->NNN;
			#ifdef DEBUG
				printf("jump to NNN\n");
			#endif 
			break;
		case 0x2:
			// 2NNN - CALL addr
			*chip8->SP++ = chip8->PC;
			chip8->PC = inst->NNN;
			#ifdef DEBUG
				printf("call subroutine at NNN\n");
			#endif
			break;
		case 0x3:
			// 3XNN - SE Vx, byte
			if (chip8->V[inst->X] == inst->NN)
				chip8->PC += 2;
			#ifdef DEBUG
				printf("skip next instruction if Vx = NN\n");
			#endif
			break;
		case 0x4:
			// 4XNN - SNE Vx, byte
			if (chip8->V[inst->X] != inst->NN)
				chip8->PC += 2;
			#ifdef DEBUG
				printf("skip next instruction if Vx != NN\n");
			#endif
			break;
		case 0x5:
			// 5XY0 - SE Vx, Vy
			// Skip next instruction if Vx = Vy
			if (inst->N != 0)
				break;
			if (chip8->V[inst->X] == chip8->V[inst->Y])
				chip8->PC += 2;
			#ifdef DEBUG
				printf("skip next instruction if Vx != Vy\n");
			#endif
			break;
		case 0x6:
			// 6XNN - LD Vx, byte
			// Set Vx = NN
			chip8->V[inst->X] = inst->NN;
			#ifdef DEBUG
				printf("load normal reg with imm\n");
			#endif
			break;
		case 0x7:
			// 7XNN - ADD Vx, byte
			// Set Vx = Vx + KK
			chip8->V[inst->X] += inst->NN;
			#ifdef DEBUG
				printf("add nn to vx\n");
			#endif
			break;
		case 0x8:
			switch(inst->N) {
				case 0x0:
					// 8XY0 - LD Vx, Vy
					chip8->V[inst->X] = chip8->V[inst->Y];
					#ifdef DEBUG
						printf("Vx = Vy\n");
					#endif
					break;
				case 0x1:
					// 8XY1 - OR Vx, Vy
					chip8->V[inst->X] |= chip8->V[inst->Y];

					chip8->V[0xF] = 0;
					#ifdef DEBUG
						printf("Vx |= Vy\n");
					#endif
					break;
				case 0x2:
					// 8XY2 - AND Vx, Vy
					chip8->V[inst->X] &= chip8->V[inst->Y];

					chip8->V[0xF] = 0;
					#ifdef DEBUG
						printf("Vx &= Vy\n");
					#endif
					break;
				case 0x3:
					// 8XY3 - XOR Vx, Vy
					chip8->V[inst->X] ^= chip8->V[inst->Y];

					chip8->V[0xF] = 0;
					#ifdef DEBUG
						printf("Vx ^= Vy\n");
					#endif
					break;
				case 0x4:
					// 8XY4 - ADD Vx, Vy
					uint16_t sum = (uint16_t) chip8->V[inst->X] + chip8->V[inst->Y];
					chip8->V[inst->X] = sum;
					if (sum > 255)
						chip8->V[0xF] = 1;
					else
						chip8->V[0xF] = 0;

					#ifdef DEBUG
						printf("Vx += Vy\n");
					#endif
					break;
				case 0x5:
					// 8XY5 - SUB Vx, Vy 
					int16_t difference = chip8->V[inst->X] - chip8->V[inst->Y];
					chip8->V[inst->X] = difference;
					if (difference >= 0) 
						chip8->V[0xF] = 1;
					else
						chip8->V[0xF] = 0;

					#ifdef DEBUG
						printf("Vx -= Vy\n");
					#endif
					break;
				case 0x6:
					// 8XY6 - SHR Vx, Vy 
					uint8_t lsb = chip8->V[inst->X] & 1;
					if (lsb == 1) 
						chip8->V[0xF] = 1;
					else
						chip8->V[0xF] = 0;
					chip8->V[inst->X] = chip8->V[inst->Y] >> 1;

					chip8->V[0xF] = lsb;
					#ifdef DEBUG
						printf("Vx = Vy >> 1\n");
					#endif
					break;
				case 0x7:
					// 8XY7 - SUBN Vx, Vy
					difference = chip8->V[inst->Y] - chip8->V[inst->X];	
					chip8->V[inst->X] = difference;
					if (difference >= 0)
						chip8->V[0xF] = 1;
					else 
						chip8->V[0xF] = 0;
					#ifdef DEBUG
						printf("Vx = Vy - Vx\n");
					#endif
					break;
				case 0xE:
					// 8XYE - SHL Vx, Vy
					uint8_t msb = (chip8->V[inst->Y] & 0x80) >> 7;
					chip8->V[inst->X] = chip8->V[inst->Y] << 1;
					chip8->V[0xF] = msb;
					#ifdef DEBUG
						printf("Vx = Vy << 1\n");
					#endif
					break;
				default:
					break;
			}
			break;

		case 0x9:
			// 9XY0 - SNE Vx, Vy
			if (chip8->V[inst->X] != chip8->V[inst->Y]) 
				chip8->PC += 2;
			#ifdef DEBUG
				printf("SNE Vx Vy\n");
			#endif
			break;
		case 0xA:
			// ANNN - LD I, addr
			chip8->I = inst->NNN;
			#ifdef DEBUG
				printf("load I with NNN\n");
			#endif
			break;
		case 0xB:
			// BNNN - JP V0, addr
			chip8->PC = inst->NNN + chip8->V[0x0];
			#ifdef DEBUG
				printf("branch to NNN + V0\n");
			#endif
			break;
		case 0xC: 
			// CXNN - RND Vx, byte
			uint8_t random = rand() % 256;
			chip8->V[inst->X] = random & inst->NN;
			#ifdef DEBUG
				printf("Vx = rand & NN\n");
			#endif
			break;
                case 0xD:
			// DXYN - DRW Vx, Vy, nibble
			chip8->V[0xF] = 0;

                        uint8_t x = chip8->V[inst->X] % 64;
                        uint8_t y = chip8->V[inst->Y] % 32;
			
			const uint8_t orig_x = x;

			const uint8_t sprite_height = inst->N;

                        for (uint8_t i = 0; i < sprite_height; i++) {
				if (y >= 32) break;

                                uint8_t sprite_data = chip8->ram[chip8->I + i];
				x = orig_x;

                                for (int8_t j = 7; j >= 0; j--) {
					if (x >= 64) break;
                                        bool *pixel= &chip8->display[y][x];
                                        bool sprite_bit = (sprite_data & (1 << j)) != 0;

                                        if (sprite_bit && *pixel)
                                                chip8->V[0xF] = 1;

                                        *pixel ^= sprite_bit;

					x++;
                                }
                                y++;
                        }
                        chip8->draw = true;
			#ifdef DEBUG
				printf("draw\n");
			#endif
			break;
		case 0xE:
			// EX9E - SKP Vx
			if (inst->NN == 0x9E) {
				if (chip8->keypad[chip8->V[inst->X]])
					chip8->PC += 2;
				#ifdef DEBUG
					printf("skip inst if key pressed\n");
				#endif
			}
			// EXA1 - SKNP Vx
			else if (inst->NN == 0xA1) {
				if (!chip8->keypad[chip8->V[inst->X]])
					chip8->PC += 2;
				#ifdef DEBUG
					printf("skip inst if key not pressed\n");
				#endif
			}
			break;
		case 0xF:
			switch (inst->NN) {
				// FX0A - LD Vx, K
				case 0x0A:
					static bool any_key_pressed = false;
					static uint8_t key = 0xFF;
					for (uint8_t i = 0; key == 0xFF && i < sizeof chip8->keypad; i++) {
						if (chip8->keypad[i]) {
							key = i;
							any_key_pressed = true;
						}
					}
					if (!any_key_pressed) {
						// continue running this instruction
						chip8->PC -= 2;
					} else {
						// await release of key
						if (chip8->keypad[key]) 
							chip8->PC -= 2;
						else {
							chip8->V[inst->X] = key;
							key = 0xFF;
							any_key_pressed = false;
						}
					}
					#ifdef DEBUG
						printf("Awaiting keypress\n");
					#endif
					break;
				// FX07 - LD Vx, DT
				case 0x07:
					chip8->V[inst->X] = chip8->delay_timer;
					#ifdef DEBUG
						printf("Vx = DT\n");
					#endif
					break;
				// FX15 - LD DT, Vx
				case 0x15:
					chip8->delay_timer = chip8->V[inst->X];
					#ifdef DEBUG
						printf("DT = Vx\n");
					#endif
					break;
				// FX18 - LD ST, Vx
				case 0x18:
					chip8->sound_timer = chip8->V[inst->X];
					#ifdef DEBUG
						printf("ST = Vx\n");
					#endif
					break;
				// FX1E - ADD I, Vx
				case 0x1E:
					chip8->I += chip8->V[inst->X];
					#ifdef DEBUG
						printf("I = I + Vx\n");
					#endif
					break;
				// FX29 - LD F, Vx
				case 0x29:
					chip8->I = chip8->V[inst->X] * 5;
					#ifdef DEBUG
						printf("I = location of sprite\n");
					#endif
					break;
				// FX33 - LD B, Vx
				case 0x33:
					uint8_t bcd = chip8->V[inst->X];
					for (uint8_t i = 0; i < 3; i++) {
						chip8->ram[chip8->I + (2-i)] = bcd % 10;
						bcd /= 10;
					}
					#ifdef DEBUG
						printf("store BCD of BX at I\n");
					#endif
					break;
				// FX55 - LD [I], Vx
				case 0x55:
					for (uint8_t i = 0; i <= inst->X; i++) {
						chip8->ram[chip8->I++] = chip8->V[i];
					}
					#ifdef DEBUG
						printf("Load from V0 to Vx\n");
					#endif
					break;
				// FX65 - LD VX, [I]
				case 0x65:
					for (uint8_t i = 0; i <= inst->X; i++) {
						chip8->V[i] = chip8->ram[chip8->I++];
					}
					#ifdef DEBUG
						printf("Read from V0 to Vx I\n");
					#endif
					break;
				default:
					break;
			}
			break;
		default:
			fprintf(stderr, "Error: unimplemented opcode not found\n");
			break;
	}
}

void run_instruction(chip8_t *chip8) {
	instruction_t inst;

	fetch_instruction(chip8, &inst);

	decode_instruction(&inst);

	exec_instruction(chip8, &inst);
}

void update_timers(chip8_t* chip8) {
	if (chip8->delay_timer > 0) 
		chip8->delay_timer--;
	if (chip8->sound_timer > 0) 
		chip8->sound_timer--;		
}
