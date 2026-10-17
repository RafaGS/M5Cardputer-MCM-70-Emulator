#include "../mcm_core/mcm_core.h"
#include <Arduino.h>
#include "printer_output.h"

// Display and keyboard hooks are implemented by the Cardputer HAL. Printer
// commands are captured in a bounded text spool and exposed over HTTP.

void runPrinter(int code) {
    mcm_printer_output_command(code, pr_data);
    // The original GUI animated carriage/paper motion over multiple frames.
    // This backend applies the movement immediately and never stalls the CPU.
    pr_op_code = 0;
}

void mcm_printer_reset(void) {
    mcm_printer_output_reset_head();
    pr_status = 241;
    pr_op_code = 0;
}

void wait140(void) {
    delayMicroseconds(140);
}
