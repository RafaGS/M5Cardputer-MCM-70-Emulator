#include "mcm_core.h"
#include "../hal/tape_storage.h"
#include <string.h>
#define printf mcm_core_printf

/**********************************************************************************
 *                       Emulation of MCM 8008 CPU                                *
 * modified fragment of 8008 emulator from Mike Willegal's 8008-disassembler.c at *
 * http://www.willegal.net/scelbi/8008-disassembler.c                             *
 * *******************************************************************************/

/*-------------------------------------------------------------
    Reset CPU; called at the beginning of emulation and when
    MCM/70's "reset button" is pressed
-------------------------------------------------------------*/

void resetCPU ()           // reset CPU
{
    int i;
    
    // clear registers and flags
    regs.r.regA=0;
    regs.r.regB=0;
    regs.r.regC=0;
    regs.r.regD=0;
    regs.r.regE=0;
    regs.r.regH=0;
    regs.r.regL=0;
    
    flagZero=0;
    flagCarry=0;
    flagSign=0;
    flagParity=0;

    // clear stack
    for (i=0; i<8; i++)  stack.stk[i] = 0;
   
    stackptr=0; 
    
    for (i=0x2000; i<0x4000; i++) memory[i]=0;   // clear RAM
}

//------------------------------------------------------------------
// reset the emulator
//------------------------------------------------------------------
void resetMCM ()   
 {
  clear_display();    // clear display  
  refresh_display=0;  // refresh display flag set to "no refresh"
  instr_count=0;      // instruction counter set to 0; this counter is used with refresh display
  resetCPU();         // reset CPU
  mcm_on=0;           // MCM is in "off" state
  SelectedDevice=0;   // no device selected
 }

