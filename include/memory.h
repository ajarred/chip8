#ifndef _MEMORY_H
#define _MEMORY_H

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <SDL3/SDL.h>

typedef enum {
        RUNNING,
        QUIT,
        PAUSED
} state_t ;

typedef struct {
        uint8_t ram[4096];
        bool display[32][64]; // (y,x)
        uint16_t stack[16];
        // registers
        uint8_t V[16];
        uint16_t I;
        uint8_t delay_timer;
        uint8_t sound_timer;
        uint16_t PC;
        uint16_t *SP;
        bool draw;
        state_t state;
        bool keypad[16];
} chip8_t;

typedef struct {
        uint16_t opcode;
        uint16_t NNN; // address, lowest 12-bit
        uint8_t NN; // lowest 8-bit
        uint8_t N; // nibble, lowest 4-bit
        uint8_t X; // lowest 4-bit of high byte
        uint8_t Y; // higher 4-bit of low byte
} instruction_t;

bool chip8_initialize(const char* file_path, chip8_t* chip8);

bool fetch_instruction(chip8_t* chip8, instruction_t* inst);

void decode_instruction(instruction_t* inst);

void exec_instruction(chip8_t* chip8, const instruction_t* inst);

void run_instruction(chip8_t *chip8);

void update_timers(chip8_t* chip8);

#endif
