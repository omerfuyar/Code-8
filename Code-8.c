/*
    Code-8 is by far the worst Chip-8 emulator
    you can find in entire git repositories.

    It uses the terminal for display. So you
    will need a 64x32 terminal to use it.

    I will try to keep this as bare metal as
    possible because I am also planning to
    port this to a esp32. So that means no
    syscalls in logic and minimum header usage.
*/

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define CHIP8_MEMORY_SIZE 0x1000
#define CHIP8_REGISTER_COUNT 0x10
#define CHIP8_STACK_COUNT 0x10
#define CHIP8_KEY_COUNT 0x10
#define CHIP8_ROM_START 0x200
#define CHIP8_CLOCK_FREQUENCY 60
#define CHIP8_SCREEN_WIDTH 0x40
#define CHIP8_SCREEN_HEIGHT 0x20

short STACK[CHIP8_STACK_COUNT] = {0};
char REGISTERS[CHIP8_REGISTER_COUNT] = {0};
char MEMORY[CHIP8_MEMORY_SIZE] = {0};
bool KEYS[CHIP8_KEY_COUNT] = {0};
bool SCREEN[CHIP8_SCREEN_HEIGHT][CHIP8_SCREEN_WIDTH] = {0};

short PC = CHIP8_ROM_START; // Program counter (index)
char SC = 0;                // Stack counter (index)
short I = 0;                // 12-bit index for 4096 bytes of memory
char DELAY_TIMER = 0;
char SOUND_TIMER = 0;
clock_t LAST_UPDATE = 0;

void screenDraw(char x, char y, char height)
{
    REGISTERS[0xf] = 0;

    for (char i = 0; i < height; i++)
    {
        for (char j = 0; j < 8; j++)
        {
            bool temp = SCREEN[y + i][x + j];

            SCREEN[y + i][x + j] ^= (MEMORY[I + i] >> j);

            if (temp && !SCREEN[y + i][x + j])
            {
                REGISTERS[0xf] = 1;
            }

            if (temp != SCREEN[y + i][x + j])
            {
                printf("\x1b[%d;%dH", y + i + 1, x + j + 1);

                if (SCREEN[y + i][x + j])
                {
                    // printf("█");
                    printf("#");
                }
                else
                {
                    printf(" ");
                }
            }
        }
    }

    fflush(stdout);
}

void screenClear()
{
    for (char i = 0; i < CHIP8_SCREEN_HEIGHT; i++)
    {
        for (char j = 0; j < CHIP8_SCREEN_WIDTH; j++)
        {
            SCREEN[i][j] = false;
        }
    }
}

void screenInit()
{
    screenClear();
    printf("\x1b[?25l");
}

