/*
	Emulation of all processes carried out by the Central Processing Unit (CPU)
*/

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include "cpu.h"
#include "memory.h"


int WAITING_FOR_SIGNAL = TRUE;			/* Flag to handle Signals */
struct PSW_BITS* PSWptr;				/* Initialzing PSW */
enum CPU_STATES state;					/* State of the CPU */
enum WORD_BYTE wb;						/* Determining Word or Byte type, based on Instruction Opcode */

/*
	Initializing System Clock.
	Using arbitrary data type with widest range in positive integers.
	Range of Values: 0 to 4,294,967,295
*/
unsigned long SYS_CLK;

/*
	Initializing Register File.
	16-bit Registers.
	Range of Values: -32768 to 32767
*/
signed short REG_FILE[] = {
	0,	/* R0 - General Purpose Register */
	0,	/* R1 - General Purpose Register */
	0,	/* R2 - General Purpose Register */
	0,	/* R3 - General Purpose Register */
	0,	/* R4 - LR */
	0,	/* R5 - SP */
	0,	/* R6 - PSW */
	0	/* R7 - PC */
};

/*
	Branch Instructions functions pointer table
*/
void(*BRANCH_PTR[])(unsigned, ...) = {
	Process_BEQ,	/* Bits: (12, 11, 10) = 0 0 0; Same as BZ */
	Process_BNE,	/* Bits: (12, 11, 10) = 0 0 1; Same as BNE */
	Process_BC,		/* Bits: (12, 11, 10) = 0 1 0; Same as BHS */
	Process_BNC,	/* Bits: (12, 11, 10) = 0 1 1; Same as BLO */
	Process_BN,		/* Bits: (12, 11, 10) = 1 0 0 */
	Process_BGE,	/* Bits: (12, 11, 10) = 1 0 1 */
	Process_BLT,	/* Bits: (12, 11, 10) = 1 1 0 */
	Process_BAL		/* Bits: (12, 11, 10) = 1 1 1 */
};

/*
	Arithmetic Instructions functions pointer table
*/
void(*ARITHMETIC_PTR[])(unsigned, ...) = {
	Process_ADD,   /* Bits: (12, 11, 10, 9, 8) = 0 0 0 0 0 */
	none,
	Process_ADDC,  /* Bits: (12, 11, 10, 9, 8) = 0 0 0 1 0 */
	none,
	Process_SUB,   /* Bits: (12, 11, 10, 9, 8) = 0 0 1 0 0 */
	none,
	Process_SUBC,  /* Bits: (12, 11, 10, 9, 8) = 0 0 1 1 0 */
	none,
	Process_DADD,  /* Bits: (12, 11, 10, 9, 8) = 0 1 0 0 0 */
	none,
	Process_CMP,   /* Bits: (12, 11, 10, 9, 8) = 0 1 0 1 0 */
	none,
	Process_XOR,   /* Bits: (12, 11, 10, 9, 8) = 0 1 1 0 0 */
	none,
	Process_AND,   /* Bits: (12, 11, 10, 9, 8) = 0 1 1 1 0 */
	none,
	Process_BIT,   /* Bits: (12, 11, 10, 9, 8) = 1 0 0 0 0 */
	Process_SRA,   /* Bits: (12, 11, 10, 9, 8) = 1 0 0 0 1 */
	Process_BIC,   /* Bits: (12, 11, 10, 9, 8) = 1 0 0 1 0 */
	Process_RRC,   /* Bits: (12, 11, 10, 9, 8) = 1 0 0 1 1 */
	Process_BIS,   /* Bits: (12, 11, 10, 9, 8) = 1 0 1 0 0 */
	Process_SWPB,  /* Bits: (12, 11, 10, 9, 8) = 1 0 1 0 1 */
	Process_MOV,   /* Bits: (12, 11, 10, 9, 8) = 1 0 1 1 0 */
	Process_SXT,   /* Bits: (12, 11, 10, 9, 8) = 1 0 1 1 1 */
	Process_SWAP   /* Bits: (12, 11, 10, 9, 8) = 1 1 0 0 0 */
};


