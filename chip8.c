#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include <SDL2/SDL.h>

// SDL Container Object
typedef struct 
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_AudioSpec want, have;
    SDL_AudioDeviceID dev;
} sdl_t;

// Emulator states 
typedef enum
{
    QUIT,
    RUNNING,
    PAUSED,
} emulator_state_t;

// CHIP8 extensions/quirks support
typedef enum
{
    CHIP8,
    SUPERCHIP,
    XOCHIP,    
} extension_t;

// Emulator configuration object
typedef struct 
{
    uint32_t window_width;      // SDL window width
    uint32_t window_height;     // SDL window height
    uint32_t fg_color;          // foreground color RGBA8888
    uint32_t bg_color;          // background color RGBA8888
    uint32_t scale_factor;      // amount to scale CHIP8 pixel
    bool pixel_outlines;        // Draw pixel "outlines" yes/no
    uint32_t insts_per_second;  // CHIP8 CPU "clock rate" or Hz
    uint32_t square_wave_freq;  // Frequency of square wave sound
    uint32_t audio_sample_rate; //  
    int16_t volume;             // How loud or not is the sound 
    float color_lerp_rate;      // Amount to lerp colors by, between [0.1, 1.0]
    extension_t current_extension;  // Current quirks/extension support for e.g. CHIP8 vs SUPERCHIP
} config_t;

// CHIP8 Instruction Format
typedef struct
{
    uint16_t opcode;
    uint16_t NNN;   // 12 bit address/constant
    uint8_t NN;     // 8 bit constant
    uint8_t N;      // 4 bit constant
    uint8_t X;      // 4 bit register identifier
    uint8_t Y;      // 4 bit register identifier
} instruction_t;

// CHIP8 Machine object 
typedef struct {
    emulator_state_t state;
    uint8_t ram[4096];
    bool display[64*32];    // Emulate original CHIP8 resolution pixels
    uint32_t pixel_color[64*32];  // CHIP8 pixel colors to draw 
    uint16_t stack[12];     // Subroutine stack
    uint16_t *stack_ptr;    // Stack pointer
    uint8_t V[16];          // Data registers V0-VF
    uint16_t I;             // Index register
    uint16_t PC;            // Program counter 
    uint8_t delay_timer;    // Decrements at 60hz when > 0
    uint8_t sound_timer;    // Decrements at 60hz and plays tone when > 0 
    bool keypad[16];        // Hexadecimal keypad 0x0-0xF
    const char *rom_name;   // Currently running ROM
    instruction_t inst;     // Currently executing instruction
    bool draw;              // Update screen yes/no
} chip8_t;

// Color "lerp" (linear interpretation)
uint32_t color_lerp(const uint32_t start_color, const uint32_t end_color, const float t)
{
    const uint8_t s_r = (start_color >> 24) & 0XFF;
    const uint8_t s_g = (start_color >> 16) & 0XFF;
    const uint8_t s_b = (start_color >>  8) & 0XFF;
    const uint8_t s_a = (start_color >>  0) & 0XFF;

    const uint8_t e_r = (end_color >> 24) & 0XFF;
    const uint8_t e_g = (end_color >> 16) & 0XFF;
    const uint8_t e_b = (end_color >>  8) & 0XFF;
    const uint8_t e_a = (end_color >>  0) & 0XFF;

    const uint8_t ret_r = ((1 - t) * s_r) + (t * e_r);
    const uint8_t ret_g = ((1 - t) * s_g) + (t * e_g);
    const uint8_t ret_b = ((1 - t) * s_b) + (t * e_b);
    const uint8_t ret_a = ((1 - t) * s_a) + (t * e_a);

    return (ret_r << 24) | (ret_g << 16) | (ret_b << 8) | ret_a; 
}

// SDL Audio Callback
// Fill out stream/audio buffer w/ data
void audio_callback(void *userdata, uint8_t *stream, int len)
{
    config_t *config = (config_t *) userdata;

    int16_t *audio_data = (int16_t *) stream;
    static uint32_t running_sample_index = 0;
    const int32_t square_wave_period = config->audio_sample_rate / config->square_wave_freq;
    const int32_t half_square_wave_period = square_wave_period / 2;

    // Filling out 2 bytes at a time (int16_t), len is in bytes so divide by 2
    // If current chunk of audio for square wave is crest of the wave, 
    //      this will add the volume, otherwise it is trough of the wave,
    //      and will add "negative" volume
    for (int i = 0; i < len / 2; i++) 
        audio_data[i] = ((running_sample_index++ / half_square_wave_period) % 2) ? 
                        config->volume : 
                        -config->volume;
}

