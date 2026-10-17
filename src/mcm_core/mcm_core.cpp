#include "mcm_core.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <Arduino.h>
#include "../hal/printer_output.h"

#ifndef MCM_DEBUG_SERIAL
#define MCM_DEBUG_SERIAL 1
#endif

BYTE memory[0x4000] = {0};
BYTE ROM[0x8000] = {0};
unsigned int ROM_end = 0x1FFF;
union regs regs = {};
union stack stack = {};
BYTE stackptr = 0;
BYTE flagParity = 0, flagSign = 0, flagCarry = 0, flagZero = 0;
int bank = 0;
int instr_count = 0;
BYTE inst = 0;
int key_requested = 0;
int mcm_on = 0;
BYTE power = 1;
int refresh_display = 0;
int SelectedDevice = 0;
BYTE move_clock = 0;
TP tape0_s = {};
TP tape1_s = {};
BYTE Printer = 0;
BYTE pr_status = 241;
BYTE pr_data = 0;
BYTE pr_op_code = 0;
BYTE printer_addressed = 0;
BYTE ABC = 0;
int car_X = 76;
int car_Y = 612;
int page_top = 0;
uint32_t mcm_total_steps = 0;

void mcm_core_printf(const char *format, ...) {
#if MCM_DEBUG_SERIAL
    char buffer[192];
    va_list args;
    va_start(args, format);
    const int count = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (count > 0) Serial.print(buffer);
#else
    (void)format;
#endif
}

void mcm_core_init(void) {
    mcm_on = 0;
    power = 1;
    key_requested = 0;
    refresh_display = 0;
    instr_count = 0;
    SelectedDevice = 0;
    move_clock = 0;
    bank = 0;
    inst = 0;
    pr_op_code = 0;
    pr_data = 0;
    Printer = 1;          // M7: emulated printer output is available over HTTP
    pr_status = 241;
    ABC = 0;
    tape0_s = {};
    tape1_s = {};
    tape0_s.id = 0xC8;
    tape1_s.id = 0xC9;
    tape0_s.status = 0x05; // unloaded, matching desktop InitTapes()
    tape1_s.status = 0x05;
    tape0_s.w_head = delta;
    tape1_s.w_head = delta;
    tape0_s.speed = 0;
    tape1_s.speed = 0;
    resetMCM();
    mcm_printer_output_init();
    mcm_total_steps = 0;
}

void mcm_cpu_run_slice(uint16_t instructions) {
    if (!mcm_on || !power) return;
    for (uint16_t i = 0; i < instructions; ++i) {
        (void)runCPU();
        ++mcm_total_steps;
    }
}

uint16_t mcm_cpu_pc(void) {
    return stack.stk[stackptr & 0x07];
}

uint8_t mcm_cpu_opcode(void) {
    return inst;
}

uint32_t mcm_cpu_steps(void) {
    return mcm_total_steps;
}