//----------------------------------------------------------------
// Execute a single emulator CPU cycle 
//----------------------------------------------------------------
int  runCPU ( )
   {
    unsigned int temp;
    int halted;
    
    // printer's head and paper movements are animated; on each Glut main loop execution, when runCPU is selected,
    // the check is done whether or not the paper/head animation is finished. This is done by checking the
    // value of printer's operation code pr_op_code -- if it is non-zero, the animation is not finished.
    // Animation frames are delayed by 0.02 second.

    if (pr_op_code != 0) { runPrinter(pr_op_code); return 1; }
    
    inst = memory[stack.stk[stackptr]];  // instruction to be executed in this cycle   
    
/* Emulatio of Direct Access Memory (DAM) for SelfScan
  
   SelfScan displays the content of display memory (mempry[2021]-memory[20FF]) every time CPU is not involved in memory
   access operations (reading/writing). In the emulator, Self Scan is refreshed (almost) every time display memory is modifed
   (var refresh_display). Because the dislay of a computer on which the emulator is executed is refresehed, say max 70 times
   per second, many modifications to emulator's display memory have to be lost. This is why the following code refreshes
   SelfScan only every 56th call to refresh display. This speeds up the emulation reducing the number of Glut redisplay calls. */       

      if (refresh_display == 56)
        {
         refresh_SS();              // refresh display
	     refresh_display =0;        // reset display counter and instruction count
	     instr_count=0;
        }
    
     if (refresh_display !=0 ) instr_count++;   // increase instruction count but only when there is an
                                                // active call to refresh SS
                                                                                              
/* The last call to refresh Self-Scan could be less than 56th. To detect it, wait 1000 instructions and if none
   of them is a call to refresh display, call refresh_display one more time.  */
   if (instr_count == 1000) 
       {refresh_SS();
        refresh_display=0;
        instr_count=0;  
       }
     
    stack.stk[stackptr]++;
    stack.stk[stackptr] &= 0x3fff;
 
    // Tape movement; uses move_clock variable
    // if move_clock=82, then a tape is moved 1 cell in a specified direction
    move_clock++;
    
    /* if clock is up and not a write to tape statement, adjust tape position and status; 
       Note: writing to tape is handled separately
       Note: the value 82 is selected experimentally to have appropriate gap sizes on tapes (10- and 70-byte long, as required
             by tape organization); 
             this value controls the number of instructions to be executed (e.g. 82) for a tape to advance 1 cell (byte) */
    if ((move_clock >= 82) && ((inst != 0x57) | ((tape0_s.speed & 0x10) == 0) | ((tape1_s.speed & 0x10) == 0)))   
      {
       if (SelectedDevice == 0xC8) moveTape(0);        // move tape 0
       if (SelectedDevice == 0xC9) moveTape(1);        // move tape 1
      }   

 /*   call into 8008's four major instruction subgroups for decoding; these subgroups are determined by MS 2 bits and are:
      00  load (mediate), add/subtract (mediate), increment, decrement, 
          and/or/eor/cmp (mediate), rotate, return, halt
      01  jump, call, input, output
      10  add/subtract (not mediate), and/or/eor/cmp (not mediate)
      11  load (not mediate), halt
*/
 
    temp = inst >> 6;     // tmp=instruction subgroup

    switch (temp) {
    case 0: /* group 00:  INC/DEC/ROTATE, RST/RET and IMMEDIATE OPERATIONS
              LS 3 bits:
                 000 increment (bits 3-5 determine reg except 0 = hlt inst)
                 001 decrement (bits 3-5 determine reg except 0 = hlt inst)
                 010 rotate  (bits 3-5 specify
                 000 A left
                 001 A right
                 010 A left through carry
                 011 A right through carry
                 1xx ????
                 
                 011 conditional return
                 100 add/subtract/and/or/eor/cmp mediate
                 bits 3-5
                 000 add w/o carry
                 001 add w carry
                 010 subtract w/o borrow
                 011 subtract w borrow
                 100 and
                 101 xor
                 110 or
                 111 cmp

                 101 RST -call routine in low mem (address = inst & 0x38)
                 110 load mediate(bits 3-5 determine dest)
                 111 unconditional return
           */                 
           halted = imediateplus(inst);
           break;
    case 1: /* group 01: I/O, JUMP AND CALL
               LS 3 bits:

               000 jump conditionally
               XX1 input/output
               010 call conditionally
               100 jump
               110 call
            */   
        halted = iojmpcall(inst);
        break;
    case 2: 
           /* group 10:   MATH AND BOOLEAN OPERATIONS
               bits 3-5

               000 add (bit 0-2 determines source)
               001 add w/carry (bit 0-2 determines source)
               010 subtract (bit 0-2 determines source)
               011 subtract w/borrow (bit 0-2 determines source)
               100 and (bit 0-2 determines source)
               101 eor (bit 0-2 determines source)
               110 or (bit 0-2 determines source)
               111 cmp (bit 0-2 determines source)
           */    
        halted = mathboolean (inst);
        break;
    case 3: 
           /* group 11: Case MS 2 bits =  11  LOAD INSTRUCTION
              XXX load (bits 0-5 determine src/dest,except 0xFF = halt)
           */
           halted = load (inst);
          break;  
    }
    
   return halted;  
}