// Initialize SDL
bool init_sdl(sdl_t *sdl, config_t *config)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0)
    {
        SDL_Log("Could not initialize SDL subsystems! %s\n", SDL_GetError());
        return false;
    }

    sdl->window = SDL_CreateWindow("CHIP8", SDL_WINDOWPOS_CENTERED, 
                                   SDL_WINDOWPOS_CENTERED, 
                                   config->window_width * config->scale_factor, 
                                   config->window_height * config->scale_factor,
                                   0);
    if (!sdl->window) 
    {
        SDL_Log("Could not create SDL window %s\n", SDL_GetError());
        return false;
    }

    sdl->renderer = SDL_CreateRenderer(sdl->window, -1, SDL_RENDERER_ACCELERATED);
    if (!sdl->renderer) 
    {
        SDL_Log("Could not create SDL renderer %s\n", SDL_GetError());
        return false;
    }

    // Init Audio
    sdl->want = (SDL_AudioSpec)
    {
        .freq = 44100,          // 44100 hz "CD" quality
        .format = AUDIO_S16LSB, // Signed 16 bit little endian 
        .channels = 1,          // Mono channel
        .samples = 512,
        .callback = audio_callback,
        .userdata = config,    // Userdata passed to audio callback
    };

    sdl->dev = SDL_OpenAudioDevice(NULL, 0, &sdl->want, &sdl->have, 0); 

    if (sdl->dev == 0)
    {
        SDL_Log("Could not obtain Audio Device%s\n", SDL_GetError());
        return false;
    }

    if ((sdl->want.format != sdl->have.format) ||
        (sdl->want.channels != sdl->have.channels))
    {
        SDL_Log("Could not obtain desired Audio Spec\n");
        return false;       
    }
    
    return true; // Success
}

// Set up initial emulator configuration from passed in arguments
bool set_config_from_args(config_t *config, int argc, char** argv)
{
    // Set defaults
    *config = (config_t) 
    {
        .window_width = 64,     // chip 8 original x resolution
        .window_height = 32,    // chip 8 original y resolution
        .fg_color = 0xFFFFFFFF, // white
        .bg_color = 0x000000FF, // black
        .scale_factor = 20,     // default resolution will be 1280x620 or (64x32)*20
        .pixel_outlines = true, // Draw pixel outlines by default
        .insts_per_second = 700,    // Number of instructions to emulate in 1 second (clock rate of CPU)
        .square_wave_freq = 440,    // 440hz for middle A
        .audio_sample_rate = 44100, // CD quality, 44100hz
        .volume = 3000,             // INT16_MAX would be max volume
        .color_lerp_rate = 0.8,     // Color lerp rate, between [0.1, 1.0]
        .current_extension = CHIP8, // CHIP8 default extension
    };

    // Override defaults from passed in arguments
    for (int i = 1; i < argc; i++)
    {
        (void) argv[i]; // prevent compiler from unused argc/argv
        // e.g. set scale factor 
        if (strncmp(argv[i], "--scale-factor", strlen("--scale-factor")) == 0)
        {
            // Note: should add checks for numerical input
            i++;
            config->scale_factor = (uint32_t) strtol(argv[i], NULL, 10); 
        }

    }

    return true;    // Success
}

