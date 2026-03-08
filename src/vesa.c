/* vesa.c, Low level VESA set/get functions for querying or setting screen modes
 for the x86Launcher.
 Copyright (C) 2021  John Snowdon
 
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dpmi.h>
#include <go32.h>
#include <sys/farptr.h>
#include "watcom_compat.h"

#include "vesa.h"

int vesa_GetModeInfo(unsigned short mode, vesamodeinfo_t *modeinfo) {
	__dpmi_regs r;

	if (VESA_VERBOSE) {
		printf("%s.%d\t Quering mode %xh\n", __FILE__, __LINE__, mode);
	}

	memset(&r, 0, sizeof(r));
	r.x.ax = VESA_MODE_INFO;
	r.x.cx = mode;
	r.x.es = __tb >> 4;
	r.x.di = __tb & 0xF;

	__dpmi_int(VESA_INTERRUPT, &r);

	if (r.x.ax != VESA_BIOS_SUCCESS) {
		if (VESA_VERBOSE) {
			printf("%s.%d\t Error, Unable to query VESA mode [return code 0x%04lx]\n", __FILE__, __LINE__, r.x.ax);
		}
		return -1;
	}

	dosmemget(__tb, sizeof(vesamodeinfo_t), modeinfo);

	if (VESA_VERBOSE > 1) {
		printf("%s.%d\t VESA mode queried successfully!\n", __FILE__, __LINE__);
		vesa_PrintVBEModeInfo(modeinfo);
	}

	return 0;
}

int vesa_GetVBEInfo(vbeinfo_t *vbeinfo) {
	__dpmi_regs r;

	// Copy "VBE2" signature into transfer buffer to request VBE 2.0 info
	dosmemput("VBE2", 4, __tb);

	memset(&r, 0, sizeof(r));
	r.x.ax = 0x4F00;
	r.x.es = __tb >> 4;
	r.x.di = __tb & 0xF;

	__dpmi_int(0x10, &r);

	if (r.x.ax != 0x004F) {
		printf("%s.%d\t vesa_GetVBEInfo() failed [return code 0x%04lx]\n", __FILE__, __LINE__, r.x.ax);
		return -1;
	}

	dosmemget(__tb, sizeof(vbeinfo_t), vbeinfo);

	return 0;
}

int vesa_HasMode(unsigned short int mode, vbeinfo_t *vbeinfo) {
	vesamodeinfo_t *modeinfo = (vesamodeinfo_t *) calloc(1,sizeof(vesamodeinfo_t));
	int result = -1;

	// Directly query the mode instead of scanning the mode list
	if (vesa_GetModeInfo(mode, modeinfo) == 0) {
		if (modeinfo->ModeAttributes & 0x01) {
			// Bit 0 of ModeAttributes = mode is supported
			result = 0;
		}
	}

	free(modeinfo);
	return result;
}

int vesa_SetDAC(unsigned char width) {
	__dpmi_regs r;

	if (VESA_VERBOSE) {
		printf("%s.%d\t vesa_SetDAC() Setting VGA DAC mode %dbpp\n", __FILE__, __LINE__, width);
	}

	memset(&r, 0, sizeof(r));
	r.x.ax = VESA_DAC_SET;
	r.h.bl = 0x00;
	r.h.bh = width;

	__dpmi_int(VESA_INTERRUPT, &r);

	if (r.x.ax != VESA_BIOS_SUCCESS) {
		if (VESA_VERBOSE) {
			printf("%s.%d\t vesa_SetDAC() Error, Unable to set VGA DAC mode %dbpp [return code 0x%04lx]\n", __FILE__, __LINE__, width, r.x.ax);
		}
		return -1;
	}

	if (VESA_VERBOSE) {
		printf("%s.%d\t vesa_SetDAC() Successfully set VGA DAC mode %dbpp (returned %dbpp)\n", __FILE__, __LINE__, width, r.h.bh);
	}
	return 0;
}

int vesa_GetDAC(unsigned char width) {
	__dpmi_regs r;

	if (VESA_VERBOSE) {
		printf("%s.%d\t vesa_GetDAC() Checking VGA DAC mode for %dbpp\n", __FILE__, __LINE__, width);
	}

	memset(&r, 0, sizeof(r));
	r.x.ax = VESA_DAC_SET;
	r.h.bl = 0x01;

	__dpmi_int(VESA_INTERRUPT, &r);

	if (r.x.ax != VESA_BIOS_SUCCESS) {
		if (VESA_VERBOSE) {
			printf("%s.%d\t vesa_GetDAC() Error, Unable to check for VGA DAC mode %dbpp [return code 0x%04lx]\n", __FILE__, __LINE__, width, r.x.ax);
		}
		return -1;
	}

	if (VESA_VERBOSE) {
		if (r.h.bh != width) {
			printf("%s.%d\t vesa_GetDAC() VGA DAC mode is %dbpp, this is WRONG!\n", __FILE__, __LINE__, r.h.bh);
		} else {
			printf("%s.%d\t vesa_GetDAC() VGA DAC mode is %dbpp, this is CORRECT!\n", __FILE__, __LINE__, r.h.bh);
		}
	}

	return (r.h.bh != width) ? -1 : 0;
}

int vesa_SetMode(unsigned short int mode) {
	__dpmi_regs r;

	if (VESA_VERBOSE) {
		printf("%s.%d\t vesa_SetMode() Setting VESA mode %xh\n", __FILE__, __LINE__, mode);
	}

	memset(&r, 0, sizeof(r));
	r.x.ax = VESA_MODE_SET;
	r.x.bx = mode;

	__dpmi_int(VESA_INTERRUPT, &r);

	if (r.x.ax != VESA_BIOS_SUCCESS) {
		if (VESA_VERBOSE) {
			printf("%s.%d\t vesa_SetMode() Error, Unable to set VESA mode %xh [return code 0x%04lx]\n", __FILE__, __LINE__, mode, r.x.ax);
		}
		return -1;
	}

	if (VESA_VERBOSE) {
		printf("%s.%d\t vesa_SetMode() Successfully set VESA mode %xh\n", __FILE__, __LINE__, mode);
	}
	return 0;
}

int vesa_SetWindow(unsigned short int position) {
	__dpmi_regs r;

	memset(&r, 0, sizeof(r));
	r.x.ax = VESA_WINDOW_SET;
	r.h.bh = 0;
	r.h.bl = 0;
	r.x.dx = position;

	__dpmi_int(VESA_INTERRUPT, &r);

	if (r.x.ax != VESA_BIOS_SUCCESS) {
		if (VESA_VERBOSE) {
			printf("%s.%d\t vesa_SetWindow() Error, Unable to set VESA memory region window to position %d [return code 0x%04lx]\n", __FILE__, __LINE__, position, r.x.ax);
		}
		return -1;
	}

	if (VESA_VERBOSE) {
		printf("%s.%d\t vesa_SetWindow() Successfully set VESA window %d\n", __FILE__, __LINE__, position);
	}

	return 0;
}

void vesa_PrintVBEInfo(vbeinfo_t *vbeinfo) {
	printf("%s.%d\t vesa_PrintVBEInfo() VESA BIOS information follows\n", __FILE__, __LINE__);
	printf("VBE Signature:\t %s\n", vbeinfo->vbe_signature);
	printf("VBE Version:\t %d\n", vbeinfo->vbe_version);
	printf("SW Version:\t %d\n", vbeinfo->oem_software_rev);
	printf("Total RAM:\t %dKB\n", (vbeinfo->total_memory * 64));
	if (vbeinfo->capabilities & 0x01) {
		printf("DAC Type:\t Switchable, 6bit + other\n");
	} else {
		printf("DAC Type:\t Fixed, 6bit\n");
	}
	printf("%s.%d\t vesa_PrintVBEInfo() End of VESA BIOS information\n", __FILE__, __LINE__);
	printf("----------\n");
}

void vesa_PrintVBEModes(vbeinfo_t *vbeinfo) {
	int i;
	unsigned short int cur_mode;
	unsigned long mode_list_addr;

	printf("%s.%d\t vesa_PrintVBEModes() VESA mode list follows\n", __FILE__, __LINE__);

	mode_list_addr = ((vbeinfo->mode_list_ptr >> 16) << 4) + (vbeinfo->mode_list_ptr & 0xFFFF);

	for (i = 0;; i++) {
		dosmemget(mode_list_addr + i * 2, 2, &cur_mode);
		if (cur_mode == VESA_MODELIST_LAST) break;
		printf("Mode %3d: %xh\n", i, cur_mode);
	}
	printf("%s.%d\t vesa_PrintVBEModes() Found %d VESA modes\n", __FILE__, __LINE__, i);
	printf("----------\n");
}

void vesa_PrintVBEModeInfo(vesamodeinfo_t *modeinfo) {
	printf("%s.%d\t vesa_PrintVBEModeInfo() VESA mode information follows\n", __FILE__, __LINE__);
	printf("Resolution\t\t: %d x %d\n", modeinfo->XResolution, modeinfo->YResolution);
	printf("Colour Depth\t\t: %dbpp\n", modeinfo->BitsPerPixel);
	printf("Bitplanes\t\t: %d\n", modeinfo->NumberOfPlanes);
	printf("Supported\t\t: %d\n", modeinfo->ModeAttributes);
	printf("WinAAttributes\t\t: %d\n", modeinfo->WinAAttributes);
	printf("WinBAttributes\t\t: %d\n", modeinfo->WinBAttributes);
	printf("WinGranularity\t\t: %d\n", modeinfo->WinGranularity);
	printf("WinSize\t\t\t: %d\n", modeinfo->WinSize);
	printf("WinASegment\t\t: %xh\n", modeinfo->WinASegment);
	printf("WinBSegment\t\t: %xh\n", modeinfo->WinBSegment);
	printf("BytesPerScanLine\t: %d\n", modeinfo->BytesPerScanLine);
	printf("XCharSize\t\t: %d\n", modeinfo->XCharSize);
	printf("YCharSize\t\t: %d\n", modeinfo->YCharSize);
	printf("NumberOfPlanes\t\t: %d\n", modeinfo->NumberOfPlanes);
	printf("NumberOfBanks\t\t: %d\n", modeinfo->NumberOfBanks);
	printf("MemoryModel\t\t: %d\n", modeinfo->MemoryModel);
	printf("BankSize\t\t: %d\n", modeinfo->BankSize);
	printf("NumberOfImagePages\t: %d\n", modeinfo->NumberOfImagePages);
	printf("RedMaskSize\t\t: %d\n", modeinfo->RedMaskSize);
	printf("RedFieldPosition\t: %d\n", modeinfo->RedFieldPosition);
	printf("GreenMaskSize\t\t: %d\n", modeinfo->GreenMaskSize);
	printf("GreenFieldPosition\t: %d\n", modeinfo->GreenFieldPosition);
	printf("BlueMaskSize\t\t: %d\n", modeinfo->BlueMaskSize);
	printf("BlueFieldPosition\t: %d\n", modeinfo->BlueFieldPosition);
	printf("RsvdMaskSize\t\t: %d\n", modeinfo->RsvdMaskSize);
	printf("RsvdFieldPosition\t: %d\n", modeinfo->RsvdFieldPosition);
	printf("DirectColorModeInfo\t: %d\n", modeinfo->DirectColorModeInfo);
	printf("%s.%d\t vesa_PrintVBEModeInfo() End of VESA mode information\n", __FILE__, __LINE__);
	printf("----------\n");
}