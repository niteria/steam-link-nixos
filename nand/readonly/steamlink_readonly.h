/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef STEAMLINK_READONLY_H
#define STEAMLINK_READONLY_H

static inline bool steamlink_readonly_opcode_allowed(unsigned int opcode)
{
	switch (opcode) {
	case 0x00: /* READ0 */
	case 0x05: /* RNDOUT */
	case 0x30: /* READSTART */
	case 0x70: /* STATUS */
	case 0x90: /* READID */
	case 0xe0: /* RNDOUTSTART */
	case 0xec: /* PARAM */
	case 0xee: /* GET FEATURES */
	case 0xff: /* RESET (volatile chip state only) */
		return true;
	default:
		return false;
	}
}

static inline bool steamlink_readonly_instruction_allowed(bool data_out,
						 bool command, unsigned int opcode)
{
	return !data_out && (!command || steamlink_readonly_opcode_allowed(opcode));
}
#endif
