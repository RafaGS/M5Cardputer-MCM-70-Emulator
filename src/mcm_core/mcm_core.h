#pragma once

#include <stdint.h>
#include <stddef.h>

// Core model types retained from the original MCM/70 Emulator.
typedef uint8_t BYTE;

#ifndef delta
#define delta 11
#endif

struct r {
    BYTE regA, regB, regC, regD, regE, regH, regL;
};
union regs {
    BYTE regs[7];
    struct r r;
};

struct address {
    BYTE regH;
    BYTE regL;
};
union stack {
    struct address stack[8];
    unsigned short stk[8];
};

typedef struct {
    BYTE id;
    char name[20];
    BYTE status;
    int lid;
    int w_head;
    BYTE speed;
    long int length;
} TP;

extern BYTE memory[0x4000];
extern BYTE ROM[0x8000];
extern unsigned int ROM_end;
extern union regs regs;
extern union stack stack;
extern BYTE stackptr;
extern BYTE flagParity, flagSign, flagCarry, flagZero;
extern int bank;
extern int instr_count;
extern BYTE inst;
extern int key_requested;
extern int mcm_on;
extern BYTE power;
extern int refresh_display;
extern int SelectedDevice;
extern BYTE move_clock;
extern TP tape0_s, tape1_s;
extern BYTE Printer, pr_status, pr_data, pr_op_code, printer_addressed, ABC;
extern int car_X, car_Y, page_top;
extern uint32_t mcm_total_steps;

void resetCPU(void);
void resetMCM(void);
int runCPU(void);
void NextInstr(void);
int imediateplus(BYTE instruction);
int iojmpcall(BYTE instruction);
int mathboolean(BYTE instruction);
int load(BYTE instruction);
int chkconditional(BYTE instruction);
void MCM_IO(BYTE instruction);
void setflags(int result, int carryzero);
void setflagsinc(int result);
void setparity(int result);

void refresh_SS(void);
void clear_display(void);
BYTE get_row(BYTE col);
void moveTape(int id);
void runPrinter(int code);
void mcm_printer_reset(void);
void wait140(void);
void mcm_core_printf(const char *format, ...);

bool mcm_load_roms(void);
void mcm_core_init(void);
void mcm_cpu_run_slice(uint16_t instructions);
uint16_t mcm_cpu_pc(void);
uint8_t mcm_cpu_opcode(void);
uint32_t mcm_cpu_steps(void);