int imediateplus(unsigned char inst)
{
    unsigned int op;
    unsigned int reg;
    int carryzero;
    unsigned int sdata1, sdata2, result, adr;
    unsigned int cc; /* condition code to check, if conditional operation */

        /* first check for halt */
        if ((inst & 0xfe) == 0x0) {
                printf ("halt:%2x\n",inst);
                return 1;
        }
        
    op = inst & 0x7;
    switch (op) {
    case 0:  /* increment */
        reg = (inst & 0x38) >>3;
        result = (int)regs.regs[reg] + 1;
        setflagsinc (result);
        regs.regs[reg] = (unsigned char)result;
        break;
    case 1: /* decrement */
        reg = (inst & 0x38) >>3;
        result = (int)regs.regs[reg] - 1;
        setflagsinc (result);
        regs.regs[reg] = (unsigned char)result;
        break;
    case 2: /* rotate */
        op = (inst & 0x38) >> 3;
        switch (op) {
            case 0: /* rotate left */
                result = (int)regs.r.regA << 1;
                regs.r.regA = (unsigned char)(((result >> 8) | result) & 0xff);
                                flagCarry =  regs.r.regA & 1;
                break;
            case 1: /* rotate right */
                                flagCarry =  regs.r.regA & 1;
                result = (int)(regs.r.regA >> 1);
                regs.r.regA = (unsigned char)(((regs.r.regA<<7) | result) & 0xff);
                break;
            case 2: /* rotate left through carry */
                result = (int)regs.r.regA << 1 | flagCarry;
                flagCarry = regs.r.regA >> 7;
                regs.r.regA = (unsigned char)(result & 0xff);
                break;
            case 3: /* rotate right through carry */
                result = (int)regs.r.regA >> 1 | (flagCarry << 7);
                flagCarry = regs.r.regA & 0x1;
                regs.r.regA = (unsigned char)(result & 0xff);
                break;
            default: /*  4-7 = ??? */
                break;
        }
        break;
    case 3: /* conditional return */
        cc = chkconditional (inst);
        if (cc) {
            stackptr = (stackptr - 1) & 0x7;   //   & 0x7 to guarantee taking care of stack overflow (circular behavior)
        }
        break;
    case 4: /* math with mediate operands */
        sdata2 = (unsigned char)regs.r.regA;  /* reg A is source and target */
        sdata1 = (unsigned char)memory[stack.stk[stackptr]]; /* source is mediate */
        stack.stk[stackptr]++;
        stack.stk[stackptr] &= 0x3fff;

        sdata1 = sdata1 & 0xff;
        sdata2 = sdata2 & 0xff;

        op = (inst>>3) & 0x7;
        switch (op) {
        case 0: /* add without carry, but set carry */
            result = sdata1 + sdata2;
            regs.r.regA = (unsigned char) result;
            carryzero= 0;
            break;
        case 1: /* add with carry, and set carry */
            result = sdata1 + sdata2 + flagCarry;
            regs.r.regA = (unsigned char) result;
            carryzero= 0;
            break;
        case 2: /* subtract with no borrow, but set borrow */
            result = sdata2 - sdata1;
            regs.r.regA = (unsigned char) result;
            carryzero= 0;
            break;
        case 3: /* subtract with borrow, and set borrow */
            result = sdata2 - sdata1 -flagCarry;
            regs.r.regA = (unsigned char) result;
            carryzero= 0;
            break;
        case 4: /* logical and */
            result = sdata1 & sdata2;
            regs.r.regA = (unsigned char) result;
            carryzero= 1;
            break;
        case 5: /* exclusive or */
            result = sdata1 ^ sdata2;
            regs.r.regA = (unsigned char) result;
            carryzero= 1;
            break;
        case 6: /* or */
            result = sdata1 | sdata2;
            regs.r.regA = (unsigned char) result;
            carryzero= 1;
            break;
        case 7: /* compare (assume no borrow for now) */
            result = sdata2 - sdata1;
            carryzero= 0;
            break;
        }
        setflags (result, carryzero);
        break;
    case 5:  /* RST (call function in low memory) */
        stackptr = (stackptr + 1) & 0x7;   //   & 0x7 to guarantee taking care of stack overflow (circular behavior)
                stack.stk[stackptr] = (inst & 0x38) & 0x3fff;
        break;
    case 6: /* load mediate */    
        sdata1 = memory[stack.stk[stackptr]]; /* source is mediate */
        stack.stk[stackptr]++;
        stack.stk[stackptr] &= 0x3fff;

        reg = (inst >> 3) & 0x7;
	adr=((regs.r.regH&0x3f)<<8)+regs.r.regL;  // destination address in memory
        if (reg == 7) 
	   { 
	     if ((adr > ROM_end) && (memory[adr] != sdata1)) 
	       { memory[adr]=sdata1;   
		 // if writing something new to display memory, then set the refresh_display flag
	         // to allow display refreshing when called
	         if ((adr > 0x2020) && (adr < 0x20FF)) refresh_display++;  // increase refresh counter; when it reaches 7,
	       }                                                           // SS is refreshed
	   }
         else regs.regs[reg] = sdata1;
        break;
    case 7: /* unconditional return */
        stackptr = (stackptr - 1) & 0x7;    //   & 0x7 to guarantee taking care of stack overflow (circular behavior)
        break;
    }
    return 0;
}