void execute(char ins1, char ins2)
{
    // 0x0F : 0000 1111
    const char nib1 = (ins1 >> 4) & 0x0f;
    const char nib2 = ins1 & 0x0f;
    const char nib3 = (ins2 >> 4) & 0x0f;
    const char nib4 = ins2 & 0x0f;

    PC += 2;

    switch (nib1)
    {
    case 0x0:
        switch (nib2)
        {
        case 0x0:
            switch (nib4)
            {
            case 0x0: // 00E0 Clears the screen.
                screenClear();
                break;
            case 0xE:           // 00EE Returns from a subroutine.
                PC = STACK[SC]; //!
                SC--;
                break;
            }
            break;
        default: //// 0NNN Calls machine code routine (RCA 1802 for COSMAC VIP) at address NNN. Not necessary for most ROMs.
            break;
        }
        break;
    case 0x1: // 1NNN Jumps to address NNN.
        PC = nib2 * 0x10 * 0x10 + nib3 * 0x10 + nib4;
        break;
    case 0x2: // 2NNN Calls subroutine at NNN.
        SC++;
        STACK[SC] = PC; //!
        PC = nib2 * 0x10 * 0x10 + nib3 * 0x10 + nib4;
        break;
    case 0x3: // 3XNN Skips the next instruction if VX equals NN (usually the next instruction is a jump to skip a code block).
        if (REGISTERS[nib2] == nib3 * 0x10 + nib4)
        {
            PC += 2;
        }
        break;
    case 0x4: // 4XNN Skips the next instruction if VX does not equal NN (usually the next instruction is a jump to skip a code block).
        if (REGISTERS[nib2] != nib3 * 0x10 + nib4)
        {
            PC += 2;
        }
        break;
    case 0x5: // 5XY0 Skips the next instruction if VX equals VY (usually the next instruction is a jump to skip a code block).
        if (REGISTERS[nib2] == REGISTERS[nib3])
        {
            PC += 2;
        }
        break;
    case 0x6: // 6XNN Sets VX to NN.
        REGISTERS[nib2] = nib3 * 0x10 + nib4;
        break;
    case 0x7: // 7XNN Adds NN to VX (carry flag is not changed).
        REGISTERS[nib2] += nib3 * 0x10 + nib4;
        break;
    case 0x8:
        switch (nib4)
        {
        case 0x0: // 8XY0 Sets VX to the value of VY.
            REGISTERS[nib2] = REGISTERS[nib3];
            break;
        case 0x1: // 8XY1 Sets VX to VX or VY. (bitwise OR operation).
            REGISTERS[nib2] |= REGISTERS[nib3];
            break;
        case 0x2: // 8XY2 Sets VX to VX and VY. (bitwise AND operation).
            REGISTERS[nib2] &= REGISTERS[nib3];
            break;
        case 0x3: // 8XY3 Sets VX to VX xor VY.
            REGISTERS[nib2] ^= REGISTERS[nib3];
            break;
        case 0x4: // 8XY4 Adds VY to VX. VF is set to 1 when there's an overflow, and to 0 when there is not.
            REGISTERS[0xf] = REGISTERS[nib2] + REGISTERS[nib3] > 0xf ? 1 : 0;
            REGISTERS[nib2] += REGISTERS[nib3];
            break;
        case 0x5: // 8XY5 VY is subtracted from VX. VF is set to 0 when there's an underflow, and 1 when there is not. (i.e. VF set to 1 if VX >= VY and 0 if not).
            REGISTERS[0xf] = REGISTERS[nib2] < REGISTERS[nib3] ? 0 : 1;
            REGISTERS[nib2] -= REGISTERS[nib3];
            break;
        case 0x6: // 8XY6 Shifts VX to the right by 1, then stores the least significant bit of VX prior to the shift into VF.
            REGISTERS[0xf] = REGISTERS[nib2] & 1;
            REGISTERS[nib2] >>= 1;
            break;
        case 0x7: // 8XY7 Sets VX to VY minus VX. VF is set to 0 when there's an underflow, and 1 when there is not. (i.e. VF set to 1 if VY >= VX).
            REGISTERS[0xf] = REGISTERS[nib2] > REGISTERS[nib3] ? 0 : 1;
            REGISTERS[nib2] = REGISTERS[nib3] - REGISTERS[nib2];
            break;
        case 0xE: // 8XYE Shifts VX to the left by 1, then sets VF to 1 if the most significant bit of VX prior to that shift was set, or to 0 if it was unset.
            REGISTERS[0xf] = REGISTERS[nib2] & 0x80;
            REGISTERS[nib2] <<= 1;
            break;
        }
        break;
    case 0x9: // 9XY0 Skips the next instruction if VX does not equal VY. (Usually the next instruction is a jump to skip a code block).
        if (REGISTERS[nib2] != REGISTERS[nib3])
        {
            PC += 2;
        }
        break;
    case 0xA: // ANNN Sets I to the address NNN.
        I = nib2 * 0x10 * 0x10 + nib3 * 0x10 + nib4;
        break;
    case 0xB: // BNNN Jumps to the address NNN plus V0.
        PC = nib2 * 0x10 * 0x10 + nib3 * 0x10 + nib4 + REGISTERS[0];
        break;
    case 0xC: // CXNN Sets VX to the result of a bitwise and operation on a random number (Typically: 0 to 255) and NN.
        REGISTERS[nib2] = (rand() % 0x100) & (nib2 * 0x10 * 0x10 + nib3 * 0x10 + nib4);
        break;
    case 0xD: // DXYN Draws a sprite at coordinate (VX, VY) that has a width of 8 pixels and a height of N pixels. Each row of 8 pixels is read as bit-coded starting from memory location I; I value does not change after the execution of this instruction. As described above, VF is set to 1 if any screen pixels are flipped from set to unset when the sprite is drawn, and to 0 if that does not happen.
        screenDraw(REGISTERS[nib2], REGISTERS[nib3], nib4);
        break;
    case 0xE:
        switch (nib3)
        {
        case 0x9: // EX9E Skips the next instruction if the key stored in VX(only consider the lowest nibble) is pressed (usually the next instruction is a jump to skip a code block).
            if (KEYS[nib2])
            {
                break;
                PC += 2;
            }
        case 0xA: // EXA1 Skiphe next instruction if the key stored in VX(only consider the lowest nibble) is not pressed (usually the next instruction is a jump to skip a code block).
            if (!KEYS[nib2])
            {
                PC += 2;
            }
            break;
        }
        break;
    case 0xF:
        switch (nib4)
        {
        case 0x7: // FX07 Sets VX to the value of the delay timer.
            REGISTERS[nib2] = DELAY_TIMER;
            break;
        case 0xA: // FX0A A key press is awaited, and then stored in VX (blocking operation, all instruction halted until next key event, delay and sound timers should continue processing).
                  // todo
            break;
        case 0x8: // FX18 Sets the sound timer to VX.
            SOUND_TIMER = REGISTERS[nib2];
            break;
        case 0xE: // FX1E Adds VX to I. VF is not affected.
            I += REGISTERS[nib2];
            break;
        case 0x9: // FX29 Sets I to the location of the sprite for the character in VX(only consider the lowest nibble). Characters 0-F (in hexadecimal) are represented by a 4x5 font.
                  // todo
            break;
        case 0x3: // FX33 Stores the binary-coded decimal representation of VX, with the hundreds digit in memory at location in I, the tens digit at location I+1, and the ones digit at location I+2.
            MEMORY[I] = REGISTERS[nib2] / 100;
            MEMORY[I + 1] = (REGISTERS[nib2] % 100) / 10;
            MEMORY[I + 2] = REGISTERS[nib2] % 10;
            break;
        case 0x5:
            switch (nib3)
            {
            case 0x1: // FX15 Sets the delay timer to VX.
                DELAY_TIMER = REGISTERS[nib2];
                break;
            case 0x5: // FX55 Stores from V0 to VX (including VX) in memory, starting at address I. The offset from I is increased by 1 for each value written, but I itself is left unmodified.
                for (char i = 0; i <= nib2; i++)
                {
                    MEMORY[I + i] = REGISTERS[i];
                }
                break;
            case 0x6: // FX65 Fills from V0 to VX (including VX) with values from memory, starting at address I. The offset from I is increased by 1 for each value read, but I itself is left unmodified.
                for (char i = 0; i <= nib2; i++)
                {
                    REGISTERS[i] = MEMORY[I + i];
                }
                break;
            }
            break;
        }
        break;
    }
}

void timers()
{
    clock_t clk = clock();
    if ((clk - LAST_UPDATE) >= (CLOCKS_PER_SEC / CHIP8_CLOCK_FREQUENCY))
    {
        DELAY_TIMER--;
        SOUND_TIMER--;
        LAST_UPDATE = clk;
    }
}

void inputs()
{
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        printf("Incorrect use of app. please input one .ch8 file as argument.");
        return 2;
    }
    else
    {
        FILE *ch8 = fopen(argv[1], "rb");

        if (ch8 == NULL)
        {
            printf("Could not open input file.");
            return 3;
        }

        fread(MEMORY + CHIP8_ROM_START, sizeof(MEMORY) - CHIP8_ROM_START, 1, ch8);

        fclose(ch8);
    }

    srand(time(NULL));
    screenInit();

    while (1)
    {
        execute(MEMORY[PC], MEMORY[PC + 1]);
        timers();
        inputs();
    }

    return 1;
}