/*
	Handler for SIGINT (^c) Signal.
	Persists the state of the Signal and prevents program from running further.
	Note: In the "signal()" function call, the handler must be casted to "_crt_signal_t" type.
*/
void SignalHandler() {
	WAITING_FOR_SIGNAL = FALSE;
	signal(SIGINT, (_crt_signal_t)SignalHandler);
}


/*
	Emulation of the CPU.
	This is where the Fetch - Decode - Execute takes places
	- Machine goes in to and infinite loop in the FDE cycle.
	- Loop is broken if:
		-> "^c" Signal is detected
		-> Some condition, set by the debugger, is met
		-> PSW.SLP is set
	Note: Interrupts and Devices are handled at the end of each FDE cycle.
*/
void RunMachine(void) {

	unsigned short INST;							/* 16-bit Instruction */
	unsigned int   type;							/* Save type of Inst */
	unsigned int   temp_type;						/* Save secondary type of Inst */

	state = FETCH;									/* Initializing state of CPU */
	signal(SIGINT, (_crt_signal_t)SignalHandler);	/* Handler function for SIGNINT signals */

	while (WAITING_FOR_SIGNAL) {
		switch (state) {
			case FETCH:
				/* Get WORD from Memory */
				if ((INST = fetch()) == FALSE) {	/* If Instrution is not fetched */
					state = HANDLE_DEVICES;
				}
				else
					state = DECODE;
				REG_FILE[PC] += 2;					/* Increment PC */
				break;
			case DECODE:
				type = INST_TYPE(INST);				/* Get Instruction Type */
				state = EXECUTE;
				break;
			case EXECUTE:
				switch (type) {
				case BRANCH_BL:
					Process_BL(BL_OFFSET(INST));
					break;
				case BRANCH:
					BRANCH_PTR[BRANCH_TYPE(INST)]((BRANCH_OFFSET(INST)));
					break;
				case ARITHMETIC:
					ARITHMETIC_PTR[ARITH_TYPE(INST)](ARITH_RC(INST), ARITH_WB(INST), ARITH_SRC(INST), ARITH_DST(INST));
					break;
				case LD_ST_MOVL_MOVLZ:
					temp_type = (INST & 0x1800) >> 11;		/* Extract bits 12 and 11 */
					switch (temp_type) {
					case LD:
						Process_LD(LD_ST_PRPO(INST), LD_ST_DEC(INST), LD_ST_INC(INST), LD_ST_WB(INST), LD_ST_SRC(INST), LD_ST_DST(INST));
						break;
					case ST:
						Process_ST(LD_ST_PRPO(INST), LD_ST_DEC(INST), LD_ST_INC(INST), LD_ST_WB(INST), LD_ST_SRC(INST), LD_ST_DST(INST));
						break;
					case MOVL:
						Process_MOVL(MOV_BYTE(INST), MOV_DST(INST));
						break;
					case MOVLZ:
						Process_MOVLZ(MOV_BYTE(INST), MOV_DST(INST));
						break;
					}
					break;
				case MOVH:
					Process_MOVH(MOV_BYTE(INST), MOV_DST(INST));
					break;
				case LDR:
					Process_LDR(LDR_STR_OFFSET(INST), LDR_STR_OFFSET_WB(INST), LDR_STR_OFFSET_SRC(INST), LDR_STR_OFFSET_DST(INST));
					break;
				case STR:
					Process_STR(LDR_STR_OFFSET(INST), LDR_STR_OFFSET_WB(INST), LDR_STR_OFFSET_SRC(INST), LDR_STR_OFFSET_DST(INST));
					break;
				}
				SYS_CLK++;	/* Increment SYS_CLK at the end of evey cycle */
				state = HANDLE_DEVICES;
				break;
			case HANDLE_DEVICES:
				// Handle Interrupts
				state = FETCH;
				break;
		}
	} /* End of while loop */

}



/*
	Fetch Instruction from location at PC
*/
unsigned short fetch(void) {
	unsigned short inst = FALSE;
	unsigned short eff_addr = REG_FILE[PC];		/* Effective Address */

	/*
		Prevent Accessing Location 0xFFFF (HCF).
		Read Memory through Bus.
		Note: Bus swaps LO and HI bytes to account for 'Little-Endian-ness'
	*/
	if (eff_addr != HCF) 
		MEM_RD(eff_addr, inst, WORD);			/* Read from Memory */
	
	return inst;
}