int iojmpcall(unsigned char inst)
{
    unsigned int op;
    unsigned int cc; /* condition code to check, if conditional operation */
    unsigned int addrL,addrH;

    op = inst & 0x7;
    switch (op) {
    case 0: /* jump conditionally */
        cc = chkconditional (inst);
        addrL = (unsigned int)memory[stack.stk[stackptr]];
        stack.stk[stackptr]++;
        stack.stk[stackptr] &= 0x3fff;

        addrH = (unsigned int)memory[stack.stk[stackptr]];
        stack.stk[stackptr]++;
        stack.stk[stackptr] &= 0x3fff;

        if (cc) {
            stack.stk[stackptr] = (addrL + (addrH<<8)) & 0x3fff;
        }
        break;
    case 2: /* call conditionally */
        cc = chkconditional (inst);
        addrL = (unsigned int)memory[stack.stk[stackptr]];
        stack.stk[stackptr]++;
        stack.stk[stackptr] &= 0x3fff;

        addrH = (unsigned int)memory[stack.stk[stackptr]];
        stack.stk[stackptr]++;
        stack.stk[stackptr] &= 0x3fff;

        if (cc) {
            /* now bump stack to use new pc and save it */
            stackptr = (stackptr + 1) & 0x7;     //   & 0x7 to guarantee taking care of stack overflow (circular behavior)
            stack.stk[stackptr] = (addrL + (addrH<<8)) & 0x3fff;
        }
        break;
    case 4: /* jump */
        addrL = (unsigned int)memory[stack.stk[stackptr]];
        stack.stk[stackptr]++;
        stack.stk[stackptr] &= 0x3fff;

        addrH = (unsigned int)memory[stack.stk[stackptr]];
        stack.stk[stackptr] = (addrL + (addrH<<8)) & 0x3fff;


        break;
    case 6: /* call */
        addrL = (unsigned int)memory[stack.stk[stackptr]];
        stack.stk[stackptr]++;
        stack.stk[stackptr] &= 0x3fff;

        addrH = (unsigned int)memory[stack.stk[stackptr]];
        stack.stk[stackptr]++;
        stack.stk[stackptr] &= 0x3fff;

        /* now bump stack to use new pc and save it */
        stackptr = (stackptr + 1) & 0x7;   //   & 0x7 to guarantee taking care of stack overflow (circular behavior)
        stack.stk[stackptr] = (addrL + (addrH<<8)) & 0x3fff;
        break;
    default: // MCM/70 IO and reset instructions
             MCM_IO(inst);
        break;
    }
    return 0;
}