// Initialize CHIP8 machine 
bool init_chip8(chip8_t *chip8, const config_t config,const char rom_name[])
{
    const uint32_t entry_point = 0x200; // CHIP8 Roms will be loaded to 0x200
    const uint8_t font[] = 
    {
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

    // Initialize entire CHIP8 machine 
    memset(chip8, 0, sizeof(chip8_t));

    // Load font
    memcpy(&chip8->ram[0], font, sizeof(font));

    // Open ROM file  
    FILE *rom = fopen(rom_name, "rb");
    if (!rom)
    {
        SDL_Log("ROM file %s is invalid or does not exist\n", rom_name);
        return false;
    }

    // Get check rom size 
    fseek(rom, 0, SEEK_END);
    const size_t rom_size = ftell(rom);
    const size_t max_size = sizeof chip8->ram - entry_point;
    rewind(rom);

    if (rom_size > max_size)
    {
        SDL_Log("ROM file %s is too big! Rom size: %lu, Max size allowed: %lu",
                 rom_name, (unsigned long) rom_size, (unsigned long) max_size); 
                 // %lu compiles with cast but %zu does not
        return false;   
    }

    // Load ROM
    if (fread(&chip8->ram[entry_point], rom_size, 1, rom) != 1)
    {
        SDL_Log("Could not read ROM file %s into CHIP8 memory\n",
                 rom_name);  
        return false;           
    }

    fclose(rom);

    // Set chip8 machine defaults 
    chip8->state = RUNNING;     // Default machine state to on/running
    chip8->PC = entry_point;    // Start Program Counter at ROM entry point 
    chip8->rom_name = rom_name;
    chip8->stack_ptr = &chip8->stack[0];
    memset(&chip8->pixel_color[0], config.bg_color, sizeof chip8->pixel_color);

    return true;    // Success 
}

// Final cleanup
void final_cleanup(const sdl_t sdl)
{
    SDL_DestroyRenderer(sdl.renderer);
    SDL_DestroyWindow(sdl.window);
    SDL_CloseAudioDevice(sdl.dev);
    SDL_Quit(); // Shutdown SDL subsystems
}

// Clear screen / SDL Window to background color
void clear_screen(const sdl_t sdl, const config_t config)
{
    const uint8_t r = (config.bg_color >> 24) & 0xFF;   // mask red bits 31-24
    const uint8_t g = (config.bg_color >> 16) & 0xFF;   // mask green bits 23-16
    const uint8_t b = (config.bg_color >>  8) & 0xFF;   // mask blue bits 15-8
    const uint8_t a = (config.bg_color >>  0) & 0xFF;   // mask alpha bits 7-0
    SDL_SetRenderDrawColor(sdl.renderer, r, g, b, a);
    SDL_RenderClear(sdl.renderer);
}

// Update window with any changes
void update_screen(const sdl_t sdl, const config_t config, chip8_t *chip8)
{
    SDL_Rect rect = {.x = 0, .y = 0, .w = config.scale_factor, .h = config.scale_factor};

    // Grab background color values to draw outlines
    const uint8_t bg_r = (config.bg_color >> 24) & 0xFF;   // mask red bits 31-24
    const uint8_t bg_g = (config.bg_color >> 16) & 0xFF;   // mask green bits 23-16
    const uint8_t bg_b = (config.bg_color >>  8) & 0xFF;   // mask blue bits 15-8
    const uint8_t bg_a = (config.bg_color >>  0) & 0xFF;   // mask alpha bits 7-0

    // Loop through display pixels, draw a rectangle per pixel to the SDL window 
    for (uint32_t i = 0; i < sizeof chip8->display; i++)
    {
        // Translate 1D index i value to 2D X/Y coordinates
        // X = i % window width
        // Y = i / window width
        rect.x = (i % config.window_width) * config.scale_factor;
        rect.y = (i / config.window_width) * config.scale_factor;

        if (chip8->display[i])
        {
            // If pixel is on, draw foreground color
            if (chip8->pixel_color[i] != config.fg_color)
            {
                // Lerp towards fg color
                chip8->pixel_color[i] = color_lerp(chip8->pixel_color[i], 
                                                   config.fg_color, 
                                                   config.color_lerp_rate);
            }
            const uint8_t r = (chip8->pixel_color[i] >> 24) & 0xFF;   // mask red bits 31-24
            const uint8_t g = (chip8->pixel_color[i] >> 16) & 0xFF;   // mask green bits 23-16
            const uint8_t b = (chip8->pixel_color[i] >>  8) & 0xFF;   // mask blue bits 15-8
            const uint8_t a = (chip8->pixel_color[i] >>  0) & 0xFF;   // mask alpha bits 7-0

            SDL_SetRenderDrawColor(sdl.renderer, r, g, b, a);
            SDL_RenderFillRect(sdl.renderer, &rect);

            // TODO
            if (config.pixel_outlines)
            {
                // If user requested drawing pixel outlines, draw those here
                SDL_SetRenderDrawColor(sdl.renderer, bg_r, bg_g, bg_b, bg_a);
                SDL_RenderDrawRect(sdl.renderer, &rect);
            }
        }
        else 
        {
            // Pixel is off, draw background color
            if (chip8->pixel_color[i] != config.bg_color)
            {
                // Lerp towards bg color
                chip8->pixel_color[i] = color_lerp(chip8->pixel_color[i], 
                                                   config.bg_color, 
                                                   config.color_lerp_rate);
            }
            const uint8_t r = (chip8->pixel_color[i] >> 24) & 0xFF;   // mask red bits 31-24
            const uint8_t g = (chip8->pixel_color[i] >> 16) & 0xFF;   // mask green bits 23-16
            const uint8_t b = (chip8->pixel_color[i] >>  8) & 0xFF;   // mask blue bits 15-8
            const uint8_t a = (chip8->pixel_color[i] >>  0) & 0xFF;   // mask alpha bits 7-0

            SDL_SetRenderDrawColor(sdl.renderer, r, g, b, a);
            SDL_RenderFillRect(sdl.renderer, &rect);
        }
    }
    SDL_RenderPresent(sdl.renderer);
}

// Handle user input
// CHIP8 Keypad     QWERTY
// 123C             1234
// 456D             qwer
// 789E             asdf
// A0BF             zxcv
void handle_input(chip8_t *chip8, config_t *config)
{
    SDL_Event event;

    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
            case SDL_QUIT:
                // Exit window; End program
                chip8->state = QUIT;
                break;

            case SDL_KEYDOWN:
                switch (event.key.keysym.sym) 
                {
                    case SDLK_ESCAPE:
                        // Escape key; Exit window & end program
                        chip8->state = QUIT;
                        break;

                    case SDLK_SPACE:
                        // Space bar
                        if (chip8->state == RUNNING)
                        {
                            chip8->state = PAUSED;  // Pause
                            puts("==== PAUSED ====");
                        }
                        else 
                        {
                            chip8->state = RUNNING; // Resume 
                        }
                        break;

                    case SDLK_EQUALS:
                        // '=': Reset CHIP8 machine for current ROM
                        init_chip8(chip8, *config, chip8->rom_name);
                        break;

                    case SDLK_j:
                        // 'j': Decrease color lerp rate 
                        if (config->color_lerp_rate > 0.1)
                            config->color_lerp_rate -= 0.1;
                        break;

                    case SDLK_k:
                        // 'k': Increase color lerp rate 
                        if (config->color_lerp_rate < 1)
                            config->color_lerp_rate += 0.1;
                        break;

                    case SDLK_o:
                        // 'o': Decrease volume
                        if (config->volume > 0)
                            config->volume -= 500;
                        break;

                    case SDLK_p:
                        // 'p': Increase volume
                        if (config->volume < INT16_MAX)
                            config->volume += 500;
                        break;

                    // Map qwerty keys to CHIP8 keypad
                    case SDLK_1: chip8->keypad[0x1] = true; break;
                    case SDLK_2: chip8->keypad[0x2] = true; break;
                    case SDLK_3: chip8->keypad[0x3] = true; break;
                    case SDLK_4: chip8->keypad[0xC] = true; break;

                    case SDLK_q: chip8->keypad[0x4] = true; break;
                    case SDLK_w: chip8->keypad[0x5] = true; break;
                    case SDLK_e: chip8->keypad[0x6] = true; break;
                    case SDLK_r: chip8->keypad[0xD] = true; break;

                    case SDLK_a: chip8->keypad[0x7] = true; break;
                    case SDLK_s: chip8->keypad[0x8] = true; break;
                    case SDLK_d: chip8->keypad[0x9] = true; break;
                    case SDLK_f: chip8->keypad[0xE] = true; break;

                    case SDLK_z: chip8->keypad[0xA] = true; break;
                    case SDLK_x: chip8->keypad[0x0] = true; break;
                    case SDLK_c: chip8->keypad[0xB] = true; break;
                    case SDLK_v: chip8->keypad[0xF] = true; break;
                        
                    default: break;
                }
                break;

            case SDL_KEYUP:
                switch (event.key.keysym.sym)
                {
                    // Map qwerty keys to CHIP8 keypad
                    case SDLK_1: chip8->keypad[0x1] = false; break;
                    case SDLK_2: chip8->keypad[0x2] = false; break;
                    case SDLK_3: chip8->keypad[0x3] = false; break;
                    case SDLK_4: chip8->keypad[0xC] = false; break;

                    case SDLK_q: chip8->keypad[0x4] = false; break;
                    case SDLK_w: chip8->keypad[0x5] = false; break;
                    case SDLK_e: chip8->keypad[0x6] = false; break;
                    case SDLK_r: chip8->keypad[0xD] = false; break;

                    case SDLK_a: chip8->keypad[0x7] = false; break;
                    case SDLK_s: chip8->keypad[0x8] = false; break;
                    case SDLK_d: chip8->keypad[0x9] = false; break;
                    case SDLK_f: chip8->keypad[0xE] = false; break;

                    case SDLK_z: chip8->keypad[0xA] = false; break;
                    case SDLK_x: chip8->keypad[0x0] = false; break;
                    case SDLK_c: chip8->keypad[0xB] = false; break;
                    case SDLK_v: chip8->keypad[0xF] = false; break;

                    default: break;
                }
                break;

            default:
                break;
        }
    }
}

