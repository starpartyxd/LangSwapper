/*
 *  LangSwapper
 *
 *  Copyright (C) 2014 Omega2058
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <pspkernel.h>
#include <pspthreadman.h>
#include <pspctrl.h>

// Defines for sceImposeSetLanguageMode.
#define ASM_RANGE_MAX 				0x84
#define ASM_LANGUAGE_INSTRUCTION	0xAD84033C		// sw $a0, 828($t4)
#define ASM_BUTTON_INSTRUCTION		0xAD850340		// sw $a1, 832($t4)
#define ASM_PATCHED_INSTRUCTION		0xAD8D0340		// sw $t5, 832($t4)

// Defines for sceUtilitySavedataInitStart.
#define InitStart_OFFSET			0x18

#define NOP							0x00000000
#define MAKE_CALL(f)				(0x0C000000 | (((u32)(f) >> 2) & 0x03ffffff))

PSP_MODULE_INFO("LangSwapper", PSP_MODULE_KERNEL, 1, 0);

SceUID thid;
u32 _sceImposeSetLanguageMode;
u32 _sceUtilitySavedataInitStart, sd_sub;

/**
 * Clears the data and instruction cache.
 */
void ClearCaches(void) {
	sceKernelDcacheWritebackAll();
	sceKernelIcacheClearAll();
}

/**
 * Patches sceUtilitySavedataInitStart by hooking into the function and changing the language parameter.
 *
 * Huzzah, double pointers~.
 */
void patched_sceUtilitySavedataInitStart(u32 a0, u32 a1) {
	u32 k1 = pspSdkSetK1(0);
	int i, param_struct[2], ptr[2];

	// Get the language/button pointers and store them on the stack.
	for (i = 0; i < 2; i++) {
		_sw((a1 + 0x4) + (i * 4), ptr[i]);
	}

	// Accesses the final location of the structure in user space and
	// patches the final values for language/button.
	for (i = 0; i < 2; i++) {
		param_struct[i] = _lw(ptr[i]);
		_sw(1, param_struct[i]);
	}

	pspSdkSetK1(k1);

	void (*fnc)( u32, u32) = (void *)sd_sub;
	fnc(a0, a1);
}

/**
 * This function cycles through memory to find out where the mode for the language and buttons are stored and patches it.
 *
 * @ASM_OLD - sw $a1, 832($t4).
 * @ASM_NEW - sw $t5, 832($t4).
 *
 * Up to 12 modes (0 to 11) exist for the language.
 * Setting it to 1 forces it to always be set based on the System Language that you've chosen in the XMB.
 *
 * Only two modes exist for the button mode (0 and 1).
 * 0 -> (X as BACK / O as ENTER).
 * 1 -> (X as ENTER /O as BACK).
 * We simply force the mode to always be set to 1, regardless.
 * sceImposeSetLanguageMode() already stores 1 in register $t5 already.
 * So I simply replace the original ASM intruction ("ASM_OLD"(See above)) with my own ("ASM_NEW"(See above)).
 *
 * TLDR: The function below performs voodoo magic, which does things in memory. :)
 */
void patchHomeMenu(u32 addr) {
	int i;
	for (i = 0; i < ASM_RANGE_MAX; i += 4) {
		if (_lw(addr + i) == ASM_LANGUAGE_INSTRUCTION
				|| _lw(addr + i) == ASM_BUTTON_INSTRUCTION) {
			_sw(ASM_PATCHED_INSTRUCTION, addr + i);
		}
	}
}

/**
 * Gets the sub address called by sceUtilitySavedataInitStart and patches it to point to our own sub.
 * Once the patching finishes it continues to run normally.
 */
void patchSaveData(u32 addr) {
	//get the sub address called by sceUtilitySavedataInitStart
	sd_sub = ((*(u32*) (addr + InitStart_OFFSET) & 0x03FFFFFF) << 2)
			| 0x80000000;
	_sw(MAKE_CALL(patched_sceUtilitySavedataInitStart),
			addr + InitStart_OFFSET);
}

/**
 * Main function that does the black magic.
 */
int mainThread(SceSize args, void *argp) {

	// Find the function responsible for the booting status of the system.
	int (*_sceKernelGetSystemStatus)(
			void) = (void *)sctrlHENFindFunction("sceSystemMemoryManager", "SysMemForKernel", 0x36C503A9);

	//wait until the system returns booted status
	while (_sceKernelGetSystemStatus() != 0x20000)
	sceKernelDelayThread(1000);

	// Find the function in kernel land and patch the Home menu language and button input.
	_sceImposeSetLanguageMode = sctrlHENFindFunction("sceImpose_Driver",
			"sceImpose", 0x36AA6E91);
	if(_sceImposeSetLanguageMode) {
		patchHomeMenu(_sceImposeSetLanguageMode);
	}

	// Find the function in kernel land responsible for handling savedata.
	_sceUtilitySavedataInitStart = sctrlHENFindFunction("sceUtility_Driver", "sceUtility", 0x50C4CD57);
	if(_sceUtilitySavedataInitStart) {
		patchSaveData(_sceUtilitySavedataInitStart);
	}

	ClearCaches();
	return 0;
}

/**
 * Starts the module.
 */
int module_start(SceSize args, void *argp) {
	thid = sceKernelCreateThread("FunctionThread", &mainThread, 0x18, 0x1000, 0,
			NULL);
	if (thid >= 0)
		sceKernelStartThread(thid, 0, NULL);
	return 0;
}

/**
 * Stops the module.
 */
int module_stop(SceSize args, void *argp) {
	sceKernelTerminateDeleteThread(thid);
	return 0;
}