int mathboolean(unsigned char inst)
{
    unsigned int src, op;
    int carryzero;
    unsigned int sdata1, sdata2, result;

    src = inst & 0x7;
    if (src == 0x7) {
        sdata1 = memory[((regs.r.regH&0x3f)<<8)+regs.r.regL];
    } else {
        sdata1 = regs.regs[src];
    }
    sdata1 = sdata1 & 0xff;
    sdata2 = (char)regs.r.regA;  /* reg A is other source and usually target */
    sdata2 = sdata2 & 0xff;
    

    op = (inst>>3) & 0x7;
    switch (op) {
    case 0: /* add without carry, but set carry */
        result = sdata1 + sdata2;
        regs.r.regA = (unsigned char) result;
        carryzero = 0;
        break;

    case 1: /* add with carry, and set carry */
        result = sdata1 + sdata2 + flagCarry;
        regs.r.regA = (unsigned char) result;
        carryzero = 0;
        break;

    case 2: /* subtract with no borrow, but set borrow */
        result = sdata2 - sdata1;
        regs.r.regA = (unsigned char) result;
        carryzero = 0;
        break;
    case 3: /* subtract with borrow, and set borrow */
        result = sdata2 - sdata1 -flagCarry;
        regs.r.regA = (unsigned char) result;
        carryzero = 0;
        break;
    case 4: /* logical and */
        result = sdata1 & sdata2;
        regs.r.regA = (unsigned char) result;
        carryzero = 1;
        break;
    case 5: /* exclusive or */
        result = sdata1 ^ sdata2;
        regs.r.regA = (unsigned char) result;
        carryzero = 1;
        break;
    case 6: /* or */
        result = sdata1 | sdata2;
        regs.r.regA = (unsigned char) result;
        carryzero = 1;
        break;
    case 7: /* compare (assume no borrow for now) */
        result = sdata2 - sdata1;
        carryzero = 0;
        break;
    }
    setflags (result, carryzero);
    return 0;
}

void setflags (int result, int carryzero)
{ 
    if (result & 0xff) {
        flagZero = 0;
        if (result & 0x80) {
            flagSign = 1;
        } else {
            flagSign = 0;
        }
    } else {
        flagSign = 0;
        flagZero = 1;
    }
    if (carryzero) {
        flagCarry =0;
    } else if (result > 255 || result < 0) {
        flagCarry = 1;
    } else {
        flagCarry = 0;
    }
    setparity (result);
    return;
}

void setflagsinc (int result)
{
    if (result & 0xff) {
        flagZero = 0;
        if (result & 0x80) {
            flagSign = 1;
        } else {
            flagSign = 0;
        }
    } else {
        flagSign = 0;
        flagZero = 1;
    }
    setparity (result);
    return;
}

void setparity (int result)
{
    int i, p;

    p=0;
    flagParity = 1;
    for (i = 0; i<8; i++) {
        flagParity = flagParity ^ ((result>>i) & 1);
        if (result & (1<<i)) {
            p++;
        }
    }
    if (p & 1) {
        flagParity = 0;
    }
}

int load(unsigned char inst)
{
    unsigned int src,dest,adr;
    unsigned char data;

    src = inst & 0x7;          /* determine if register is data and, if so, which one */ 
    dest = (inst>>3) & 0x7;    /* determine destination: register or memory  */
    adr = (unsigned int)((regs.r.regH & 0x3f) << 8)+regs.r.regL;  // adr=HL, if destination is memory

    if (src == 0x7 && dest == 0x7) {   /* this is HALT instruction  */
        printf ("halt:%2x\n",inst);
        return 1;
    }
    if (src == 0x7) {        /* data comes from memory   */
        data = memory[adr];
    } else {                 /* data comes from register */
        data = regs.regs[src];
    }
    if (dest == 0x7)         /* load to memory */
      { 
	 if ((adr > ROM_end) && (memory[adr] != data))
	    { memory[adr]=data;        
	      // if writing something new to display memory, then set the refresh_display flag
	      // to allow display refreshing when called
	      if ((adr > 0x2020) && (adr < 0x20FF)) refresh_display++;  
	    }
      }	     
    else                   /* load a register          */
        regs.regs[dest] = data;
    
    return 0;
}

int chkconditional (unsigned char inst)
{
    unsigned int cc;
    cc = (inst & 0x38) >> 3;

    switch (cc) {
    case 0: /* overflow false */
        cc = !flagCarry;
        break;
    case 1: /* zero false */
        cc = !flagZero;
        break;
    case 2: /* sign false */
        cc = !flagSign;
        break;
    case 3: /* parity false */
        cc = !flagParity;
        break;
    case 4: /* overflow true */
        cc = flagCarry;
        break;
    case 5: /* zero true */
        cc = flagZero;
        break;
    case 6: /* sign true */
        cc = flagSign;
        break;
    case 7: /* parity true */
        cc = flagParity;
        break;
    }
    return (cc);
}