// Emulate 1 CHIP8 Instruction
void emulate_instruction(chip8_t *chip8, const config_t config)
{
    bool carry; // save carry flag/VF value for some instructions
    printf("Address: 0x%04X, Opcode: 0x%04X Desc: ", 
            chip8->PC-2, chip8->inst.opcode); // Debug

    // Get next opcode from ram
    chip8->inst.opcode = (chip8->ram[chip8->PC] << 8) | chip8->ram[chip8->PC+1];
    chip8->PC += 2; // Pre-increment program counter for next opcode

    // Fill out current instruction format DXYN
    chip8->inst.NNN = chip8->inst.opcode & 0x0FFF;  // mask lower 12 bits
    chip8->inst.NN = chip8->inst.opcode & 0x0FF;    // mask lower 8 bits
    chip8->inst.N = chip8->inst.opcode & 0x0F;      // mask lower 4 bits
    chip8->inst.X = (chip8->inst.opcode >> 8) & 0x0F; // mask 2nd 4 bits
    chip8->inst.Y = (chip8->inst.opcode >> 4) & 0x0F; // mask 3rd 4 bits

    // Emulate opcode

    switch((chip8->inst.opcode >> 12) & 0x0F)   // mask top 1st bits for category
    {
        case 0x00:
            if (chip8->inst.NN == 0xE0)
            {
                // 00E0: Clear the display.
                printf("Clear screen\n"); // Debug

                memset(&chip8->display[0], false, sizeof chip8->display);
                chip8->draw = true; // Will update screen on next 60hz tick
            }
            else if (chip8->inst.NN == 0xEE)
            {
                // 00EE: Return from a subroutine.
                // Set program counter to last address from subroutine stack ("pop" it off the stack)
                //      so next opcode will be obtained from that address
                printf("Return from subroutine to address 0x%04X\n", 
                        *(chip8->stack_ptr - 1));   // Debug
                chip8->PC = *--chip8->stack_ptr;
            }
            else 
            {
                // Unimplemented/invalid opcode, may be 0xNNN for calling machine code routine for RCA1802
                printf("Unimplemented Opcode.\n");   // Debug
            }
            break;

        case 0x01:
            // 1NNN: Jump to address NNN. 
            printf("Jump to address NNN (0x%04X)\n",
                    chip8->inst.NNN);   // Debug
            chip8->PC = chip8->inst.NNN;    // Set program counter so next opcode is from NNN
            break;

        case 0x02:
            // 2NNN: Call subroutine at nnn.
            // Store current address to return to on subroutine stack ("push" it on the stack)
            //      and set program counter to subroutine address so
            //      the next opcode is obtained there 
            printf("Call subroutine at NNN (0x%04X)\n",
                    chip8->inst.NNN);
            *chip8->stack_ptr++ = chip8->PC; 
            chip8->PC = chip8->inst.NNN;
            break;

        case 0x03:
            // 3XNN: Skip next instruction if Vx == NN
            printf("Check if V%X (0x%02X) == NN (0x%02X), skip next instruction if true\n",
                    chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.NN);
            if (chip8->V[chip8->inst.X] == chip8->inst.NN)
            {
               chip8->PC += 2;
            }
            break;

        case 0x04:
            // 4XNN: Skip next instruction if Vx != NN
            printf("Check if V%X (0x%02X) != NN (0x%02X), skip next instruction if true\n",
                    chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.NN);
            if (chip8->V[chip8->inst.X] != chip8->inst.NN)
            {
               chip8->PC += 2;
            }
            break;

        case 0x05:
            // 5XY0: Skips the next instruction if VX == VY
            printf("Check if V%X (0x%02X) == V%X (0x%02X), skip next instruction if true\n",
                    chip8->inst.X, chip8->V[chip8->inst.X], 
                    chip8->inst.Y, chip8->V[chip8->inst.Y]);
            if (chip8->inst.N != 0)
                break;
            if (chip8->V[chip8->inst.X] == chip8->V[chip8->inst.Y])
            {
                chip8->PC += 2;
            }
            break;

        case 0x06:
            // 6xNN: Set register VX to NN
            printf("Set V%X = 0x%02X\n", chip8->inst.X, chip8->inst.NN);
            chip8->V[chip8->inst.X] = chip8->inst.NN;   // Debug
            break;

        case 0x07:
            // 7xNN: Set register VX += NN
            printf("Set V%X  (0x%02X) += NN (0x%02X). Result: 0x%02X\n", 
                    chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.NN,
                    chip8->V[chip8->inst.X] + chip8->inst.NN);   // Debug
            chip8->V[chip8->inst.X] += chip8->inst.NN;
            break;

        case 0x08: 
            switch(chip8->inst.N)
            {
                case 0x0:
                    // 8XY0: Set register VX = VY
                    printf("Set V%X = V%X (0x%02X).\n", 
                        chip8->inst.X, chip8->inst.Y, 
                        chip8->V[chip8->inst.Y]);   // Debug
                    chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y];
                    break;

                case 0x1:
                    // 8XY1: Set register Vx |= Vy
                    printf("Set V%X (0x%02X) |= V%X (0x%02X). Result: 0x%02X\n", 
                        chip8->inst.X, chip8->V[chip8->inst.X],
                        chip8->inst.Y, chip8->V[chip8->inst.Y],
                        chip8->V[chip8->inst.X] | chip8->V[chip8->inst.Y]);   // Debug
                    chip8->V[chip8->inst.X] |= chip8->V[chip8->inst.Y];
                    if (config.current_extension == CHIP8)
                        chip8->V[0XF] = 0; // CHIP8 only quirk
                    break;

                case 0x2:
                    // 8XY2: Set register Vx &= Vy
                    printf("Set V%X (0x%02X) &= V%X (0x%02X). Result: 0x%02X\n", 
                        chip8->inst.X, chip8->V[chip8->inst.X],
                        chip8->inst.Y, chip8->V[chip8->inst.Y],
                        chip8->V[chip8->inst.X] & chip8->V[chip8->inst.Y]);   // Debug
                    chip8->V[chip8->inst.X] &= chip8->V[chip8->inst.Y];
                    if (config.current_extension == CHIP8)
                        chip8->V[0XF] = 0; // CHIP8 only quirk
                    break;

                case 0x3:
                    // 8XY3: Set register Vx ^= Vy
                    printf("Set V%X (0x%02X) ^= V%X (0x%02X). Result: 0x%02X\n", 
                        chip8->inst.X, chip8->V[chip8->inst.X],
                        chip8->inst.Y, chip8->V[chip8->inst.Y],
                        chip8->V[chip8->inst.X] ^ chip8->V[chip8->inst.Y]);   // Debug
                    chip8->V[chip8->inst.X] ^= chip8->V[chip8->inst.Y];
                    if (config.current_extension == CHIP8)
                        chip8->V[0XF] = 0; // CHIP8 only quirk
                    break;

                case 0x4:
                    // 8XY4: Set register Vx += Vy, VF = 1 when there's a carry, and to 0 when there is not. 
                    printf("Set V%X (0x%02X) += V%X (0x%02X), VF = 1 if carry; Result: 0x%02X, VF = %X\n", 
                        chip8->inst.X, chip8->V[chip8->inst.X],
                        chip8->inst.Y, chip8->V[chip8->inst.Y],
                        chip8->V[chip8->inst.X] + chip8->V[chip8->inst.Y],
                        ((uint16_t)(chip8->V[chip8->inst.X] + chip8->V[chip8->inst.Y]) > 0xFF));   // Debug

                    carry = ((uint16_t)(chip8->V[chip8->inst.X] + chip8->V[chip8->inst.Y]) > 255);

                    chip8->V[chip8->inst.X] += chip8->V[chip8->inst.Y];
                    chip8->V[0XF] = carry;
                    break;

                case 0x5:
                    // 8XY5: Set register Vx -= Vy
                    printf("Set V%X (0x%02X) -= V%X (0x%02X), VF = 1 if no borrow; Result: 0x%02X, VF = %X\n", 
                        chip8->inst.X, chip8->V[chip8->inst.X],
                        chip8->inst.Y, chip8->V[chip8->inst.Y],
                        chip8->V[chip8->inst.X] - chip8->V[chip8->inst.Y],
                        (chip8->V[chip8->inst.X] > chip8->V[chip8->inst.Y]));   // Debug

                    carry = (chip8->V[chip8->inst.X] >= chip8->V[chip8->inst.Y]);
                    chip8->V[chip8->inst.X] -= chip8->V[chip8->inst.Y];
                    chip8->V[0XF] = carry;
                    break;

                case 0x6:
                    // 8XY6: Stores the least significant bit of VX in VF and then shifts VX to the right by 1
                    printf("Set V%X (0x%02X) >>= 1, VF = shifted off bit; Result (%X); Result: 0x%02X\n",
                            chip8->inst.X, chip8->V[chip8->inst.X],
                            chip8->V[chip8->inst.X] & 1,
                            chip8->V[chip8->inst.X] >> 1); // Debug
                    if (config.current_extension == CHIP8)
                    {
                        carry = chip8->V[chip8->inst.Y] & 1;    // Use VY
                        chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y] >> 1; // Set VX = VY   
                    }
                    else
                    {
                        carry = chip8->V[chip8->inst.X] & 1;    // Use VX
                        chip8->V[chip8->inst.X] >>= 1;          // Use VX
                    }
                    chip8->V[0xF] = carry;

                    break;

                case 0x7:
                    // 8XY7: Sets VX = VY minus VX. VF is set to 0 when there's a borrow, and 1 when there is not. 
                    printf("Set V%X = V%X (0x%02X) - V%X (0x%02X), VF = 1 if no borrow; Result: 0x%02X, VF = %X\n", 
                        chip8->inst.X,  chip8->inst.Y, chip8->V[chip8->inst.Y], 
                        chip8->inst.X, chip8->V[chip8->inst.X],
                        chip8->V[chip8->inst.Y] - chip8->V[chip8->inst.X],
                        (chip8->V[chip8->inst.Y] > chip8->V[chip8->inst.X]));   // Debug
                    carry = (chip8->V[chip8->inst.Y] >= chip8->V[chip8->inst.X]);
                    chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y] - chip8->V[chip8->inst.X];
                    chip8->V[0XF] = carry;
                    break;

                case 0xE:
                    // 8XYE: Stores the most significant bit of VX in VF and then shifts VX to the left by 1.
                    printf("Set register V%X (0x%02X) <<= 1, VF = shited off bit(%X); Result: 0x%02X\n",
                            chip8->inst.X, chip8->V[chip8->inst.X],
                            (chip8->V[chip8->inst.X] & 0x80) >> 7,
                            chip8->V[chip8->inst.X] << 1);
                    if (config.current_extension == CHIP8)
                    {
                        carry = (chip8->V[chip8->inst.Y] & 0x80) >> 7;  // Use VY
                        chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y] << 1;
                    }
                    else
                    {
                        carry = (chip8->V[chip8->inst.X] & 0x80) >> 7;
                        chip8->V[chip8->inst.X] <<= 1;
                    }
                    chip8->V[0xF] = carry;
                    break; 

                default:
                    printf("Unimplemented/invalid opcode\n");
                    break;
            }
            break;

        case 0x09:
            // 9XY0: Skip next instruction when VX != VY
            printf("Check if V%X (0x%02X) != V%X (0x%02X), skip next instruction if true\n",
                    chip8->inst.X, chip8->V[chip8->inst.X], 
                    chip8->inst.Y, chip8->V[chip8->inst.Y]);
            if (chip8->V[chip8->inst.X] != chip8->V[chip8->inst.Y])
                chip8->PC += 2;
            break;

        case 0x0A:
            // ANNN: Set index register I to address NNN
            printf("Set I to address 0x%04X\n", chip8->inst.NNN);   // Debug
            chip8->I = chip8->inst.NNN;
            break;

        case 0x0B:
            // BNNN: Jump to V0 + NNN
            printf("Set PC to V0 (0x%02X) + NNN (0x%04X); Result PC = 0x%04X\n",
                    chip8->V[0], chip8->inst.NNN, chip8->V[0] + chip8->inst.NNN);   // Debug
            chip8->PC = chip8->V[0] + chip8->inst.NNN;
            break;

        case 0x0C:
            // CXNN: Set register VX = rand % 256 & NN (bitwise AND)
            printf("Set V%X = rand() %% 256 & NN (0x%02X)\n",
                    chip8->inst.X, chip8->inst.NN);     // Debug
            chip8->V[chip8->inst.X] = (rand() % 256) & chip8->inst.NN;
            break;

        case 0x0D:
            // DXYN: Draw N-height sprite at coords X, Y; Read from memory location I;
            //      Screen pixels are XOR'd with sprite bits
            //      VF (Carry flag) is set if any screen pixels are set off; This is useful 
            //      for collision detection or other reasons.
            printf("Draw N (%u) height sprite at coords V%X (0x%02X), V%X (0x%02X) "
                   "from memory location I (0x%04X). Set VF = 1 if any pixels are turned off.\n",
                   chip8->inst.N, chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.Y, 
                   chip8->V[chip8->inst.Y], chip8->I);   // Debug

            uint8_t X_coord = chip8->V[chip8->inst.X] % config.window_width;
            uint8_t Y_coord = chip8->V[chip8->inst.Y] % config.window_height;
            const uint8_t orig_X = X_coord;

            chip8->V[0xF] = 0;  // Initialize carry flag to 0

            for (uint8_t i = 0; i < chip8->inst.N; i++)
            {
                // Get next byte/row of sprite data 
                const uint8_t sprite_data = chip8->ram[chip8->I + i];
                X_coord = orig_X;   // Reset X for next row to draw

                for (int8_t j = 7; j >= 0; j--)
                {
                    // If sprite pixel/bit is on & display pixel is on, set carry flag 
                    bool *pixel = &chip8->display[Y_coord * config.window_width + X_coord];
                    const bool sprite_bit = (sprite_data & (1 << j));
                    if (sprite_bit && *pixel)
                    {
                        chip8->V[0xF] = 1;
                    }
                    // XOR display pixel with sprite pixel/bit to set in on/off
                    *pixel ^= sprite_bit;

                    // Stop drawing if hit right edge of screen
                    if (++X_coord >= config.window_width)
                        break;
                 }
                 // Stop drawing entire sprite if hit bottom edge of screen 
                 if (++Y_coord >= config.window_height)
                    break;
            }
            chip8->draw = true; // Will update screen on next 60hz tick
            break;

        case 0x0E:
            if (chip8->inst.NN == 0x9E)
            {
                // EX9E: Skip next instruction when key in VX is pressed
                printf("Skip next instruction if key in V%X (0x%02X) is pressed; Keypad value: %d\n",
                    chip8->inst.X, chip8->V[chip8->inst.X], chip8->keypad[chip8->V[chip8->inst.X]]); // Debug
                if (chip8->keypad[chip8->V[chip8->inst.X]])
                    chip8->PC += 2;
            }
            else if (chip8->inst.NN == 0xA1)
            {
                // EXA1: Skip next instruction when key in VX is not pressed
                printf("Skip next instruction if key in V%X (0x%02X) is not pressed; Keypad value: %d\n",
                    chip8->inst.X, chip8->V[chip8->inst.X], chip8->keypad[chip8->V[chip8->inst.X]]); // Debug
                if (!chip8->keypad[chip8->V[chip8->inst.X]])
                    chip8->PC += 2;
            }
            break;

        case 0x0F:
            switch (chip8->inst.NN)
            {
                case 0x0A:
                    // FX0A: VX = get key; Await until a keypress and store in VX
                    printf("Await until a key is pressed; Store key in V%X\n",
                            chip8->inst.X);
                    static bool any_key_pressed = false;
                    static uint8_t key = 0XFF;
                    for (uint8_t i = 0; key == 0xFF && i < sizeof chip8->keypad; i++)
                    {
                        if (chip8->keypad[i])
                        {
                            key = i;    // Save pressed key to check if it is released 
                            any_key_pressed = true;
                            break;
                        }
                    }
                    // If no key has been pressed, keep getting current opcode & running this instruction
                    if (!any_key_pressed)
                    {
                        chip8->PC -= 2; 
                    }
                    else
                    {
                        // A key has been pressed, also wait until it is released to set key in VX
                        if (chip8->keypad[key])         // Busy loop until key is released
                            chip8->PC -= 2;     
                        else 
                        {
                            chip8->V[chip8->inst.X] = key;  // i = key (offset into keypad array)
                            key = 0xFF;                     // Reset to not found 
                            any_key_pressed = false;        // Reset to no key pressed yet
                        }

                    }
                    break;

                case 0x1E:
                    // FX1E: I += VX; Add VX to I. For non-Amiga CHIP8, does not affect VF
                    printf("I (0x%04X) += V%X (0x%02X); Result (I): 0x%04X\n",
                            chip8->I, chip8->inst.X, chip8->V[chip8->inst.X],
                            chip8->I + chip8->V[chip8->inst.X]);
                    chip8->I += chip8->V[chip8->inst.X];    
                    break;

                case 0x07:
                    // FX07: VX = delay timer 
                    printf("Set V%X = delay timer value (0x%02X)\n",
                            chip8->inst.X, chip8->delay_timer);
                    chip8->V[chip8->inst.X] = chip8->delay_timer;
                    break;

                case 0x15:
                    // FX15: delay timer = VX 
                    printf("Set delay timer value = V%X (0x%02X)\n",
                            chip8->inst.X, chip8->V[chip8->inst.X]);
                    chip8->delay_timer = chip8->V[chip8->inst.X];
                    break;

                case 0x18:
                    // FX18: sound timer = VX 
                    printf("Set sound timer value = V%X (0x%02X)\n",
                            chip8->inst.X, chip8->V[chip8->inst.X]);
                    chip8->sound_timer = chip8->V[chip8->inst.X];
                    break;

                case 0x29:
                    // FX29: Set register I to sprite location in memory for character VX (0x0-0xF)
                    printf("Set I to sprite location in memory for character in V%X (0x%2X)." 
                            "Result (VX*5) = (0x%02X)\n",
                            chip8->I, chip8->V[chip8->inst.X], chip8->V[chip8->inst.X] * 5);
                            
                    chip8->I = chip8->V[chip8->inst.X] * 5;
                    break;

                case 0x33:
                    // FX33: Store BCD representation of VX at memory offset from I; 
                    //      I = hundred's place, I+1 = ten's place, I+2 = one's place
                    printf("Store BCD representation of V%X (0x%02X) at memory from I (0x%04X)\n",
                            chip8->inst.X, chip8->V[chip8->inst.X], chip8->I);
                    uint8_t bcd = chip8->V[chip8->inst.X];
                    for (int8_t i = 2; i >= 0; i--)
                    {
                        chip8->ram[chip8->I + i] = bcd % 10;
                        bcd /= 10;
                    }
                    break;

                case 0x55:
                    // FX55: Register dump V0-VX inclusive to memory offset from I
                    printf("Register dump V0-V%X (0x%02X) inclusive at memory from I (0x%04X)\n",
                            chip8->inst.X, chip8->V[chip8->inst.X], chip8->I);
                    for (uint8_t i = 0; i <= chip8->inst.X; i++)
                    {   
                        if (config.current_extension == CHIP8)
                            chip8->ram[chip8->I++] = chip8->V[i];
                        else   
                            chip8->ram[chip8->I + i] = chip8->V[i];
                    }
 
                    break;

                case 0x65:
                    // FX65: Register load V0-VX inclusive from memory offset from I
                    printf("Register load V0-V%X (0x%02X) inclusive at memory from I (0x%04X)\n",
                            chip8->inst.X, chip8->V[chip8->inst.X], chip8->I);
                    for (uint8_t i = 0; i <= chip8->inst.X; i++)
                    {
                        if (config.current_extension == CHIP8)
                            chip8->V[i] = chip8->ram[chip8->I++]; // Increment I each time 
                        else    
                            chip8->V[i] = chip8->ram[chip8->I + i];
                    }
                    break;

                default:
                    printf("Unimplemented/invalid opcode\n");
                    break;
            }
            break;

        default:
            printf("Unimplemented Opcode.\n");
            break;  // Unimplemented or invalid opcode
    }
}

