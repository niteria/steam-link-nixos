#include <assert.h>
#include <stdbool.h>
#include "steamlink_readonly.h"

int main(void)
{
	unsigned int opcode, allowed = 0;
	for (opcode = 0; opcode < 256; opcode++) {
		assert(!steamlink_readonly_instruction_allowed(true, true, opcode));
		assert(!steamlink_readonly_instruction_allowed(true, false, opcode));
		if (steamlink_readonly_instruction_allowed(false, true, opcode))
			allowed++;
	}
	assert(allowed == 9);
	assert(steamlink_readonly_instruction_allowed(false, false, 0));
	assert(steamlink_readonly_opcode_allowed(0x90));
	assert(steamlink_readonly_opcode_allowed(0x00));
	assert(steamlink_readonly_opcode_allowed(0x30));
	assert(!steamlink_readonly_opcode_allowed(0x80)); /* PROGRAM SETUP */
	assert(!steamlink_readonly_opcode_allowed(0x10)); /* PROGRAM CONFIRM */
	assert(!steamlink_readonly_opcode_allowed(0x15)); /* CACHED PROGRAM */
	assert(!steamlink_readonly_opcode_allowed(0x85)); /* RANDOM DATA INPUT */
	assert(!steamlink_readonly_opcode_allowed(0x60)); /* ERASE SETUP */
	assert(!steamlink_readonly_opcode_allowed(0xd0)); /* ERASE CONFIRM */
	assert(!steamlink_readonly_opcode_allowed(0xef)); /* SET FEATURES */
	assert(!steamlink_readonly_opcode_allowed(256));
	return 0;
}