// IO instructions
void MCM_IO (unsigned char inst)
{
    BYTE sp;
     
    switch (inst) {

      case 0x41: //  INP 00:  keyboard input
                 key_requested=1;                     // turn keyboard read on
	             regs.r.regA = get_row(regs.r.regA);  // collect keyboard input from kbd queue
                 break;
      case 0x43: // INP 01 -- system status check and other functions (uses input in register A, and output to A)
                switch (regs.r.regA)
	             {
		          case 0: //  check if power on; the value returned in A will be the "BOUNCE" line from the power supply on bit 1
                          if (power == 0) regs.r.regA=2;  // if power down, then return this as status
                          break;  
                  case 1: /* Sets the RESET line of Omniport to 1 to force peripherals to perform their initializations
                             appropriate for these devices. The RESET signal is sent during poer-up (after START key is pressed)
                             or during system reset time. 

                             Reset cassette drives -- resets USRT by modifying cassettes' status in the following way:
                             - Received Over Run: set bit 4 to 0
                             - Received Parity Error: set bit 5 to 0
                             - transmit buffer empty: set bit 6 to 1
                             - clear all receive bits: set bit 7 to 0
                        */
                             tape0_s.status = (tape0_s.status & 0x0F) | 0x40; 
                             tape1_s.status = (tape1_s.status & 0x0F) | 0x40; 
                          
                          // reset auxiliary variables for tapes:   
                             tape0_s.speed=0;        tape1_s.speed=0;       // tapes not moving 
                             move_clock=0;
                     
                          // Reset printer, if conected
                          if (Printer) {
                             mcm_printer_reset(); // platform-neutral printer reset hook
                             pr_status = 241;
                          } 
		                  break;
                  case 2: // Turn off computer (e.g.[]OFF or on power fail): clear RAM, registers, etc.
                          resetMCM ();
		                  break;
                  case 4:/* Refresh SelfScan: display the current content of display memory on SelfScan
                            This should be a call to refresh_SS(). However, this is already taken care of 
                            by the emulation of DMA at the very beginning of this code */
		                  break;
                 default: printf("INP 01 with A=%d -- not implemented\n", regs.r.regA);
	             }   
	            break;
      case 0x45: // INP 02 -- get the status of selected device into the accumulator
	            switch (SelectedDevice)
                {
                 case 0x00: // no output device is selected, return 0; this happens, for instance, 
                           // when trying to print text when printer is not yet selected
                           regs.r.regA=0;
                           break;
		         case 0x01: // printer  
		                   if (Printer == 0) regs.r.regA=0; // printer not connected, so no status, no Answer-Back Code
                           else if (ABC ==1) 
				                  {regs.r.regA=66;         // return Answer-Back Code on 1st request for status;
			                                               //    66 value comes from printer's jumpers
				                  ABC=0;                   // reset ABC so that the consecutive status requests 
                                  }                        //    return "real" status
                               else regs.r.regA=pr_status;     
			              break;
                case 0x0A: // future device (template)
                            // regs.r.regA=Device_0A_status;
                          break;	     
                case 0xC8: // cassette tape 1
                           regs.r.regA= tape0_s.status;                    // status of tape 0 returned in A
                          break;
                case 0xC9: // cassette tape 2
                          regs.r.regA= tape1_s.status;                    // status of tape 1 returned in A
		                  break;
		        default:   // get device status of a peripheral
            printf("INP 02: this status info is not yet implemented, SelectedDevice=%d A=%d\n",SelectedDevice,regs.r.regA);		     
		       }			     
                break;
      case 0x47: // INP 03: (GDI) read the selected device's data register 
	         switch (SelectedDevice)
	           {
		     case 0xC8: // 1st cassette drive
		                regs.r.regA = (mcm_tape_is_mounted(0) && tape0_s.w_head >= delta && tape0_s.w_head < tape0_s.length)
                                    ? (mcm_tape_read(0, static_cast<uint32_t>(tape0_s.w_head - delta)) & 0xFF) : 0;  
                                tape0_s.status = (tape0_s.status & 0x7F);     // Clear receive data available        
			        break;
	             case 0xC9: // 2nd cassette drive
		                regs.r.regA = (mcm_tape_is_mounted(1) && tape1_s.w_head >= delta && tape1_s.w_head < tape1_s.length)
                                    ? (mcm_tape_read(1, static_cast<uint32_t>(tape1_s.w_head - delta)) & 0xFF) : 0;  
                                tape1_s.status = (tape1_s.status & 0x7F);     // Clear receive data available        
			        break;
		     default:   printf("INP 03: reading data from device -- not yet implemented, SelectedDevice=%d AC=%d\n",SelectedDevice,regs.r.regA);
	          }	 
                 break;
      case 0x51: // OUT 08 -- MCM/70 instruction to switch ROM; bank number is the top nibble of A
	            bank=(regs.r.regA  >> 4)*2048;           // address in ROM[] of the first byte to copy
	            memcpy(&memory[0x1800],&ROM[bank],2048); // bank copied to memory starting at address 0x1800 
	            break;
      case 0x53: /* OUT 09   -- I/O device selection:
	            - device address (given in reg. A) is placed on OMNIPORT data lines  -- emulated but not implemented;
	            - AOS (Address Out Strobe) is set to 1-- emulated but not implemented.*/
	       
	            if (SelectedDevice == 1) {
		           if (Printer == 0) break;         // printer not connected
		           else  ABC=1;                     // set Answer-Back Code request flag ABC
                    }
	         
                 SelectedDevice=regs.r.regA;                 // emulator's internal storage for device address
                 break;      
      case 0x55: // OUT 0A -- control selected device; the parameters are encoded in register A
	         switch (SelectedDevice) 
	           {
		    case 0x01: /* printer 
		                  printer's command is a 2-byte long instruction: the ms byte is in regs.r.regA and
		                  the ls byte is already supplied by OUT 0B and is stored in  pr_data*/
		               runPrinter (regs.r.regA);        // execute printer's command
		               break;
		    case 0x0A: // future device (template)
		               // runDevice_0A (regs.r.regA);        // execute printer's command
		               break;
		    case 0xC8: /* Set tape 1 actions/parameters: tape movement direction/speed, etc.
		                  Check if change of the direction of tape's movement is requested*/
		               sp=tape0_s.speed & 0x07;                // record current tape movement direction (3 ls bits only)
		               if (sp != (regs.r.regA & 0x07))         // if change of direction, then...
				       switch (regs.r.regA & 0x07)
				         {
				          case 0: // stop tape
					             break;
				          case 3: // forward movement
					             break;
				          case 5: // reverse movement (visual animation omitted)
                                     break;
				         }
				 
			           // set new tape actions/parameters 
		               tape0_s.speed=regs.r.regA;            
                       if ((regs.r.regA & 0x01) == 1)          // if reset tape request:
                         {
                          //  if (tape0_s.status & 0x04) tape0_s.status= (tape0_s.status & 0xF7);  // if cassette unloaded, then make it ready 
                          tape0_s.status= (tape0_s.status & 0x87);   // clear receive flags but preserve "byte ready", 
                                                                              // make cassette ready
                          tape0_s.status= (tape0_s.status | 0x40);   // set transmit buffer to empty
                         }                                       
                       break;
		    case 0xC9: // Set tape 2 actions/parameters: tape movement direction/speed, etc.
		               sp=tape1_s.speed & 0x07;              
		               if (sp != (regs.r.regA & 0x07))    
				       switch (regs.r.regA & 0x07)
				         {
				          case 0: // stop tape
					             break;
                         case 3: // forward movement
					            break;
                         case 5: // reverse movement (visual animation omitted)
                            break;
				        }
		   
		              tape1_s.speed=regs.r.regA; 
                      if ((regs.r.regA & 0x01) == 1)          // if reset tape request:
			             { 
                          tape1_s.status= (tape1_s.status & 0x87);  
		                  tape1_s.status= (tape1_s.status | 0x40);  
                         }                                       
                      break;
			default: printf("OUT 0A: controlling device %d not implemented -- control value=%d\n",SelectedDevice,regs.r.regA);     
	       }
               break;
      case 0x57: //  OUT 0B -- send data (stored in register A) to currently selected I/O device.                          
	         switch (SelectedDevice)
	           {
		    case 0x01: // printer
		               pr_data=regs.r.regA;  // pr_data stores the ls byte of 2-byte printer command;
			                                 // the ms byte is supplied via OUT 0A that follows OUT 0B
		               break;
		    case 0xC8: // output to TAPE 0
		               if ((tape0_s.speed & 0x10) == 0) break;  // not a tape data, just a hardware signal
		               if ((tape0_s.speed == 0) | (tape0_s.speed == 0x10)) break; // no writing when tape is halted
		           
		               // all is fine, write to tape
			       if (mcm_tape_is_mounted(0) && tape0_s.w_head >= 0 && tape0_s.w_head < tape0_s.length)
                                 mcm_tape_write(0, static_cast<uint32_t>(tape0_s.w_head), regs.r.regA);
                               moveTape(0);                              // move tape 0                              
                               break;
		    case 0xC9: // output to TAPE 1
		               if ((tape1_s.speed & 0x10) == 0) break; 
		               if ((tape1_s.speed == 0) | (tape1_s.speed == 0x10)) break;
			       if (mcm_tape_is_mounted(1) && tape1_s.w_head >= 0 && tape1_s.w_head < tape1_s.length)
                                 mcm_tape_write(1, static_cast<uint32_t>(tape1_s.w_head), regs.r.regA);
                               moveTape(1);                                     
                               break;
		    default:    printf("OUT 0B: writing to device %d not implemented; writing %d\n",SelectedDevice,regs.r.regA);
	         }   
              break;
     case 0x7B:  /* send request to reset printer, caused by printer error, e.g. out of paper
                    MCM/70 displays COMM DEVICE ERROR
                    this instruction is called from addr=18C9 in ROM=C 
                    in this implementation, printer's error light will be turned on; 
                    the reset function is accomplished by clicking on printer's reset button; */ 
                 break;
     case 0x7D:  wait140( );      // "sleep" for 140uSec
                 break;   
     case 0x7F: /* OUT 1F -- Output a single column from display memory of MCM/70 to SelfScan Display.
                              Because SelfScan is not emulated (refresh is done "globally" by GLUT) -- no need
	                      to do anything: the emulator's display memory is used directly as SelfScan memory. 
	           */
                 break;
     case 0x09:  printf("unimplemented OUT 0x09 %X used! at addr=%X, ROM=%X\n",inst,stack.stk[stackptr],memory[0x1FFF]);
                // functionality unknown,... see ROM 2, addr. 1872 
                 break;
     case 0x0C:  printf("unimplemented OUT 0x0C%X used! at addr=%X, ROM=%X\n",inst,stack.stk[stackptr],memory[0x1FFF]);
                // functionality unknown,... 
                 break;
 	 
     default:   printf("unimplemented I/O %X used! at addr=%X, ROM=%X; selected device=%d\n",inst,stack.stk[stackptr],memory[0x1FFF],SelectedDevice);  
    }
}

//----------------------------------------------------------------------------------------------
//  NextInstr: idele callback -- gets called continuously to execute emulator's CPU instructions
//----------------------------------------------------------------------------------------------

void NextInstr()
{
    if (mcm_on && power) {
        (void)runCPU();
        ++mcm_total_steps;
    }
}