// Update CHIP8 delay and sound timers every 60hz
void update_timers(const sdl_t sdl, chip8_t *chip8)
{
    if (chip8->delay_timer > 0)
        chip8->delay_timer--;

    if (chip8->sound_timer > 0)
    {
        chip8->sound_timer--;
        SDL_PauseAudioDevice(sdl.dev, 0);  // Play sound
    }
    else
    {
        SDL_PauseAudioDevice(sdl.dev, 1);  // Pause sound

    }
}

int main(int argc, char** argv)
{
    // Default usage message for args
    if (argc < 2) 
    {
        fprintf(stderr, "Usage: %s <rom_name>\n", argv[0]);
        exit(EXIT_FAILURE); 
    }

    // Initialize emulator config options
    config_t config = {0};
    if (!set_config_from_args(&config, argc, argv)) 
        exit(EXIT_FAILURE);

    // Initialize SDL
    sdl_t sdl = {0};
    if (!init_sdl(&sdl, &config)) 
        exit(EXIT_FAILURE);
    
    // Initialize CHIP8 machine 
    chip8_t chip8 = {0};
    const char *rom_name = argv[1];
    if (!init_chip8(&chip8, config, rom_name))
        exit(EXIT_FAILURE);

    // Initial screen clear to background color
    clear_screen(sdl, config);

    // Seed random number generator
    srand(time(NULL));

    // Main emulator loop
    while (chip8.state != QUIT)
    {
        // Handle user input 
        handle_input(&chip8, &config);

        if (chip8.state == PAUSED) 
            continue;

        // Get time before running instructions
        const uint64_t start_frame_time = SDL_GetPerformanceCounter();

        // Emulate CHIP8 Instructions for this emulator "frame" (60hz)
        for (uint32_t i = 0; i < config.insts_per_second / 60; i++)
            emulate_instruction(&chip8, config);

        // Get time elapsed after running instructions
        const uint64_t end_frame_time = SDL_GetPerformanceCounter();
        
        // Delay for approximately 60hz (16.67 ms) or actual time elapsed 
        const double time_elapsed = (double) ((end_frame_time - start_frame_time) * 1000 ) / SDL_GetPerformanceFrequency();

        SDL_Delay(16.67f > time_elapsed ? 16.67 - time_elapsed : 0);

        // Update window with changes every 60hz
        if (chip8.draw)
        {
            update_screen(sdl, config, &chip8);
            chip8.draw = false;
        }

        // Update delay & sound timers every 60 hz
        update_timers(sdl, &chip8);
    }

    // Final cleanup
    final_cleanup(sdl);

    exit(EXIT_SUCCESS);
}