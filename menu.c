/*
Copyright 2005, 2006, 2007 Dennis van Weeren
Copyright 2008, 2009 Jakub Bednarski

This file is part of Minimig

Minimig is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.

Minimig is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// 2009-11-14   - OSD labels changed
// 2009-12-15   - added display of directory name extensions
// 2010-01-09   - support for variable number of tracks
// 2016-06-01   - improvements to 8-bit menu

//#include "stdbool.h"
#include <stdlib.h>
#include <inttypes.h>
#include <ctype.h>
#include <fcntl.h>
#include "stdio.h"
#include "string.h"
#include "errors.h"
#include "file_io.h"
#include "osd.h"
#include "state.h"
#include "fdd.h"
#include "hdd.h"
#include "hardware.h"
#include "config.h"
#include "menu.h"
#include "user_io.h"
#include "tos.h"
#include "debug.h"
#include "boot.h"
#include "archie.h"
#include "fpga_io.h"
#include <stdbool.h>
#include "mist_cfg.h"
#include "input.h"

// test features (not used right now)
// #define ALLOW_TEST_MENU 0 //remove to disable in prod version


// central MiST joystick status

static mist_joystick_t mist_joy[3] = { // 3rd one is dummy, used to store defaults
	{
		.vid = 0,
		.pid = 0,
		.num_buttons = 1, // DB9 has 1 button
		.state = 0,
		.state_extra = 0,
		.usb_state = 0,
		.usb_state_extra = 0,
		.turbo = 0,
		.turbo_counter = 0,
		.turbo_mask = 0x30,	 // A and B buttons		
		.turbo_state = 0xFF  // flip state (0 or 1)
	},
	{
		.vid = 0,
		.pid = 0,
		.num_buttons = 1, // DB9 has 1 button
		.state = 0,
		.state_extra = 0,
		.usb_state = 0,
		.usb_state_extra = 0,
		.turbo = 0,
		.turbo_counter = 0,
		.turbo_mask = 0x30, // A and B buttons		
		.turbo_state = 0xFF // flip state (0 or 1)
	},
	{
		.vid = 0,
		.pid = 0,
		.num_buttons = 1, // DB9 has 1 button
		.state = 0,
		.state_extra = 0,
		.usb_state = 0,
		.usb_state_extra = 0,
		.turbo = 0,
		.turbo_counter = 0,
		.turbo_mask = 0x30, // A and B buttons		
		.turbo_state = 0xFF // flip state (0 or 1)
	}
};
// state of MIST virtual joystick


void NumJoysticksSet(unsigned char num) {
	mist_joystick_t joy;
	StateNumJoysticksSet(num);
	if (num<3) {
		//clear USB joysticks
		if (num<2)
			joy = mist_joy[0];
		else
			joy = mist_joy[1];

		joy.vid = 0;
		joy.vid = 0;
		joy.num_buttons = 1;
		joy.state = 0;
		joy.state_extra = 0;
		joy.usb_state = 0;
		joy.usb_state_extra = 0;
	}
}

// other constants
#define DIRSIZE 8 // number of items in directory display window

unsigned char menustate = MENU_NONE1;
unsigned char parentstate;
unsigned char menusub = 0;
unsigned char menusub_last = 0; //for when we allocate it dynamically and need to know last row
unsigned int menumask = 0; // Used to determine which rows are selectable...
unsigned long menu_timer = 0;

extern unsigned char drives;
extern adfTYPE df[4];

extern configTYPE config;

extern const char version[];
const char *config_tos_mem[] = { "512 kB", "1 MB", "2 MB", "4 MB", "8 MB", "14 MB", "--", "--" };
const char *config_tos_wrprot[] = { "none", "A:", "B:", "A: and B:" };
const char *config_tos_usb[] = { "none", "control", "debug", "serial", "parallel", "midi" };

const char *config_filter_msg[] = { "none", "HORIZONTAL", "VERTICAL", "H+V" };
const char *config_memory_chip_msg[] = { "0.5 MB", "1.0 MB", "1.5 MB", "2.0 MB" };
const char *config_memory_slow_msg[] = { "none  ", "0.5 MB", "1.0 MB", "1.5 MB" };
const char *config_scanlines_msg[] = { "off", "dim", "black" };
const char *config_ar_msg[] = { "4:3", "16:9" };
const char *config_blank_msg[] = { "Blank", "Blank+" };
const char *config_dither_msg[] = { "off", "SPT", "RND", "S+R" };
const char *config_memory_fast_msg[] = { "none  ", "2.0 MB", "4.0 MB","24.0 MB","24.0 MB" };
const char *config_cpu_msg[] = { "68000 ", "68010", "-----","68020" };
const char *config_hdf_msg[] = { "Disabled", "Hardfile (disk img)", "MMC/SD card", "MMC/SD partition 1", "MMC/SD partition 2", "MMC/SD partition 3", "MMC/SD partition 4" };
const char *config_chipset_msg[] = { "OCS-A500", "OCS-A1000", "ECS", "---", "---", "---", "AGA", "---" };
const char *config_turbo_msg[] = { "none", "CHIPRAM", "KICK", "BOTH" };
char *config_autofire_msg[] = { "        AUTOFIRE OFF", "        AUTOFIRE FAST", "        AUTOFIRE MEDIUM", "        AUTOFIRE SLOW" };
const char *config_cd32pad_msg[] = { "OFF", "ON" };
char *config_button_turbo_msg[] = { "OFF", "FAST", "MEDIUM", "SLOW" };
char *config_button_turbo_choice_msg[] = { "A only", "B only", "A & B" };
char *joy_button_map[] = { "RIGHT", "LEFT", "DOWN", "UP", "BUTTON 1", "BUTTON 2", "BUTTON 3", "BUTTON 4", "BUTTON OSD" };


enum HelpText_Message { HELPTEXT_NONE, HELPTEXT_MAIN, HELPTEXT_HARDFILE, HELPTEXT_CHIPSET, HELPTEXT_MEMORY, HELPTEXT_VIDEO };
const char *helptexts[] = {
	0,
	"                                Welcome to MiSTer!  Use the cursor keys to navigate the menus.  Use space bar or enter to select an item.  Press Esc or F12 to exit the menus.  Joystick emulation on the numeric keypad can be toggled with the numlock or scrlock key, while pressing Ctrl-Alt-0 (numeric keypad) toggles autofire mode.",
	"                                Minimig can emulate an A600/A1200 IDE harddisk interface.  The emulation can make use of Minimig-style hardfiles (complete disk images) or UAE-style hardfiles (filesystem images with no partition table).",
	"                                Minimig's processor core can emulate a 68000 or 68020 processor (though the 68020 mode is still experimental.)  If you're running software built for 68000, there's no advantage to using the 68020 mode, since the 68000 emulation runs just as fast.",
	"                                Minimig can make use of up to 2 megabytes of Chip RAM, up to 1.5 megabytes of Slow RAM (A500 Trapdoor RAM), and up to 24 megabytes of true Fast RAM.  To use the HRTmon feature you will need a file on the SD card named hrtmon.rom. HRTMon is not compatible with Fast RAM.",
	"                                Minimig's video features include a blur filter, to simulate the poorer picture quality on older monitors, and also scanline generation to simulate the appearance of a screen with low vertical resolution.",
	0
};

// one screen width
const char* HELPTEXT_SPACER = "                                ";
char helptext_custom[320];

const char* scanlines[] = { "Off","25%","50%","75%" };
const char* stereo[] = { "Mono","Stereo" };
const char* atari_chipset[] = { "ST","STE","MegaSTE","STEroids" };

unsigned char config_autofire = 0;

// file selection menu variables
char fs_pFileExt[13] = "xxx";
unsigned char fs_ExtLen = 0;
unsigned char fs_Options;
unsigned char fs_MenuSelect;
unsigned char fs_MenuCancel;

char* GetExt(char *ext) {
	static char extlist[32];
	char *p = extlist;

	while (*ext) {
		strcpy(p, ",");
		strncat(p, ext, 3);
		if (strlen(ext) <= 3) break;
		ext += 3;
		p += strlen(p);
	}

	return extlist + 1;
}

char SelectedPath[1024] = { 0 };
int changeDir(char *dir)
{
	char curdir[128];
	memset(curdir, 0, sizeof(curdir));
	if(!dir || !strcmp(dir, ".."))
	{
		if (!strlen(SelectedPath))
		{
			return 0;
		}

		char *p = strrchr(SelectedPath, '/');
		if (p)
		{
			*p = 0;
			int len = strlen(p+1);
			if (len > sizeof(curdir) - 1) len = sizeof(curdir) - 1;
			strncpy(curdir, p+1, len);
		}
		else
		{
			int len = strlen(SelectedPath);
			if (len > sizeof(curdir) - 1) len = sizeof(curdir) - 1;
			strncpy(curdir, SelectedPath, len);
			SelectedPath[0] = 0;
		}
	}
	else
	{
		if (strlen(SelectedPath) + strlen(dir) > sizeof(SelectedPath) - 100)
		{
			return 0;
		}

		if (strlen(SelectedPath)) strcat(SelectedPath, "/");
		strcat(SelectedPath, dir);
	}

	ScanDirectory(SelectedPath, SCAN_INIT, fs_pFileExt, fs_Options);
	if(curdir[0])
	{
		ScanDirectory(SelectedPath, SCAN_SET_ITEM, curdir, fs_Options);
	}
	return 1;
}

static void SelectFile(char* pFileExt, unsigned char Options, unsigned char MenuSelect, unsigned char MenuCancel, char chdir)
{
	// this function displays file selection menu

	iprintf("%s - %s\n", pFileExt, fs_pFileExt);
	AdjustDirectory(SelectedPath);

	if (strncmp(pFileExt, fs_pFileExt, 12) != 0) // check desired file extension
	{ // if different from the current one go to the root directory and init entry buffer
		SelectedPath[0] = 0;

		if(((user_io_core_type() == CORE_TYPE_8BIT) || (user_io_core_type() == CORE_TYPE_MINIMIG2)) && chdir)
		{
			strcpy(SelectedPath, (user_io_core_type() == CORE_TYPE_MINIMIG2) ? "Amiga" : user_io_get_core_name());
			ScanDirectory(SelectedPath, SCAN_INIT, pFileExt, Options);
			if (!nDirEntries)
			{
				SelectedPath[0] = 0;
				ScanDirectory(SelectedPath, SCAN_INIT, pFileExt, Options);
			}
		}
		else
		{
			ScanDirectory(SelectedPath, SCAN_INIT, pFileExt, Options);
		}
	}

	iprintf("pFileExt = %3s\n", pFileExt);
	strcpy(fs_pFileExt, pFileExt);
	fs_ExtLen = strlen(fs_pFileExt);
	//  fs_pFileExt = pFileExt;
	fs_Options = Options;
	fs_MenuSelect = MenuSelect;
	fs_MenuCancel = MenuCancel;

	menustate = MENU_FILE_SELECT1;
}


static void substrcpy(char *d, char *s, char idx) {
	char p = 0;

	while (*s) {
		if ((p == idx) && *s && (*s != ','))
			*d++ = *s;

		if (*s == ',')
			p++;

		s++;
	}

	*d = 0;
}

#define STD_EXIT       "            exit"
#define STD_SPACE_EXIT "        SPACE to exit"
#define STD_COMBO_EXIT "      Ctrl+ESC to exit"

#define HELPTEXT_DELAY 10000
#define FRAME_DELAY 150

// prints input as a string of binary (on/off) values
// assumes big endian, returns using special characters (checked box/unchecked box)
void siprintbinary(char* buffer, size_t const size, void const * const ptr)
{
	unsigned char *b = (unsigned char*)ptr;
	unsigned char byte;
	int i, j;
	memset(buffer, '\0', sizeof(buffer));
	for (i = size - 1; i >= 0; i--)
	{
		for (j = 0; j<8; j++)
		{
			byte = (b[i] >> j) & 1;
			buffer[j] = byte ? '\x1a' : '\x19';
		}
	}
	return;
}

void get_joystick_state(char *joy_string, char *joy_string2, uint8_t joy_num) {
	// helper to get joystick status (both USB or DB9)
	uint16_t vjoy;
	memset(joy_string, '\0', sizeof(joy_string));
	memset(joy_string2, '\0', sizeof(joy_string2));
	vjoy = StateJoyGet(joy_num);
	vjoy |= StateJoyGetExtra(joy_num) << 8;
	if (vjoy == 0) {
		strcpy(joy_string2, "                             ");
		memset(joy_string2, ' ', 8);
		memset(joy_string2 + 8, '\x14', 1);
		memset(joy_string2 + 9, ' ', 1);
		strcat(joy_string2, "\0");
		return;
	}
	strcpy(joy_string, "        \x12   X Y L R L2 R2 L3");
	strcpy(joy_string2, "      < \x13 > A B Sel Sta R3");
	if (!(vjoy & JOY_UP)) memset(joy_string + 8, ' ', 1);
	if (!(vjoy & JOY_X))  memset(joy_string + 12, ' ', 1);
	if (!(vjoy & JOY_Y))  memset(joy_string + 14, ' ', 1);
	if (!(vjoy & JOY_L))  memset(joy_string + 16, ' ', 1);
	if (!(vjoy & JOY_R))  memset(joy_string + 18, ' ', 1);
	if (!(vjoy & JOY_L2))  memset(joy_string + 20, ' ', 2);
	if (!(vjoy & JOY_R2))  memset(joy_string + 23, ' ', 2);
	if (!(vjoy & JOY_L3))  memset(joy_string + 26, ' ', 2);
	if (!(vjoy & JOY_LEFT)) 	memset(joy_string2 + 6, ' ', 1);
	if (!(vjoy & JOY_DOWN))  memset(joy_string2 + 8, '\x14', 1);
	if (!(vjoy & JOY_RIGHT)) memset(joy_string2 + 10, ' ', 1);
	if (!(vjoy & JOY_A))  		memset(joy_string2 + 12, ' ', 1);
	if (!(vjoy & JOY_B))  		memset(joy_string2 + 14, ' ', 1);
	if (!(vjoy & JOY_SELECT))memset(joy_string2 + 16, ' ', 3);
	if (!(vjoy & JOY_START)) memset(joy_string2 + 20, ' ', 3);
	if (!(vjoy & JOY_R3))  	memset(joy_string2 + 24, ' ', 2);
	return;
}

void get_joystick_state_usb(char *s, unsigned char joy_num) {
	/* USB specific - current "raw" state
	(in reverse binary format to correspont to MIST.INI mapping entries)
	*/
	char buffer[5];
	unsigned short i;
	char binary_string[9] = "00000000";
	unsigned char joy = 0;
	unsigned int max_btn = 1;
	if (StateNumJoysticks() == 0 || (joy_num == 1 && StateNumJoysticks()<2))
	{
		strcpy(s, " ");
		return;
	}
	max_btn = StateUsbGetNumButtons(joy_num);
	joy = StateUsbJoyGet(joy_num);
	siprintf(s, "  USB: ---- 0000 0000 0000");
	siprintbinary(binary_string, sizeof(joy), &joy);
	s[7] = binary_string[0] == '\x1a' ? '>' : '\x1b';
	s[8] = binary_string[1] == '\x1a' ? '<' : '\x1b';
	s[9] = binary_string[2] == '\x1a' ? '\x13' : '\x1b';
	s[10] = binary_string[3] == '\x1a' ? '\x12' : '\x1b';
	s[12] = binary_string[4];
	s[13] = max_btn>1 ? binary_string[5] : ' ';
	s[14] = max_btn>2 ? binary_string[6] : ' ';
	s[15] = max_btn>3 ? binary_string[7] : ' ';
	joy = StateUsbJoyGetExtra(joy_num);
	siprintbinary(binary_string, sizeof(joy), &joy);
	s[17] = max_btn>4 ? binary_string[0] : ' ';
	s[18] = max_btn>5 ? binary_string[1] : ' ';
	s[19] = max_btn>6 ? binary_string[2] : ' ';
	s[20] = max_btn>7 ? binary_string[3] : ' ';
	s[22] = max_btn>8 ? binary_string[4] : ' ';
	s[23] = max_btn>9 ? binary_string[5] : ' ';
	s[24] = max_btn>10 ? binary_string[6] : ' ';
	s[25] = max_btn>11 ? binary_string[7] : ' ';
	return;
}

void append_joystick_usbid(char *usb_id, unsigned int usb_vid, unsigned int usb_pid) {
	siprintf(usb_id, "VID:%04X PID:%04X", usb_vid, usb_pid);
}

void get_joystick_id(char *usb_id, unsigned char joy_num, short raw_id) {
	/*
	Builds a string containing the USB VID/PID information of a joystick
	*/
	char buffer[32] = "";
	mist_joystick_t joystick;
	if (joy_num>3)
		joystick = mist_joy[2];
	else
		joystick = mist_joy[joy_num];

	if (raw_id == 0) {
		if (StateNumJoysticks() == 0 || (joy_num == 1 && StateNumJoysticks()<2))
		{
			strcpy(usb_id, "      ");
			strcat(usb_id, "Atari DB9 Joystick");
			return;
		}
	}

	//hack populate from outside
	joystick.vid = StateUsbVidGet(joy_num);
	joystick.pid = StateUsbPidGet(joy_num);

	memset(usb_id, '\0', sizeof(usb_id));
	if (joystick.vid>0) {
		/*
		if (raw_id == 0) {
		strcpy(buffer, get_joystick_alias( joystick.vid, joystick.pid ));
		}
		*/
		if (strlen(buffer) == 0) {
			append_joystick_usbid(buffer, joystick.vid, joystick.pid);
		}
	}
	else {
		strcpy(buffer, "Atari DB9 Joystick");
	}
	if (raw_id == 0)
		siprintf(usb_id, "%*s", (28 - strlen(buffer)) / 2, " ");
	else
		strcpy(usb_id, "");
	strcat(usb_id, buffer);
	return;
}


unsigned char getIdx(char *opt) {
	if ((opt[1] >= '0') && (opt[1] <= '9')) return opt[1] - '0';
	if ((opt[1] >= 'A') && (opt[1] <= 'V')) return opt[1] - 'A' + 10;
	return 0; // basically 0 cannot be valid because used as a reset. Thus can be used as a error.
}

unsigned long getStatus(char *opt, unsigned long status) {
	char idx1 = getIdx(opt);
	char idx2 = getIdx(opt + 1);
	unsigned long x = (status & (1 << idx1)) ? 1 : 0;

	if (idx2>idx1) {
		x = status >> idx1;
		x = x & ~(0xffffffff << (idx2 - idx1 + 1));
	}

	return x;
}

unsigned long setStatus(char *opt, unsigned long status, unsigned long value) {
	unsigned char idx1 = getIdx(opt);
	unsigned char idx2 = getIdx(opt + 1);
	unsigned long x = 1;

	if (idx2>idx1) x = ~(0xffffffff << (idx2 - idx1 + 1));
	x = x << idx1;

	return (status & ~x) | ((value << idx1) & x);
}

unsigned long getStatusMask(char *opt) {
	char idx1 = getIdx(opt);
	char idx2 = getIdx(opt + 1);
	unsigned long x = 1;

	if (idx2>idx1) x = ~(0xffffffff << (idx2 - idx1 + 1));

	//iprintf("grtStatusMask %d %d %x\n", idx1, idx2, x);

	return x << idx1;
}

char* get_keycode_table()
{
	switch (user_io_core_type())
	{
	case CORE_TYPE_MINIMIG2:
		return "Amiga";

	case CORE_TYPE_MIST:
		return "  ST";

	case CORE_TYPE_ARCHIE:
		return "Archie";
	}

	return   " PS/2";
}

void HandleUI(void)
{
	char *p;
	char s[40];
	unsigned char i, c, m, up, down, select, menu, right, left, plus, minus;
	uint8_t mod;
	unsigned long len;
	static hardfileTYPE t_hardfile[2]; // temporary copy of former hardfile configuration
	static unsigned char ctrl = false;
	static unsigned char lalt = false;
	char enable;
	static long helptext_timer;
	static const char *helptext;
	static char helpstate = 0;
	uint8_t keys[6] = { 0,0,0,0,0,0 };
	uint16_t keys_ps2[6] = { 0,0,0,0,0,0 };

	mist_joystick_t joy0, joy1;

	/* check joystick status */
	char joy_string[32];
	char joy_string2[32];
	char usb_id[64];

	// update turbo status for joysticks
	//StateTurboUpdate(0);
	//StateTurboUpdate(1);

	// get user control codes
	c = OsdGetCtrl();

	// decode and set events
	menu = false;
	select = false;
	up = false;
	down = false;
	left = false;
	right = false;
	plus = false;
	minus = false;

	switch (c)
	{
	case KEY_CTRL:
		ctrl = true;
		break;
	case KEY_CTRL | KEY_UPSTROKE:
		ctrl = false;
		break;
	case KEY_LALT:
		lalt = true;
		break;
	case KEY_LALT | KEY_UPSTROKE:
		lalt = false;
		break;
	case KEY_KP0:
		if (ctrl && lalt)
		{
			if (menustate == MENU_NONE2 || menustate == MENU_INFO)
			{
				config_autofire++;
				config_autofire &= 3;
				ConfigAutofire(config_autofire);
				if (menustate == MENU_NONE2 || menustate == MENU_INFO)
					InfoMessage(config_autofire_msg[config_autofire]);
			}
		}
		break;

	case KEY_MENU:
		menu = true;
		OsdKeySet(KEY_MENU | KEY_UPSTROKE);
		break;

		// Within the menu the esc key acts as the menu key. problem:
		// if the menu is left with a press of ESC, then the follwing
		// break code for the ESC key when the key is released will 
		// reach the core which never saw the make code. Simple solution:
		// react on break code instead of make code
	case KEY_ESC | KEY_UPSTROKE:
		if (menustate != MENU_NONE2)
			menu = true;
		break;
	case KEY_ENTER:
	case KEY_SPACE:
		select = true;
		break;
	case KEY_UP:
		up = true;
		break;
	case KEY_DOWN:
		down = true;
		break;
	case KEY_LEFT:
		left = true;
		break;
	case KEY_RIGHT:
		right = true;
		break;
	case KEY_KPPLUS:
	case 0x0c: // =/+
		plus = true;
		break;
	case KEY_KPMINUS:
	case 0x0b: // -/_
		minus = true;
		break;

	case 0x01: // 1: 1280x720 mode
		if (user_io_osd_is_visible) mist_cfg.video_mode = 0;
		break;

	case 0x02: // 2: 1280x1024 mode
		if (user_io_osd_is_visible) mist_cfg.video_mode = 1;
		break;
	}

	if (menu || select || up || down || left || right)
	{
		if (helpstate)
			OsdWrite(OsdGetSize()-1, STD_EXIT, (menumask - ((1 << (menusub + 1)) - 1)) <= 0, 0); // Redraw the Exit line...
		helpstate = 0;
		helptext_timer = GetTimer(HELPTEXT_DELAY);
	}

	if (helptext)
	{
		if (helpstate<9)
		{
			if (CheckTimer(helptext_timer))
			{
				helptext_timer = GetTimer(FRAME_DELAY);
				OsdWriteOffset(OsdGetSize() - 1, STD_EXIT, 0, 0, helpstate, 0);
				++helpstate;
			}
		}
		else if (helpstate == 9)
		{
			ScrollReset();
			++helpstate;
		}
		else
			ScrollText(OsdGetSize()-1, helptext, 0, 0, 0, 0);
	}

	// Standardised menu up/down.
	// The screen should set menumask, bit 0 to make the top line selectable, bit 1 for the 2nd line, etc.
	// (Lines in this context don't have to correspond to rows on the OSD.)
	// Also set parentstate to the appropriate menustate.
	if (menumask)
	{
		if (down && (menumask >= (1 << (menusub + 1))))	// Any active entries left?
		{
			do
				menusub++;
			while ((menumask & (1 << menusub)) == 0);
			menustate = parentstate;
		}

		if (up && menusub > 0 && (menumask << (OsdGetSize() - menusub)))
		{
			do
				--menusub;
			while ((menumask & (1 << menusub)) == 0);
			menustate = parentstate;
		}
	}


	// Switch to current menu screen
	switch (menustate)
	{
		/******************************************************************/
		/* no menu selected                                               */
		/******************************************************************/
	case MENU_NONE1:
		helptext = helptexts[HELPTEXT_NONE];
		menumask = 0;
		OsdDisable();
		menustate = MENU_NONE2;
		OsdSetSize(8);
		break;

	case MENU_NONE2:
		if (menu)
		{
			if (StateKeyboardModifiers() & 0x44) //Alt+Menu
			{
				OsdSetSize(16);
				SelectFile("RBF", 0, MENU_FIRMWARE_CORE_FILE_SELECTED, MENU_NONE1, 0);
			}
			else if (user_io_core_type() == CORE_TYPE_MINIMIG2)
				menustate = MENU_MAIN1;
			else if (user_io_core_type() == CORE_TYPE_MIST)
				menustate = MENU_MIST_MAIN1;
			else if (user_io_core_type() == CORE_TYPE_ARCHIE)
				menustate = MENU_ARCHIE_MAIN1;
			else {
				// the "menu" core is special in jumps directly to the core selection menu
				if (is_menu_core())
				{
					OsdSetSize(16);
					OsdCoreNameSet("");
					SelectFile("RBF", 0, MENU_FIRMWARE_CORE_FILE_SELECTED, MENU_FIRMWARE1, 0);
				}
				else
				{
					menustate = MENU_8BIT_MAIN1;
				}
			}
			menusub = 0;
			OsdClear();
			OsdEnable(DISABLE_KEYBOARD);
		}
		break;

		/******************************************************************/
		/* archimedes main menu                                           */
		/******************************************************************/

	case MENU_ARCHIE_MAIN1: {
		OsdSetSize(8);
		menumask = 0x3f;
		OsdSetTitle("ARCHIE", 0);

		strcpy(s, " Floppy 0: ");
		strcat(s, archie_get_floppy_name(0));
		OsdWrite(0, s, menusub == 0, 0);

		strcpy(s, " Floppy 1: ");
		strcat(s, archie_get_floppy_name(1));
		OsdWrite(1, s, menusub == 1, 0);

		strcpy(s, " OS ROM: ");
		strcat(s, archie_get_rom_name());
		OsdWrite(2, s, menusub == 2, 0);

		OsdWrite(3, "", 0, 0);

		// the following is exactly like the atatri st core
		OsdWrite(4, " Firmware & Core           \x16", menusub == 3, 0);
		OsdWrite(5, " Save config                ", menusub == 4, 0);
		OsdWrite(6, "", 0, 0);
		OsdWrite(7, STD_EXIT, menusub == 5, 0);
		menustate = MENU_ARCHIE_MAIN2;
		parentstate = MENU_ARCHIE_MAIN1;
	} break;

	case MENU_ARCHIE_MAIN2:
		// menu key closes menu
		if (menu)
			menustate = MENU_NONE1;
		if (select) {
			switch (menusub) {
			case 0:  // Floppy 0
			case 1:  // Floppy 1
				if (archie_floppy_is_inserted(menusub)) {
					archie_set_floppy(menusub, NULL);
					menustate = MENU_ARCHIE_MAIN1;
				}
				else
					SelectFile("ADF", SCAN_DIR, MENU_ARCHIE_MAIN_FILE_SELECTED, MENU_ARCHIE_MAIN1, 1);
				break;

			case 2:  // Load ROM
				SelectFile("ROM", 0, MENU_ARCHIE_MAIN_FILE_SELECTED, MENU_ARCHIE_MAIN1, 1);
				break;

			case 3:  // Firmware submenu
				menustate = MENU_FIRMWARE1;
				menusub = 1;
				break;

			case 4:  // Save config
				menustate = MENU_NONE1;
				archie_save_config();
				break;

			case 5:  // Exit
				menustate = MENU_NONE1;
				break;
			}
		}
		break;

	case MENU_ARCHIE_MAIN_FILE_SELECTED: // file successfully selected
		if (menusub == 0) archie_set_floppy(0, SelectedPath);
		if (menusub == 1) archie_set_floppy(1, SelectedPath);
		if (menusub == 2) archie_set_rom(SelectedPath);
		menustate = MENU_ARCHIE_MAIN1;
		break;

		/******************************************************************/
		/* 8 bit main menu                                                */
		/******************************************************************/

	case MENU_8BIT_MAIN1: {
		int entry;
		int selentry;

		int old_osd_size = OsdGetSize();
		while (1)
		{
			selentry = 0;
			entry = 0;
			menumask = 0;
			p = user_io_get_core_name();
			if (!p[0]) OsdSetTitle("8BIT", OSD_ARROW_RIGHT);
			else      OsdSetTitle(p, OSD_ARROW_RIGHT);

			if (!p[0]) OsdCoreNameSet("8BIT");
			else      OsdCoreNameSet(p);

			// check if there's a file type supported
			p = user_io_8bit_get_string(1);
			if (p && strlen(p)) {
				entry++;
				selentry++;
				menumask = 1;
				strcpy(s, " Load *.");
				strcat(s, GetExt(p));
				OsdWrite(0, s, menusub == 0, 0);
			}

			// add options as requested by core
			i = 2;
			do {
				char* pos;
				unsigned long status = user_io_8bit_set_status(0, 0);  // 0,0 gets status

				p = user_io_8bit_get_string(i);
				//	  iprintf("Option %d: %s\n", i-1, p);

				// check for 'F'ile or 'S'D image strings
				if (p && ((p[0] == 'F') || (p[0] == 'S'))) {
					substrcpy(s, p, 2);
					if (strlen(s)) {
						strcpy(s, " ");
						substrcpy(s + 1, p, 2);
						strcat(s, " *.");
					}
					else {
						if (p[0] == 'F') strcpy(s, " Load *.");
						else            strcpy(s, " Mount *.");
					}
					pos = s + strlen(s);
					substrcpy(pos, p, 1);
					strcpy(pos, GetExt(pos));
					OsdWrite(entry, s, menusub == selentry, 0);

					// add bit in menu mask
					menumask = (menumask << 1) | 1;
					entry++;
					selentry++;
				}

				// check for 'T'oggle strings
				if (p && (p[0] == 'T')) {

					s[0] = ' ';
					substrcpy(s + 1, p, 1);
					OsdWrite(entry, s, menusub == selentry, 0);

					// add bit in menu mask
					menumask = (menumask << 1) | 1;
					entry++;
					selentry++;
				}

				// check for 'O'ption strings
				if (p && (p[0] == 'O')) {
					unsigned long x = getStatus(p, status);

					// get currently active option
					substrcpy(s, p, 2 + x);
					char l = strlen(s);
					if (!l) {
						// option's index is outside of available values.
						// reset to 0.
						x = 0;
						user_io_8bit_set_status(setStatus(p, status, x), 0xffffffff);
						substrcpy(s, p, 2 + x);
						l = strlen(s);
					}

					s[0] = ' ';
					substrcpy(s + 1, p, 1);
					strcat(s, ":");
					l = 28 - l - strlen(s);
					while (l--) strcat(s, " ");

					substrcpy(s + strlen(s), p, 2 + x);

					OsdWrite(entry, s, menusub == selentry, 0);

					// add bit in menu mask
					menumask = (menumask << 1) | 1;
					entry++;
					selentry++;
				}

				// delimiter
				if (p && (p[0] == '-'))
				{
					OsdWrite(entry, "", 0, 0);
					entry++;
				}

				// check for 'V'ersion strings
				if (p && (p[0] == 'V')) {

					// p[1] is not used but kept for future use
					char x = p[1];

					// get version string
					strcpy(s, OsdCoreName());
					strcat(s, " ");
					substrcpy(s + strlen(s), p, 1);
					OsdCoreNameSet(s);
				}
				i++;
			} while (p);

			OsdSetSize(entry > 7 ? 16 : 8);
			if (old_osd_size == OsdGetSize()) break;
			old_osd_size = OsdGetSize();
		}

		// exit row
		OsdWrite(OsdGetSize() - 1, STD_EXIT, menusub == selentry, 0);
		menusub_last = selentry;
		menumask = (menumask << 1) | 1;

		for (; entry<OsdGetSize() - 1; entry++) OsdWrite(entry, "", 0, 0);

		menustate = MENU_8BIT_MAIN2;
		parentstate = MENU_8BIT_MAIN1;

		// set helptext with core display on top of basic info
		siprintf(helptext_custom, HELPTEXT_SPACER);
		strcat(helptext_custom, OsdCoreName());
		strcat(helptext_custom, helptexts[HELPTEXT_MAIN]);
		helptext = helptext_custom;

	} break; // end MENU_8BIT_MAIN1

	case MENU_8BIT_MAIN2:
		// menu key closes menu
		if (menu)
		{
			menustate = MENU_NONE1;
		}
		if (select)
		{
			if (menusub == menusub_last)
			{
				menustate = MENU_NONE1;
			}
			else
			{
				char fs_present;
				p = user_io_8bit_get_string(1);
				fs_present = p && strlen(p);

				int entry = 0;
				int i = 1;
				while (1)
				{
					p = user_io_8bit_get_string(i++);
					if (!p || p[0] == '-') continue;
					if (entry == menusub) break;
					entry++;
				}

				// entry 0 = file selector
				if(!menusub && fs_present)
				{
					// use a local copy of "p" since SelectFile will destroy the buffer behind it
					static char ext[13];
					strncpy(ext, p, 13);
					while (strlen(ext) < 3) strcat(ext, " ");
					SelectFile(ext, SCAN_DIR, MENU_8BIT_MAIN_FILE_SELECTED, MENU_8BIT_MAIN1, 1);
				}
				else if ((p[0] == 'F') || (p[0] == 'S'))
				{
					static char ext[13];
					substrcpy(ext, p, 1);
					while (strlen(ext) < 3) strcat(ext, " ");
					SelectFile(ext, SCAN_DIR,
						(p[0] == 'F') ? MENU_8BIT_MAIN_FILE_SELECTED : MENU_8BIT_MAIN_IMAGE_SELECTED,
						MENU_8BIT_MAIN1, 1);
				}
				else if (p[0] == 'O')
				{
					unsigned long status = user_io_8bit_set_status(0, 0);  // 0,0 gets status
					unsigned long x = getStatus(p, status) + 1;

					// check if next value available
					substrcpy(s, p, 2 + x);
					if (!strlen(s)) x = 0;

					user_io_8bit_set_status(setStatus(p, status, x), 0xffffffff);

					menustate = MENU_8BIT_MAIN1;
				}
				else if (p[0] == 'T')
				{
					// determine which status bit is affected
					unsigned long mask = 1 << getIdx(p);
					unsigned long status = user_io_8bit_set_status(0, 0);

					user_io_8bit_set_status(status ^ mask, mask);
					user_io_8bit_set_status(status, mask);

					menustate = MENU_8BIT_MAIN1;
				}
			}
		}
		else if (right)
		{
			menustate = MENU_8BIT_SYSTEM1;
			menusub = 0;
		}
		break;

	case MENU_8BIT_MAIN_FILE_SELECTED:
		iprintf("File selected: %s\n", SelectedPath);
		user_io_file_tx(SelectedPath, user_io_ext_idx(SelectedPath, fs_pFileExt) << 6 | (menusub + 1));
		menustate = MENU_NONE1;
		break;

	case MENU_8BIT_MAIN_IMAGE_SELECTED:
		iprintf("Image selected: %s\n", SelectedPath);
		user_io_set_index(user_io_ext_idx(SelectedPath, fs_pFileExt) << 6 | (menusub + 1));
		user_io_file_mount(SelectedPath);
		menustate = MENU_NONE1;
		break;

	case MENU_8BIT_SYSTEM1:
		helptext = helptexts[HELPTEXT_MAIN];
		m = 0;
		if (user_io_core_type() == CORE_TYPE_MINIMIG2) m = 1;
		menumask = m ? 0x3f : 0x7f; // 5 selections + Exit
		OsdSetTitle("System", OSD_ARROW_LEFT);
		menustate = MENU_8BIT_SYSTEM2;
		parentstate = MENU_8BIT_SYSTEM1;
		OsdWrite(0, "", 0, 0);
		OsdWrite(1, " Firmware & Core           \x16", menusub == 0, 0);
		if(OsdIsBig) OsdWrite(2, "", 0, 0);
		OsdWrite(OsdIsBig ? 3 : 2, " Define joystick buttons", menusub == 1, 0);
		OsdWrite(OsdIsBig ? 4 : 3, " Keyboard Test", menusub == 2, 0);
		if (OsdIsBig) OsdWrite(5, "", 0, 0);
		OsdWrite(OsdIsBig ? 6 : 4, m ? " Reset" : " Reset settings", menusub == 3, 0);
		if (m)
			OsdWrite(OsdIsBig ? 7 : 5, "", 0, 0);
		else
			OsdWrite(OsdIsBig ? 7 : 5, " Save settings", menusub == 4, 0); // Minimig saves settings elsewhere
		if (OsdIsBig) OsdWrite(8, "", 0, 0);
		OsdWrite(OsdIsBig ? 9 : 6, " About", menusub == (5 - m), 0);
		if(OsdIsBig) for (int i = 10; i < OsdGetSize() - 1; i++) OsdWrite(i, "", 0, 0);
		OsdWrite(OsdGetSize() - 1, STD_EXIT, menusub == (6 - m), 0);
		break;

	case MENU_8BIT_SYSTEM2:
		m = 0;
		if (user_io_core_type() == CORE_TYPE_MINIMIG2) m = 1;
		// menu key closes menu
		if (menu)
			menustate = MENU_NONE1;
		if (select) {
			switch (menusub) {
			case 0:
				// Firmware submenu
				menustate = MENU_FIRMWARE1;
				menusub = 0;
				break;
			case 1:
				start_map_setting();
				menustate = MENU_JOYDIGMAP;
				menusub = 0;
				break;
			case 2:
				// Keyboard test
				menustate = MENU_8BIT_KEYTEST1;
				menusub = 0;
				break;
			case 3:
				menustate = MENU_RESET1;
				menusub = 1;
				break;
			case 4:
				if (m)
				{
					menustate = MENU_8BIT_ABOUT1;
					menusub = 0;
				}
				else
				{
					// Save settings
					char *filename = user_io_create_config_name();
					unsigned long status = user_io_8bit_set_status(0, 0);
					iprintf("Saving config to %s\n", filename);
					FileSaveConfig(filename, &status, 4);
					menustate = MENU_8BIT_MAIN1;
					menusub = 0;
				}
				break;
			case 5:
				if (m) {
					menustate = MENU_NONE1;
					menusub = 0;
				}
				else {
					// About logo
					menustate = MENU_8BIT_ABOUT1;
					menusub = 0;
				}
				break;
			case 6:
				// Exit
				menustate = MENU_NONE1;
				menusub = 0;
				break;
			}
		}
		else {
			if (left) {
				// go back to core requesting this menu
				switch (user_io_core_type()) {
				case CORE_TYPE_MINIMIG2:
					menusub = 0;
					menustate = MENU_MAIN1;
					break;
				case CORE_TYPE_MIST:
					menusub = 5;
					menustate = MENU_MIST_MAIN1;
					break;
				case CORE_TYPE_ARCHIE:
					menusub = 3;
					menustate = MENU_ARCHIE_MAIN1;
					break;
				case CORE_TYPE_8BIT:
					menusub = 0;
					menustate = MENU_8BIT_MAIN1;
					break;
				}
			}
		}
		break;

	case MENU_JOYDIGMAP:
		helptext = 0;
		menumask = 1;
		OsdSetTitle("Joystick", 0);
		menustate = MENU_JOYDIGMAP1;
		parentstate = MENU_JOYDIGMAP;
		for (int i = 0; i < OsdGetSize() - 1; i++) OsdWrite(i, "", 0, 0);
		OsdWrite(OsdGetSize() - 1, "           cancel", menusub == 0, 0);
		break;

	case MENU_JOYDIGMAP1:
		if(get_map_button()<9) sprintf(s, "       Press: %s", joy_button_map[get_map_button()]);
		OsdWrite(3, s, 0, 0);
		if (get_map_button())
		{
			OsdWrite(OsdGetSize() - 1, "           finish", menusub == 0, 0);
			if (get_map_button()<9) sprintf(s, "   Joystick ID: %04x:%04x", get_map_vid(), get_map_pid());
			OsdWrite(5, s, 0, 0);
		}
		if (select || menu || get_map_button()>=9)
		{
			finish_map_setting();
			menustate = MENU_8BIT_SYSTEM1;
			menusub = 1;
		}
		break;

	case MENU_8BIT_ABOUT1:
		menumask = 0;
		helptext = helptexts[HELPTEXT_NONE];
		OsdSetTitle("About", 0);
		menustate = MENU_8BIT_ABOUT2;
		parentstate = MENU_8BIT_ABOUT1;
		for (int i = 5; i < OsdGetSize() - 1; i++) OsdWrite(i, "", 0, 0);
		OsdDrawLogo(0, 0, 1);
		OsdDrawLogo(1, 1, 1);
		OsdDrawLogo(2, 2, 1);
		OsdDrawLogo(3, 3, 1);
		OsdDrawLogo(4, 4, 1);
		OsdDrawLogo(5, 5, 1);
		OsdWrite(OsdGetSize() - 1, STD_EXIT, menusub == 0, 0);
		StarsInit();
		ScrollReset();
		break;

	case MENU_8BIT_ABOUT2:
		StarsUpdate();
		OsdDrawLogo(0, 0, 1);
		OsdDrawLogo(1, 1, 1);
		OsdDrawLogo(2, 2, 1);
		OsdDrawLogo(3, 3, 1);
		OsdDrawLogo(4, 4, 1);
		OsdDrawLogo(5, 5, 1);
		ScrollText(OsdIsBig ? 13 : 6, "                                 MiST by Till Harbaum, based on Minimig by Dennis van Weeren and other projects. MiST hardware and software is distributed under the terms of the GNU General Public License version 3. MiST FPGA cores are the work of their respective authors under individual licensing.", 0, 0, 0, 0);
		// menu key closes menu
		if (menu) {
			menustate = MENU_8BIT_SYSTEM1;
			menusub = 5-m;
		}
		if (select) {
			//iprintf("Selected", 0);
			if (menusub == 0) {
				menustate = MENU_8BIT_SYSTEM1;
				menusub = 5-m;
			}
		}
		else {
			if (left)
			{
				menustate = MENU_8BIT_SYSTEM1;
				menusub = 5-m;
			}
		}
		break;

	case MENU_8BIT_CONTROLLERS1:
		helptext = helptexts[HELPTEXT_NONE];
		menumask = 0x3f;
		OsdSetTitle("Inputs", 0);
		menustate = MENU_8BIT_CONTROLLERS2;
		parentstate = MENU_8BIT_CONTROLLERS1;
		OsdWrite(0, " Turbo Settings (disabled)  ", menusub == 0, 1);
		//OsdWrite(0, " Turbo Settings            \x16", menusub==0, 0);
		OsdWrite(1, " Joystick 1 Test           \x16", menusub == 1, 0);
		OsdWrite(2, " Joystick 2 Test           \x16", menusub == 2, 0);
		OsdWrite(3, " Keyboard Test             \x16", menusub == 3, 0);
		OsdWrite(4, " USB status                \x16", menusub == 4, 0);
		OsdWrite(5, "", 0, 0);
		//OsdWrite(5, " CHR test                  \x16", menusub==6, 0);
		for (int i = 6; i < OsdGetSize() - 1; i++) OsdWrite(i, "", 0, 0);
		OsdWrite(OsdGetSize() - 1, STD_EXIT, menusub == 5, 0);
		break;

	case MENU_8BIT_CONTROLLERS2:
		// menu key goes back to previous menu
		if (menu) {
			menusub = 1;
			menustate = MENU_8BIT_SYSTEM1;
		}
		if (select) {
			switch (menusub) {
			case 0:
				// Turbo config
				//menustate = MENU_8BIT_TURBO1;
				menusub = 0;
				break;
			case 1:
				// Joystick1 Test
				menustate = MENU_8BIT_JOYTEST_A1;
				menusub = 0;
				break;
			case 2:
				// Joystick2 test
				menustate = MENU_8BIT_JOYTEST_B1;
				menusub = 0;
				break;
			case 3:
				// Keyboard test
				menustate = MENU_8BIT_KEYTEST1;
				menusub = 0;
				break;
			case 4:
				// USB status
				menustate = MENU_8BIT_USB1;
				menusub = 0;
				break;
			case 5:
				// Exit to system menu
				menustate = MENU_8BIT_SYSTEM1;
				menusub = 1;
				break;
				/*case 6:
				// character rom test
				menustate=MENU_8BIT_CHRTEST1;
				menusub = 0;
				break;
				*/
			}
		}
		break;

	case MENU_8BIT_KEYTEST1:
		helptext = helptexts[HELPTEXT_NONE];
		menumask = 1;
		OsdSetTitle("Keyboard", 0);
		menustate = MENU_8BIT_KEYTEST2;
		parentstate = MENU_8BIT_KEYTEST1;
		for (int i = 0; i < OsdGetSize() - 1; i++) OsdWrite(i, "", 0, 0);
		OsdWrite(OsdGetSize() - 1, STD_COMBO_EXIT, menusub == 0, 0);
		break;

	case MENU_8BIT_KEYTEST2:
		m = OsdIsBig ? 4 : 0;
		StateKeyboardPressed(keys);
		OsdWrite(m++, "       USB scancodes", 0, 0);
		siprintf(s, "     %2x   %2x   %2x   %2x", keys[0], keys[1], keys[2], keys[3]); // keys[4], keys[5]);
		OsdWrite(m++, s, 0, 0);
		mod = StateKeyboardModifiers();
		strcpy(usb_id, "                      ");
		siprintbinary(usb_id, sizeof(mod), &mod);
		siprintf(s, "    mod keys - %s ", usb_id);
		OsdWrite(m++, s, 0, 0);
		m++;
		uint16_t keys_ps2[6] = { 0,0,0,0,0,0 };
		StateKeyboardPressedPS2(keys_ps2);
		add_modifiers(mod, keys_ps2);
		siprintf(s, "      %s scancodes", get_keycode_table());
		OsdWrite(m++, s, 0, 0);
		siprintf(s, "   %4x %4x %4x %4x ", keys_ps2[0], keys_ps2[1], keys_ps2[2], keys_ps2[3]);// keys_ps2[4], keys_ps2[5]);
		OsdWrite(m++, s, 0, 0);
		if ((mod & 0x11) && menu) // Ctrl+ESC
		{
			menustate = MENU_8BIT_SYSTEM1;
			menusub = 2;
		}
		break;

	case MENU_8BIT_USB1:
		helptext = helptexts[HELPTEXT_NONE];
		menumask = 1;
		OsdSetTitle("USB", 0);
		menustate = MENU_8BIT_USB2;
		parentstate = MENU_8BIT_USB1;
		strcpy(usb_id, " ");
		get_joystick_id(usb_id, 0, 1);
		siprintf(s, " Joy1 - %s", usb_id);
		OsdWrite(0, "", 0, 0);
		OsdWrite(1, s, 0, 0);
		strcpy(usb_id, " ");
		get_joystick_id(usb_id, 1, 1);
		siprintf(s, " Joy2 - %s", usb_id);
		OsdWrite(2, "", 0, 0);
		OsdWrite(3, s, 0, 0);
		OsdWrite(4, "", 0, 0);
		OsdWrite(5, "", 0, 0);
		OsdWrite(6, " ", 0, 0);
		for (int i = 7; i < OsdGetSize() - 1; i++) OsdWrite(i, "", 0, 0);
		OsdWrite(OsdGetSize() - 1, STD_EXIT, menusub == 0, 0);
		break;

	case MENU_8BIT_USB2:
		menumask = 1;
		OsdSetTitle("USB", 0);
		strcpy(usb_id, " ");
		get_joystick_id(usb_id, 0, 1);
		siprintf(s, " Joy1 - %s", usb_id);
		OsdWrite(0, "", 0, 0);
		OsdWrite(1, s, 0, 0);
		strcpy(usb_id, " ");
		get_joystick_id(usb_id, 1, 1);
		siprintf(s, " Joy2 - %s", usb_id);
		OsdWrite(2, "", 0, 0);
		OsdWrite(3, s, 0, 0);
		OsdWrite(OsdGetSize() - 1, STD_EXIT, menusub == 0, 0);
		// menu key goes back to previous menu
		if (menu) {
			menustate = MENU_8BIT_CONTROLLERS1;
			menusub = 4;
		}
		if (select) {
			if (menusub == 0) {
				menustate = MENU_8BIT_CONTROLLERS1;
				menusub = 4;
			}
		}
		break;

	case MENU_8BIT_JOYTEST_A1:
		helptext = helptexts[HELPTEXT_NONE];
		menumask = 1;
		get_joystick_id(usb_id, 0, 0);
		OsdSetTitle("Joy1", 0);
		menustate = MENU_8BIT_JOYTEST_A2;
		parentstate = MENU_8BIT_JOYTEST_A1;
		OsdWrite(0, "       Test Joystick 1", 0, 0);
		OsdWrite(1, usb_id, 0, 0);
		OsdWrite(2, "", 0, 0);
		OsdWrite(3, "", 0, 0);
		OsdWrite(4, "", 0, 0);
		OsdWrite(5, "", 0, 0);
		OsdWrite(6, " ", 0, 0);
		for (int i = 7; i < OsdGetSize() - 1; i++) OsdWrite(i, "", 0, 0);
		OsdWrite(OsdGetSize() - 1, STD_SPACE_EXIT, menusub == 0, 0);
		break;

	case MENU_8BIT_JOYTEST_A2:
		get_joystick_state(joy_string, joy_string2, 0); //grab state of joy 0
		get_joystick_id(usb_id, 0, 0);
		OsdWrite(1, usb_id, 0, 0);
		OsdWrite(3, joy_string, 0, 0);
		OsdWrite(4, joy_string2, 0, 0);
		OsdWrite(5, " ", 0, 0);
		// display raw USB input
		get_joystick_state_usb(s, 0);
		OsdWrite(6, s, 0, 0);
		// allow allow exit when hitting space
		if (c == KEY_SPACE) {
			menustate = MENU_8BIT_CONTROLLERS1;
			menusub = 1;
		}
		break;

	case MENU_8BIT_JOYTEST_B1:
		helptext = helptexts[HELPTEXT_NONE];
		menumask = 1;
		get_joystick_id(usb_id, 1, 0);
		OsdSetTitle("Joy2", 0);
		menustate = MENU_8BIT_JOYTEST_B2;
		parentstate = MENU_8BIT_JOYTEST_B1;
		OsdWrite(0, "       Test Joystick 2", 0, 0);
		OsdWrite(1, usb_id, 0, 0);
		OsdWrite(2, "", 0, 0);
		OsdWrite(3, "", 0, 0);
		OsdWrite(4, "", 0, 0);
		OsdWrite(5, "", 0, 0);
		OsdWrite(6, " ", 0, 0);
		for (int i = 7; i < OsdGetSize() - 1; i++) OsdWrite(i, "", 0, 0);
		OsdWrite(OsdGetSize() - 1, STD_SPACE_EXIT, menusub == 0, 0);
		break;

	case MENU_8BIT_JOYTEST_B2:
		get_joystick_state(joy_string, joy_string2, 1);
		get_joystick_id(usb_id, 1, 0);
		OsdWrite(1, usb_id, 0, 0);
		OsdWrite(3, joy_string, 0, 0);
		OsdWrite(4, joy_string2, 0, 0);
		OsdWrite(5, " ", 0, 0);
		// display raw USB input
		get_joystick_state_usb(s, 1);
		OsdWrite(6, s, 0, 0);
		// allow allow exit when hitting space
		if (c == KEY_SPACE) {
			menustate = MENU_8BIT_CONTROLLERS1;
			menusub = 2;
		}
		break;

	case MENU_8BIT_TURBO1:
		helptext = helptexts[HELPTEXT_NONE];
		menumask = 0x1F;
		OsdSetTitle("Turbo", 0);
		menustate = MENU_8BIT_TURBO2;
		parentstate = MENU_8BIT_TURBO1;
		//StateJoyState(0, &mist_joy[0]);
		//joy0 = mist_joy[0];//StateJoyGet(0);
		StateJoyState(0, &mist_joy[0]);
		joy0 = mist_joy[0];
		//StateJoyState(1, &mist_joy[1]);
		joy1 = mist_joy[1];//StateJoyGet(1);
		OsdWrite(0, "    Button Configuration", 1, 0);
		OsdWrite(1, "", 0, 0);
		strcpy(s, "    Joy 1 Turbo     : ");
		strcat(s, config_button_turbo_msg[(int)joy0.turbo / OSD_TURBO_STEP]);
		OsdWrite(2, s, menusub == 0, 0);
		strcpy(s, "          Buttons   : ");
		strcat(s, config_button_turbo_choice_msg[(int)joy0.turbo_mask / 16]);
		OsdWrite(3, s, menusub == 1, 0);
		strcpy(s, "    Joy 2 Turbo     : ");
		strcat(s, config_button_turbo_msg[(int)joy1.turbo / OSD_TURBO_STEP]);
		OsdWrite(4, s, menusub == 2, 0);
		strcpy(s, "          Buttons   : ");
		strcat(s, config_button_turbo_choice_msg[(int)joy1.turbo_mask / 16]);
		OsdWrite(5, s, menusub == 3, 0);
		OsdWrite(6, " ", 0, 0);
		for (int i = 7; i < OsdGetSize() - 1; i++) OsdWrite(i, "", 0, 0);
		OsdWrite(OsdGetSize() - 1, STD_EXIT, menusub == 4, 0);
		break;

	case MENU_8BIT_TURBO2:
		StateJoyState(0, &mist_joy[0]);
		joy0 = mist_joy[0];
		//StateJoyState(1, &mist_joy[1]);
		//joy0 = mist_joy[0];//StateJoyGet(0);
		joy1 = mist_joy[1];//StateJoyGet(1);
		strcpy(s, "    Joy 1 Turbo     : ");
		strcat(s, config_button_turbo_msg[(int)(mist_joy[0].turbo / OSD_TURBO_STEP) & 0xFF]);
		OsdWrite(2, s, menusub == 0, 0);
		strcpy(s, "          Buttons   : ");
		strcat(s, config_button_turbo_choice_msg[(int)(mist_joy[0].turbo_mask / 16 - 1) & 0xFF]);
		OsdWrite(3, s, menusub == 1, 0);
		strcpy(s, "    Joy 2 Turbo     : ");
		strcat(s, config_button_turbo_msg[(int)(mist_joy[1].turbo / OSD_TURBO_STEP) & 0xFF]);
		OsdWrite(4, s, menusub == 2, 0);
		strcpy(s, "          Buttons   : ");
		strcat(s, config_button_turbo_choice_msg[(int)(mist_joy[1].turbo_mask / 16 - 1) & 0xFF]);
		OsdWrite(5, s, menusub == 3, 0);
		// menu key goes back to previous menu
		if (menu) {
			menustate = MENU_8BIT_CONTROLLERS1;
			menusub = 0;
		}
		if (select) {
			if (menusub == 0) {
				mist_joy[0].turbo += OSD_TURBO_STEP;
				if (mist_joy[0].turbo>OSD_TURBO_STEP * 3) mist_joy[0].turbo = 0;
			}
			if (menusub == 1) {
				mist_joy[0].turbo_mask += 16;
				if (mist_joy[0].turbo_mask>16 * 3) mist_joy[0].turbo_mask = 16;
			}
			if (menusub == 2) {
				mist_joy[1].turbo += OSD_TURBO_STEP;
				if (mist_joy[1].turbo>OSD_TURBO_STEP * 3) mist_joy[1].turbo = 0;
			}
			if (menusub == 3) {
				mist_joy[1].turbo_mask += 16;
				if (mist_joy[1].turbo_mask>16 * 3) mist_joy[1].turbo_mask = 16;
			}
			if (menusub == 4) {
				menustate = MENU_8BIT_CONTROLLERS1;
				menusub = 0;
			}
		}
		break;

	case MENU_8BIT_CHRTEST1:
		helptext = helptexts[HELPTEXT_NONE];
		menumask = 0;
		OsdSetTitle("CHR", 0);
		menustate = MENU_8BIT_CHRTEST2;
		parentstate = MENU_8BIT_CHRTEST1;
		strcpy(usb_id, "                          ");
		for (i = 1; i<24; i++) {
			if (i<4 || i>13)
				usb_id[i] = i;
			else
				usb_id[i] = ' ';
		}
		OsdWrite(0, usb_id, 0, 0);
		for (i = 0; i<24; i++) usb_id[i] = i + 24;
		OsdWrite(1, usb_id, 0, 0);
		for (i = 0; i<24; i++) usb_id[i] = i + (24 * 2);
		OsdWrite(2, usb_id, 0, 0);
		for (i = 0; i<24; i++) usb_id[i] = i + (24 * 3);
		OsdWrite(3, usb_id, 0, 0);
		for (i = 0; i<24; i++) usb_id[i] = i + (24 * 4);
		OsdWrite(4, usb_id, 0, 0);
		strcpy(usb_id, "                          ");
		for (i = 0; i<8; i++) usb_id[i] = i + (24 * 5);
		OsdWrite(5, usb_id, 0, 0);
		//for(i=0; i<24; i++) usb_id[i] = i+(24*6);
		OsdWrite(6, "", 0, 0);
		for (int i = 7; i < OsdGetSize() - 1; i++) OsdWrite(i, "", 0, 0);
		OsdWrite(OsdGetSize() - 1, STD_SPACE_EXIT, menusub == 0, 0);
		break;

	case MENU_8BIT_CHRTEST2:
		if (c == KEY_SPACE) {
			menustate = MENU_8BIT_CONTROLLERS1;
			menusub = 1;
		}
		break;

		/******************************************************************/
		/* mist main menu                                                 */
		/******************************************************************/

	case MENU_MIST_MAIN1:
		OsdSetSize(8);
		menumask = 0xff;
		OsdSetTitle("Mist", 0);

		// most important: main page has setup for floppy A: and screen
		strcpy(s, " A: ");
		strcat(s, tos_get_disk_name(0));
		if (tos_system_ctrl() & TOS_CONTROL_FDC_WR_PROT_A) strcat(s, " \x17");
		OsdWrite(0, s, menusub == 0, 0);

		strcpy(s, " Screen: ");
		if (tos_system_ctrl() & TOS_CONTROL_VIDEO_COLOR) strcat(s, "Color");
		else                                          strcat(s, "Mono");
		OsdWrite(1, s, menusub == 1, 0);

		/* everything else is in submenus */
		OsdWrite(2, " Storage                   \x16", menusub == 2, 0);
		OsdWrite(3, " System                    \x16", menusub == 3, 0);
		OsdWrite(4, " Audio / Video             \x16", menusub == 4, 0);
		OsdWrite(5, " Firmware & Core           \x16", menusub == 5, 0);

		OsdWrite(6, " Save config                ", menusub == 6, 0);

		OsdWrite(7, STD_EXIT, menusub == 7, 0);

		menustate = MENU_MIST_MAIN2;
		parentstate = MENU_MIST_MAIN1;
		break;

	case MENU_MIST_MAIN2:
		// menu key closes menu
		if (menu)
			menustate = MENU_NONE1;
		if (select) {
			switch (menusub) {
			case 0:
				if (tos_disk_is_inserted(0)) {
					tos_insert_disk(0, NULL);
					menustate = MENU_MIST_MAIN1;
				}
				else
					SelectFile("ST ", SCAN_DIR, MENU_MIST_MAIN_FILE_SELECTED, MENU_MIST_MAIN1, 0);
				break;

			case 1:
				tos_update_sysctrl(tos_system_ctrl() ^ TOS_CONTROL_VIDEO_COLOR);
				menustate = MENU_MIST_MAIN1;
				break;

			case 2:  // Storage submenu
				menustate = MENU_MIST_STORAGE1;
				menusub = 0;
				break;

			case 3:  // System submenu
				menustate = MENU_MIST_SYSTEM1;
				menusub = 0;
				break;

			case 4:  // Video submenu
				menustate = MENU_MIST_VIDEO1;
				menusub = 0;
				break;

			case 5:  // Firmware submenu
				menustate = MENU_FIRMWARE1;
				menusub = 0;
				break;

			case 6:  // Save config
				menustate = MENU_NONE1;
				tos_config_save();
				break;

			case 7:  // Exit
				menustate = MENU_NONE1;
				break;
			}
		}
		break;

	case MENU_MIST_MAIN_FILE_SELECTED: // file successfully selected
		tos_insert_disk(0, SelectedPath);
		menustate = MENU_MIST_MAIN1;
		break;

	case MENU_MIST_STORAGE1:
		menumask = tos_get_direct_hdd() ? 0x3f : 0x7f;
		OsdSetTitle("Storage", 0);
		// entries for both floppies
		for (i = 0; i<2; i++) {
			strcpy(s, " A: ");
			strcat(s, tos_get_disk_name(i));
			s[1] = 'A' + i;
			if (tos_system_ctrl() & (TOS_CONTROL_FDC_WR_PROT_A << i))
				strcat(s, " \x17");
			OsdWrite(i, s, menusub == i, 0);
		}
		strcpy(s, " Write protect: ");
		strcat(s, config_tos_wrprot[(tos_system_ctrl() >> 6) & 3]);
		OsdWrite(2, s, menusub == 2, 0);
		OsdWrite(3, "", 0, 0);
		strcpy(s, " ACSI0 direct SD: ");
		strcat(s, tos_get_direct_hdd() ? "on" : "off");
		OsdWrite(4, s, menusub == 3, 0);
		for (i = 0; i<2; i++) {
			strcpy(s, " ACSI0: ");
			s[5] = '0' + i;

			strcat(s, tos_get_disk_name(2 + i));
			OsdWrite(5 + i, s, ((i == 1) || !tos_get_direct_hdd()) ? (menusub == (!tos_get_direct_hdd() ? 4 : 3) + i) : 0,
				(i == 0) && tos_get_direct_hdd());
		}
		OsdWrite(7, STD_EXIT, !tos_get_direct_hdd() ? (menusub == 6) : (menusub == 5), 0);
		parentstate = menustate;
		menustate = MENU_MIST_STORAGE2;
		break;


	case MENU_MIST_STORAGE2:
		if (menu) {
			menustate = MENU_MIST_MAIN1;
			menusub = 2;
		}
		if (select) {
			if (menusub <= 1) {
				if (tos_disk_is_inserted(menusub)) {
					tos_insert_disk(menusub, NULL);
					menustate = MENU_MIST_STORAGE1;
				}
				else
					SelectFile("ST ", SCAN_DIR, MENU_MIST_STORAGE_FILE_SELECTED, MENU_MIST_STORAGE1, 0);
			}
			else if (menusub == 2) {
				// remove current write protect bits and increase by one
				tos_update_sysctrl((tos_system_ctrl() & ~(TOS_CONTROL_FDC_WR_PROT_A | TOS_CONTROL_FDC_WR_PROT_B))
					| (((((tos_system_ctrl() >> 6) & 3) + 1) & 3) << 6));
				menustate = MENU_MIST_STORAGE1;

			}
			else if (menusub == 3) {
				tos_set_direct_hdd(!tos_get_direct_hdd());
				menustate = MENU_MIST_STORAGE1;

				// no direct hhd emulation: Both ACSI entries are enabled
				// or direct hhd emulation for ACSI0: Only second ACSI entry is enabled
			}
			else if ((menusub == 4) || (!tos_get_direct_hdd() && (menusub == 5))) {
				char disk_idx = menusub - (tos_get_direct_hdd() ? 1 : 2);
				iprintf("Select image for disk %d\n", disk_idx);

				if (tos_disk_is_inserted(disk_idx)) {
					tos_insert_disk(disk_idx, NULL);
					menustate = MENU_MIST_STORAGE1;
				}
				else
					SelectFile("HD ", 0, MENU_MIST_STORAGE_FILE_SELECTED, MENU_MIST_STORAGE1, 0);

			}
			else if (tos_get_direct_hdd() ? (menusub == 5) : (menusub == 6)) {
				menustate = MENU_MIST_MAIN1;
				menusub = 2;
			}
		}
		break;

	case MENU_MIST_STORAGE_FILE_SELECTED: // file successfully selected
										  // floppy/hdd      
		if (menusub < 2)
			tos_insert_disk(menusub, SelectedPath);
		else {
			char disk_idx = menusub - (tos_get_direct_hdd() ? 1 : 2);
			iprintf("Insert image for disk %d\n", disk_idx);
			tos_insert_disk(disk_idx, SelectedPath);
		}
		menustate = MENU_MIST_STORAGE1;
		break;

	case MENU_MIST_SYSTEM1:
		menumask = 0xff;
		OsdSetTitle("System", 0);

		strcpy(s, " Memory:    ");
		strcat(s, config_tos_mem[(tos_system_ctrl() >> 1) & 7]);
		OsdWrite(0, s, menusub == 0, 0);

		strcpy(s, " CPU:       ");
		strcat(s, config_cpu_msg[(tos_system_ctrl() >> 4) & 3]);
		OsdWrite(1, s, menusub == 1, 0);

		strcpy(s, " TOS:       ");
		strcat(s, tos_get_image_name());
		OsdWrite(2, s, menusub == 2, 0);

		strcpy(s, " Cartridge: ");
		strcat(s, tos_get_cartridge_name());
		OsdWrite(3, s, menusub == 3, 0);

		strcpy(s, " USB I/O:   ");
		strcat(s, "NONE"); //config_tos_usb[tos_get_cdc_control_redirect()]);
		OsdWrite(4, s, menusub == 4, 0);

		OsdWrite(5, " Reset", menusub == 5, 0);
		OsdWrite(6, " Cold boot", menusub == 6, 0);

		OsdWrite(7, STD_EXIT, menusub == 7, 0);

		parentstate = menustate;
		menustate = MENU_MIST_SYSTEM2;
		break;

	case MENU_MIST_SYSTEM2:
		if (menu) {
			menustate = MENU_MIST_MAIN1;
			menusub = 3;
		}
		if (select) {
			switch (menusub) {
			case 0: { // RAM
				int mem = (tos_system_ctrl() >> 1) & 7;   // current memory config
				mem++;
				if (mem > 5) mem = 3;                 // cycle 4MB/8MB/14MB
				tos_update_sysctrl((tos_system_ctrl() & ~0x0e) | (mem << 1));
				tos_reset(1);
				menustate = MENU_MIST_SYSTEM1;
			} break;

			case 1: { // CPU
				int cpu = (tos_system_ctrl() >> 4) & 3;   // current cpu config
				cpu = (cpu + 1) & 3;
				if (cpu == 2) cpu = 3;                 // skip unused config
				tos_update_sysctrl((tos_system_ctrl() & ~0x30) | (cpu << 4));
				tos_reset(0);
				menustate = MENU_MIST_SYSTEM1;
			} break;

			case 2:  // TOS
				SelectFile("IMG", 0, MENU_MIST_SYSTEM_FILE_SELECTED, MENU_MIST_SYSTEM1, 0);
				break;

			case 3:  // Cart
					 // if a cart name is set, then remove it
				if (tos_cartridge_is_inserted()) {
					tos_load_cartridge("");
					menustate = MENU_MIST_SYSTEM1;
				}
				else
					SelectFile("IMG", 0, MENU_MIST_SYSTEM_FILE_SELECTED, MENU_MIST_SYSTEM1, 0);
				break;

			case 4:
				menustate = MENU_MIST_SYSTEM1;
				break;

			case 5:  // Reset
				tos_reset(0);
				menustate = MENU_NONE1;
				break;

			case 6:  // Cold Boot
				tos_reset(1);
				menustate = MENU_NONE1;
				break;

			case 7:
				menustate = MENU_MIST_MAIN1;
				menusub = 3;
				break;
			}
		}
		break;

	case MENU_MIST_SYSTEM_FILE_SELECTED: // file successfully selected
		if (menusub == 2) {
			tos_upload(SelectedPath);
			menustate = MENU_MIST_SYSTEM1;
		}
		if (menusub == 3) {
			tos_load_cartridge(SelectedPath);
			menustate = MENU_MIST_SYSTEM1;
		}
		break;


	case MENU_MIST_VIDEO1:

		menumask = 0x7f;
		OsdSetTitle("A/V", 0);

		strcpy(s, " Screen:        ");
		if (tos_system_ctrl() & TOS_CONTROL_VIDEO_COLOR) strcat(s, "Color");
		else                                            strcat(s, "Mono");
		OsdWrite(0, s, menusub == 0, 0);

		// Viking card can only be enabled with max 8MB RAM
		enable = (tos_system_ctrl() & 0xe) <= TOS_MEMCONFIG_8M;
		strcpy(s, " Viking/SM194:  ");
		strcat(s, ((tos_system_ctrl() & TOS_CONTROL_VIKING) && enable) ? "on" : "off");
		OsdWrite(1, s, menusub == 1, enable ? 0 : 1);

		// Blitter is always present in >= STE
		enable = (tos_system_ctrl() & (TOS_CONTROL_STE | TOS_CONTROL_MSTE)) ? 1 : 0;
		strcpy(s, " Blitter:       ");
		strcat(s, ((tos_system_ctrl() & TOS_CONTROL_BLITTER) || enable) ? "on" : "off");
		OsdWrite(2, s, menusub == 2, enable);

		strcpy(s, " Chipset:       ");
		// extract  TOS_CONTROL_STE and  TOS_CONTROL_MSTE bits
		strcat(s, atari_chipset[(tos_system_ctrl() >> 23) & 3]);
		OsdWrite(3, s, menusub == 3, 0);

		OsdWrite(4, " Video adjust              \x16", menusub == 4, 0);

		strcpy(s, " YM-Audio:      ");
		strcat(s, stereo[(tos_system_ctrl() & TOS_CONTROL_STEREO) ? 1 : 0]);
		OsdWrite(5, s, menusub == 5, 0);
		OsdWrite(6, "", 0, 0);

		OsdWrite(7, STD_EXIT, menusub == 6, 0);

		parentstate = menustate;
		menustate = MENU_MIST_VIDEO2;
		break;

	case MENU_MIST_VIDEO2:
		if (menu) {
			menustate = MENU_MIST_MAIN1;
			menusub = 4;
		}

		if (select) {
			switch (menusub) {
			case 0:
				tos_update_sysctrl(tos_system_ctrl() ^ TOS_CONTROL_VIDEO_COLOR);
				menustate = MENU_MIST_VIDEO1;
				break;

			case 1:
				// viking/sm194
				tos_update_sysctrl(tos_system_ctrl() ^ TOS_CONTROL_VIKING);
				menustate = MENU_MIST_VIDEO1;
				break;

			case 2:
				if (!(tos_system_ctrl() & TOS_CONTROL_STE)) {
					tos_update_sysctrl(tos_system_ctrl() ^ TOS_CONTROL_BLITTER);
					menustate = MENU_MIST_VIDEO1;
				}
				break;

			case 3: {
				unsigned long chipset = (tos_system_ctrl() >> 23) + 1;
				if (chipset == 4) chipset = 0;
				tos_update_sysctrl(tos_system_ctrl() & ~(TOS_CONTROL_STE | TOS_CONTROL_MSTE) |
					(chipset << 23));
				menustate = MENU_MIST_VIDEO1;
			} break;

			case 4:
				menustate = MENU_MIST_VIDEO_ADJUST1;
				menusub = 0;
				break;

			case 5:
				tos_update_sysctrl(tos_system_ctrl() ^ TOS_CONTROL_STEREO);
				menustate = MENU_MIST_VIDEO1;
				break;

			case 6:
				menustate = MENU_MIST_MAIN1;
				menusub = 4;
				break;
			}
		}
		break;

	case MENU_MIST_VIDEO_ADJUST1:

		menumask = 0x1f;
		OsdSetTitle("V-adjust", 0);

		OsdWrite(0, "", 0, 0);

		strcpy(s, " PAL mode:    ");
		if (tos_system_ctrl() & TOS_CONTROL_PAL50HZ) strcat(s, "50Hz");
		else                                      strcat(s, "56Hz");
		OsdWrite(1, s, menusub == 0, 0);

		strcpy(s, " Scanlines:   ");
		strcat(s, scanlines[(tos_system_ctrl() >> 20) & 3]);
		OsdWrite(2, s, menusub == 1, 0);

		OsdWrite(3, "", 0, 0);

		siprintf(s, " Horizontal:  %d", tos_get_video_adjust(0));
		OsdWrite(4, s, menusub == 2, 0);

		siprintf(s, " Vertical:    %d", tos_get_video_adjust(1));
		OsdWrite(5, s, menusub == 3, 0);

		OsdWrite(6, "", 0, 0);

		OsdWrite(7, STD_EXIT, menusub == 4, 0);

		parentstate = menustate;
		menustate = MENU_MIST_VIDEO_ADJUST2;
		break;

	case MENU_MIST_VIDEO_ADJUST2:
		if (menu) {
			menustate = MENU_MIST_VIDEO1;
			menusub = 4;
		}

		// use left/right to adjust video position
		if (left || right) {
			if ((menusub == 2) || (menusub == 3)) {
				if (left && (tos_get_video_adjust(menusub - 2) > -100))
					tos_set_video_adjust(menusub - 2, -1);

				if (right && (tos_get_video_adjust(menusub - 2) < 100))
					tos_set_video_adjust(menusub - 2, +1);

				menustate = MENU_MIST_VIDEO_ADJUST1;
			}
		}

		if (select) {
			switch (menusub) {
			case 0:
				tos_update_sysctrl(tos_system_ctrl() ^ TOS_CONTROL_PAL50HZ);
				menustate = MENU_MIST_VIDEO_ADJUST1;
				break;

			case 1: {
				// next scanline state
				int scan = ((tos_system_ctrl() >> 20) + 1) & 3;
				tos_update_sysctrl((tos_system_ctrl() & ~TOS_CONTROL_SCANLINES) | (scan << 20));
				menustate = MENU_MIST_VIDEO_ADJUST1;
			} break;

				// entries 2 and 3 use left/right

			case 4:
				menustate = MENU_MIST_VIDEO1;
				menusub = 4;
				break;
			}
		}
		break;

		/******************************************************************/
		/* minimig main menu                                              */
		/******************************************************************/
	case MENU_MAIN1:
		OsdSetSize(16);
		menumask = 0xFF0;	// b01110000 Floppy turbo, Harddisk options & Exit.
		OsdSetTitle("Minimig", OSD_ARROW_RIGHT);
		helptext = helptexts[HELPTEXT_MAIN];

		OsdWrite(0, "", 0, 0);

		// floppy drive info
		// We display a line for each drive that's active
		// in the config file, but grey out any that the FPGA doesn't think are active.
		// We also print a help text in place of the last drive if it's inactive.
		for (i = 0; i < 4; i++)
		{
			if (i == config.floppy.drives + 1)
				OsdWrite(i+1, " KP +/- to add/remove drives", 0, 1);
			else
			{
				strcpy(s, " dfx: ");
				s[3] = i + '0';
				if (i <= drives)
				{
					menumask |= (1 << i);	// Make enabled drives selectable

					if (df[i].status & DSK_INSERTED) // floppy disk is inserted
					{
						char *p;
						if (p = strrchr(df[i].name, '/'))
						{
							p++;
						}
						else
						{
							p = df[i].name;
						}

						int len = strlen(p);
						if (len > 22) len = 21;
						strncpy(&s[6], p, len);
						s[6 + len] = ' ';
						s[6 + len + 1] = 0;
						s[6 + len + 2] = 0;
						if (!(df[i].status & DSK_WRITABLE)) s[6 + len + 1] = '\x17'; // padlock icon for write-protected disks
					}
					else // no floppy disk
					{
						strcat(s, "* no disk *");
					}
				}
				else if (i <= config.floppy.drives)
				{
					strcat(s, "* active after reset *");
				}
				else
					strcpy(s, "");
				OsdWrite(i+1, s, menusub == i, (i>drives) || (i>config.floppy.drives));
			}
		}
		siprintf(s, " Floppy disk turbo : %s", config.floppy.speed ? "on" : "off");
		OsdWrite(5, s, menusub == 4, 0);
		OsdWrite(6, "", 0, 0);

		OsdWrite(7, "  Hard disk settings \x16", menusub == 5, 0);
		OsdWrite(8, "    chipset settings \x16", menusub == 6, 0);
		OsdWrite(9, "     memory settings \x16", menusub == 7, 0);
		OsdWrite(10, "      video settings \x16", menusub == 8, 0);
		OsdWrite(11, "", 0, 0);

		OsdWrite(12, "    save configuration", menusub == 9, 0);
		OsdWrite(13, "    load configuration", menusub == 10, 0);
		OsdWrite(14, "", 0, 0);

		OsdWrite(15, STD_EXIT, menusub == 11, 0);

		menustate = MENU_MAIN2;
		parentstate = MENU_MAIN1;
		break;

	case MENU_MAIN2:
		if (menu)
			menustate = MENU_NONE1;
		else if (plus && (config.floppy.drives<3))
		{
			config.floppy.drives++;
			ConfigFloppy(config.floppy.drives, config.floppy.speed);
			menustate = MENU_MAIN1;
		}
		else if (minus && (config.floppy.drives>0))
		{
			config.floppy.drives--;
			ConfigFloppy(config.floppy.drives, config.floppy.speed);
			menustate = MENU_MAIN1;
		}
		else if (select)
		{
			if (menusub < 4)
			{
				if (df[menusub].status & DSK_INSERTED) // eject selected floppy
				{
					df[menusub].status = 0;
					menustate = MENU_MAIN1;
				}
				else
				{
					df[menusub].status = 0;
					SelectFile("ADF", SCAN_DIR, MENU_FILE_SELECTED, MENU_MAIN1, 1);
				}
			}
			else if (menusub == 4)	// Toggle floppy turbo
			{
				config.floppy.speed ^= 1;
				ConfigFloppy(config.floppy.drives, config.floppy.speed);
				menustate = MENU_MAIN1;
			}
			else if (menusub == 5)	// Go to harddrives page.
			{
				t_hardfile[0] = config.hardfile[0];
				t_hardfile[1] = config.hardfile[1];
				menustate = MENU_SETTINGS_HARDFILE1;
				menusub = 0;
			}
			else if (menusub == 6)
			{
				menustate = MENU_SETTINGS_CHIPSET1;
				menusub = 0;
			}
			else if (menusub == 7)
			{
				menustate = MENU_SETTINGS_MEMORY1;
				menusub = 0;
			}
			else if (menusub == 8)
			{
				menustate = MENU_SETTINGS_VIDEO1;
				menusub = 0;
			}
			else if (menusub == 9)
			{
				menusub = 0;
				menustate = MENU_SAVECONFIG_1;
			}
			else if (menusub == 10)
			{
				menusub = 0;
				menustate = MENU_LOADCONFIG_1;
			}
			else if (menusub == 11)
				menustate = MENU_NONE1;
		}
		else if (c == KEY_BACK) // eject all floppies
		{
			for (i = 0; i <= drives; i++)
				df[i].status = 0;

			menustate = MENU_MAIN1;
		}
		else if (right)
		{
			menustate = MENU_8BIT_SYSTEM1;
			menusub = 0;
		}
		break;

	case MENU_FILE_SELECTED: // file successfully selected

		InsertFloppy(&df[menusub], SelectedPath);
		menustate = MENU_MAIN1;
		menusub++;
		if (menusub > drives)
			menusub = 6;

		break;

	case MENU_LOADCONFIG_1:
		helptext = helptexts[HELPTEXT_NONE];
		if (parentstate != menustate)	// First run?
		{
			menumask = 0x20;
			SetConfigurationFilename(0); if (ConfigurationExists(0)) menumask |= 0x01;
			SetConfigurationFilename(1); if (ConfigurationExists(0)) menumask |= 0x02;
			SetConfigurationFilename(2); if (ConfigurationExists(0)) menumask |= 0x04;
			SetConfigurationFilename(3); if (ConfigurationExists(0)) menumask |= 0x08;
			SetConfigurationFilename(4); if (ConfigurationExists(0)) menumask |= 0x10;
		}
		parentstate = menustate;
		OsdSetTitle("Load", 0);

		OsdWrite(0, "", 0, 0);
		OsdWrite(1, "          Default", menusub == 0, (menumask & 1) == 0);
		OsdWrite(2, "          1", menusub == 1, (menumask & 2) == 0);
		OsdWrite(3, "          2", menusub == 2, (menumask & 4) == 0);
		OsdWrite(4, "          3", menusub == 3, (menumask & 8) == 0);
		OsdWrite(5, "          4", menusub == 4, (menumask & 0x10) == 0);
		OsdWrite(6, "", 0, 0);
		for (int i = 7; i < OsdGetSize() - 1; i++) OsdWrite(i, "", 0, 0);
		OsdWrite(OsdGetSize() - 1, STD_EXIT, menusub == 5, 0);

		menustate = MENU_LOADCONFIG_2;
		break;

	case MENU_LOADCONFIG_2:

		if (down)
		{
			//            if (menusub < 3)
			if (menusub < 5)
				menusub++;
			menustate = MENU_LOADCONFIG_1;
		}
		else if (select)
		{
			if (menusub<5)
			{
				OsdDisable();
				SetConfigurationFilename(menusub);
				LoadConfiguration(NULL);
				//				OsdReset(RESET_NORMAL);
				menustate = MENU_NONE1;
			}
			else
			{
				menustate = MENU_MAIN1;
				menusub = 10;
			}
		}
		if (menu) // exit menu
		{
			menustate = MENU_MAIN1;
			menusub = 10;
		}
		break;

		/******************************************************************/
		/* file selection menu                                            */
		/******************************************************************/
	case MENU_FILE_SELECT1:
		helptext = helptexts[HELPTEXT_NONE];
		OsdSetTitle("Select", 0);
		PrintDirectory();
		menustate = MENU_FILE_SELECT2;
		break;

	case MENU_FILE_SELECT2:
		menumask = 0;

		ScrollLongName(); // scrolls file name if longer than display line

		if (c == KEY_HOME)
		{
			ScanDirectory(SelectedPath, SCAN_INIT, fs_pFileExt, fs_Options);
			menustate = MENU_FILE_SELECT1;
		}

		if (c == KEY_BACK)
		{
			changeDir("..");
			menustate = MENU_FILE_SELECT1;
		}

		if ((c == KEY_PGUP) || (c == KEY_LEFT))
		{
			ScanDirectory(SelectedPath, SCAN_PREV_PAGE, fs_pFileExt, fs_Options);
			menustate = MENU_FILE_SELECT1;
		}

		if ((c == KEY_PGDN) || (c == KEY_RIGHT))
		{
			ScanDirectory(SelectedPath, SCAN_NEXT_PAGE, fs_pFileExt, fs_Options);
			menustate = MENU_FILE_SELECT1;
		}

		if (down) // scroll down one entry
		{
			ScanDirectory(SelectedPath, SCAN_NEXT, fs_pFileExt, fs_Options);
			menustate = MENU_FILE_SELECT1;
		}

		if (up) // scroll up one entry
		{
			ScanDirectory(SelectedPath, SCAN_PREV, fs_pFileExt, fs_Options);
			menustate = MENU_FILE_SELECT1;
		}

		if ((i = GetASCIIKey(c)))
		{ 
			// find an entry beginning with given character
			ScanDirectory(SelectedPath, i, fs_pFileExt, fs_Options);
			menustate = MENU_FILE_SELECT1;
		}

		if (select)
		{
			if(DirItem[iSelectedEntry].d_type == DT_DIR)
			{
				changeDir(DirItem[iSelectedEntry].d_name);
				menustate = MENU_FILE_SELECT1;
			}
			else
			{
				if (nDirEntries)
				{
					if(strlen(SelectedPath)) strcat(SelectedPath, "/");
					strcat(SelectedPath, DirItem[iSelectedEntry].d_name);

					menustate = fs_MenuSelect;
				}
			}
		}

		if (menu)
		{
			menustate = fs_MenuCancel;
		}

		break;

		/******************************************************************/
		/* reset menu                                                     */
		/******************************************************************/
	case MENU_RESET1:
		m = 0;
		if (user_io_core_type() == CORE_TYPE_MINIMIG2) m = 1;
		helptext = helptexts[HELPTEXT_NONE];
		OsdSetTitle("Reset", 0);
		menumask = 0x03;	// Yes / No
		parentstate = menustate;

		OsdWrite(0, "", 0, 0);
		OsdWrite(1, m ? "         Reset MiST?" : "       Reset settings?", 0, 0);
		OsdWrite(2, "", 0, 0);
		OsdWrite(3, "             yes", menusub == 0, 0);
		OsdWrite(4, "             no", menusub == 1, 0);
		OsdWrite(5, "", 0, 0);
		OsdWrite(6, "", 0, 0);
		for (int i = 7; i < OsdGetSize(); i++) OsdWrite(i, "", 0, 0);

		menustate = MENU_RESET2;
		break;

	case MENU_RESET2:

		m = 0;
		if (user_io_core_type() == CORE_TYPE_MINIMIG2) m = 1;

		if (select && menusub == 0)
		{
			if (m) {
				menustate = MENU_NONE1;
				OsdReset(RESET_NORMAL);
			}
			else {
				char *filename = user_io_create_config_name();
				unsigned long status = user_io_8bit_set_status(0, 0xffffffff);
				iprintf("Saving config to %s\n", filename);
				FileSaveConfig(filename, &status, 4);
				menustate = MENU_8BIT_MAIN1;
				menusub = 0;
			}
		}

		if (menu || (select && (menusub == 1))) // exit menu
		{
			menustate = MENU_8BIT_SYSTEM1;
			menusub = 3;
		}
		break;

	case MENU_SAVECONFIG_1:
		helptext = helptexts[HELPTEXT_NONE];
		menumask = 0x3f;
		parentstate = menustate;
		OsdSetTitle("Save", 0);

		OsdWrite(0, "", 0, 0);
		OsdWrite(1, "        Default", menusub == 0, 0);
		OsdWrite(2, "        1", menusub == 1, 0);
		OsdWrite(3, "        2", menusub == 2, 0);
		OsdWrite(4, "        3", menusub == 3, 0);
		OsdWrite(5, "        4", menusub == 4, 0);
		OsdWrite(6, "", 0, 0);
		for (int i = 7; i < OsdGetSize() - 1; i++) OsdWrite(i, "", 0, 0);
		OsdWrite(OsdGetSize() - 1, STD_EXIT, menusub == 5, 0);

		menustate = MENU_SAVECONFIG_2;
		break;

	case MENU_SAVECONFIG_2:

		if (menu)
		{
			menustate = MENU_MAIN1;
			menusub = 9;
		}

		else if (up)
		{
			if (menusub > 0)
				menusub--;
			menustate = MENU_SAVECONFIG_1;
		}
		else if (down)
		{
			//            if (menusub < 3)
			if (menusub < 5)
				menusub++;
			menustate = MENU_SAVECONFIG_1;
		}
		else if (select)
		{
			if (menusub<5)
			{
				SetConfigurationFilename(menusub);
				SaveConfiguration(NULL);
				menustate = MENU_NONE1;
			}
			else
			{
				menustate = MENU_MAIN1;
				menusub = 9;
			}
		}
		if (menu) // exit menu
		{
			menustate = MENU_MAIN1;
			menusub = 9;
		}
		break;



		/******************************************************************/
		/* chipset settings menu                                          */
		/******************************************************************/
	case MENU_SETTINGS_CHIPSET1:
		helptext = helptexts[HELPTEXT_CHIPSET];
		menumask = 0;
		OsdSetTitle("Chipset", OSD_ARROW_LEFT | OSD_ARROW_RIGHT);

		OsdWrite(0, "", 0, 0);
		strcpy(s, "         CPU : ");
		strcat(s, config_cpu_msg[config.cpu & 0x03]);
		OsdWrite(1, s, menusub == 0, 0);
		strcpy(s, "       Turbo : ");
		strcat(s, config_turbo_msg[(config.cpu >> 2) & 0x03]);
		OsdWrite(2, s, menusub == 1, 0);
		strcpy(s, "       Video : ");
		strcat(s, config.chipset & CONFIG_NTSC ? "NTSC" : "PAL");
		OsdWrite(3, s, menusub == 2, 0);
		strcpy(s, "     Chipset : ");
		strcat(s, config_chipset_msg[(config.chipset >> 2) & 7]);
		OsdWrite(4, s, menusub == 3, 0);
		strcpy(s, "     CD32Pad : ");
		strcat(s, config_cd32pad_msg[(config.autofire >> 2) & 1]);
		OsdWrite(5, s, menusub == 4, 0);
		OsdWrite(6, "", 0, 0);
		for (int i = 7; i < OsdGetSize() - 1; i++) OsdWrite(i, "", 0, 0);
		OsdWrite(OsdGetSize() - 1, STD_EXIT, menusub == 5, 0);

		menustate = MENU_SETTINGS_CHIPSET2;
		break;

	case MENU_SETTINGS_CHIPSET2:

		if (down && menusub < 5)
		{
			menusub++;
			menustate = MENU_SETTINGS_CHIPSET1;
		}

		if (up && menusub > 0)
		{
			menusub--;
			menustate = MENU_SETTINGS_CHIPSET1;
		}

		if (select)
		{
			if (menusub == 0)
			{
				menustate = MENU_SETTINGS_CHIPSET1;
				int _config_cpu = config.cpu & 0x3;
				_config_cpu += 1;
				if (_config_cpu == 0x02) _config_cpu += 1;
				config.cpu = (config.cpu & 0xfc) | (_config_cpu & 0x3);
				ConfigCPU(config.cpu);
			}
			else if (menusub == 1)
			{
				menustate = MENU_SETTINGS_CHIPSET1;
				int _config_turbo = (config.cpu >> 2) & 0x3;
				_config_turbo += 1;
				config.cpu = (config.cpu & 0x3) | ((_config_turbo & 0x3) << 2);
				ConfigCPU(config.cpu);
			}
			else if (menusub == 2)
			{
				config.chipset ^= CONFIG_NTSC;
				menustate = MENU_SETTINGS_CHIPSET1;
				ConfigChipset(config.chipset);
			}
			else if (menusub == 3)
			{
				switch (config.chipset & 0x1c) {
				case 0:
					config.chipset = (config.chipset & 3) | CONFIG_A1000;
					break;
				case CONFIG_A1000:
					config.chipset = (config.chipset & 3) | CONFIG_ECS;
					break;
				case CONFIG_ECS:
					config.chipset = (config.chipset & 3) | CONFIG_AGA | CONFIG_ECS;
					break;
				case (CONFIG_AGA | CONFIG_ECS) :
					config.chipset = (config.chipset & 3) | 0;
					break;
				}

				menustate = MENU_SETTINGS_CHIPSET1;
				ConfigChipset(config.chipset);
			}
			else if (menusub == 4)
			{
				//config.autofire = ((((config.autofire >> 2) + 1) & 1) << 2) || (config.autofire & 3);
				config.autofire = (config.autofire + 4) & 0x7;
				menustate = MENU_SETTINGS_CHIPSET1;
				ConfigAutofire(config.autofire);
			}
			else if (menusub == 5)
			{
				menustate = MENU_MAIN1;
				menusub = 6;
			}
		}

		if (menu)
		{
			menustate = MENU_MAIN1;
			menusub = 6;
		}
		else if (right)
		{
			menustate = MENU_SETTINGS_MEMORY1;
			menusub = 0;
		}
		else if (left)
		{
			menustate = MENU_SETTINGS_VIDEO1;
			menusub = 0;
		}
		break;

		/******************************************************************/
		/* memory settings menu                                           */
		/******************************************************************/
	case MENU_SETTINGS_MEMORY1:
		helptext = helptexts[HELPTEXT_MEMORY];
		menumask = 0x3f;
		parentstate = menustate;

		OsdSetTitle("Memory", OSD_ARROW_LEFT | OSD_ARROW_RIGHT);

		OsdWrite(0, "", 0, 0);
		strcpy(s, "      CHIP  : ");
		strcat(s, config_memory_chip_msg[config.memory & 0x03]);
		OsdWrite(1, s, menusub == 0, 0);
		strcpy(s, "      SLOW  : ");
		strcat(s, config_memory_slow_msg[config.memory >> 2 & 0x03]);
		OsdWrite(2, s, menusub == 1, 0);
		strcpy(s, "      FAST  : ");
		strcat(s, config_memory_fast_msg[config.memory >> 4 & 0x03]);
		OsdWrite(3, s, menusub == 2, 0);

		OsdWrite(4, "", 0, 0);

		strcpy(s, "      ROM   : ");
		strncat(s, config.kickstart, 15);
		OsdWrite(5, s, menusub == 3, 0);

		strcpy(s, "      HRTmon: ");
		strcat(s, (config.memory & 0x40) ? "enabled " : "disabled");
		OsdWrite(6, s, menusub == 4, 0);

		for (int i = 7; i < OsdGetSize() - 1; i++) OsdWrite(i, "", 0, 0);
		OsdWrite(OsdGetSize() - 1, STD_EXIT, menusub == 5, 0);

		menustate = MENU_SETTINGS_MEMORY2;
		break;

	case MENU_SETTINGS_MEMORY2:
		if (select)
		{
			if (menusub == 0)
			{
				config.memory = ((config.memory + 1) & 0x03) | (config.memory & ~0x03);
				menustate = MENU_SETTINGS_MEMORY1;
				ConfigMemory(config.memory);
			}
			else if (menusub == 1)
			{
				config.memory = ((config.memory + 4) & 0x0C) | (config.memory & ~0x0C);
				menustate = MENU_SETTINGS_MEMORY1;
				ConfigMemory(config.memory);
			}
			else if (menusub == 2)
			{
				config.memory = ((config.memory + 0x10) & 0x30) | (config.memory & ~0x30);
				menustate = MENU_SETTINGS_MEMORY1;
				ConfigMemory(config.memory);
			}
			else if (menusub == 3)
			{
				SelectFile("ROM", 0, MENU_ROMFILE_SELECTED, MENU_SETTINGS_MEMORY1, 1);
			}
			else if (menusub == 4)
			{
				config.memory ^= 0x40;
				ConfigMemory(config.memory);
				menustate = MENU_SETTINGS_MEMORY1;
			}
			else if (menusub == 5)
			{
				menustate = MENU_MAIN1;
				menusub = 7;
			}
		}

		if (menu)
		{
			menustate = MENU_MAIN1;
			menusub = 7;
		}
		else if (right)
		{
			menustate = MENU_SETTINGS_VIDEO1;
			menusub = 0;
		}
		else if (left)
		{
			menustate = MENU_SETTINGS_CHIPSET1;
			menusub = 0;
		}
		break;

		/******************************************************************/
		/* hardfile settings menu                                         */
		/******************************************************************/

		// FIXME!  Nasty race condition here.  Changing HDF type has immediate effect
		// which could be disastrous if the user's writing to the drive at the time!
		// Make the menu work on the copy, not the original, and copy on acceptance,
		// not on rejection.
	case MENU_SETTINGS_HARDFILE1:
		helptext = helptexts[HELPTEXT_HARDFILE];
		OsdSetTitle("Harddisks", 0);

		parentstate = menustate;
		menumask = 0x21;	// b00100001 - On/off & exit enabled by default...
		if (config.enable_ide)
			menumask |= 0x0a;  // b00001010 - HD0 and HD1 type
		OsdWrite(0, "", 0, 0);
		strcpy(s, "   A600/A1200 IDE : ");
		strcat(s, config.enable_ide ? "on " : "off");
		OsdWrite(1, s, menusub == 0, 0);
		OsdWrite(2, "", 0, 0);

		strcpy(s, " Master : ");
		if (config.hardfile[0].enabled == (HDF_FILE | HDF_SYNTHRDB))
			strcat(s, "Hardfile (filesys)");
		else
			strcat(s, config_hdf_msg[config.hardfile[0].enabled & HDF_TYPEMASK]);
		OsdWrite(3, s, config.enable_ide ? (menusub == 1) : 0, config.enable_ide == 0);
		if (config.hardfile[0].present)
		{
			strcpy(s, "                                ");
			strncpy(&s[7], config.hardfile[0].long_name, 21);
		}
		else
			strcpy(s, "       ** file not found **");

		enable = config.enable_ide && ((config.hardfile[0].enabled&HDF_TYPEMASK) == HDF_FILE);
		if (enable)
			menumask |= 0x04;	// Make hardfile selectable
		OsdWrite(4, s, enable ? (menusub == 2) : 0, enable == 0);

		OsdWrite(5, "", 0, 0);
		strcpy(s, "  Slave : ");
		if (config.hardfile[1].enabled == (HDF_FILE | HDF_SYNTHRDB))
			strcat(s, "Hardfile (filesys)");
		else
			strcat(s, config_hdf_msg[config.hardfile[1].enabled & HDF_TYPEMASK]);
		OsdWrite(6, s, config.enable_ide ? (menusub == 3) : 0, config.enable_ide == 0);
		if (config.hardfile[1].present) {
			strcpy(s, "                                ");
			strncpy(&s[7], config.hardfile[1].long_name, 21);
		}
		else
			strcpy(s, "       ** file not found **");
		enable = config.enable_ide && ((config.hardfile[1].enabled&HDF_TYPEMASK) == HDF_FILE);
		if (enable)
			menumask |= 0x10;	// Make hardfile selectable
		OsdWrite(7, s, enable ? (menusub == 4) : 0, enable == 0);

		for (int i = 8; i < OsdGetSize() - 1; i++) OsdWrite(i, "", 0, 0);
		OsdWrite(OsdGetSize() - 1, STD_EXIT, menusub == 5, 0);

		menustate = MENU_SETTINGS_HARDFILE2;
		break;

	case MENU_SETTINGS_HARDFILE2:
		if (select)
		{
			if (menusub == 0)
			{
				config.enable_ide = (config.enable_ide == 0);
				menustate = MENU_SETTINGS_HARDFILE1;
			}
			if (menusub == 1)
			{
				if (config.hardfile[0].enabled == HDF_FILE)
				{
					config.hardfile[0].enabled |= HDF_SYNTHRDB;
				}
				else if (config.hardfile[0].enabled == (HDF_FILE | HDF_SYNTHRDB))
				{
					config.hardfile[0].enabled = 0;
				}
				else
				{
					config.hardfile[0].enabled = HDF_FILE;
				}
				menustate = MENU_SETTINGS_HARDFILE1;
			}
			else if (menusub == 2)
			{
				SelectFile("HDF", 0, MENU_HARDFILE_SELECTED, MENU_SETTINGS_HARDFILE1, 1);
			}
			else if (menusub == 3)
			{
				if (config.hardfile[1].enabled == HDF_FILE)
				{
					config.hardfile[1].enabled |= HDF_SYNTHRDB;
				}
				else if (config.hardfile[1].enabled == (HDF_FILE | HDF_SYNTHRDB))
				{
					config.hardfile[1].enabled = 0;
				}
				else
				{
					config.hardfile[1].enabled = HDF_FILE;
				}
				menustate = MENU_SETTINGS_HARDFILE1;
			}
			else if (menusub == 4)
			{
				SelectFile("HDF", 0, MENU_HARDFILE_SELECTED, MENU_SETTINGS_HARDFILE1, 1);
			}
			else if (menusub == 5) // return to previous menu
			{
				menustate = MENU_HARDFILE_EXIT;
			}
		}

		if (menu) // return to previous menu
		{
			menustate = MENU_HARDFILE_EXIT;
		}
		break;

		/******************************************************************/
		/* hardfile selected menu                                         */
		/******************************************************************/
	case MENU_HARDFILE_SELECTED:
		if (menusub == 2) // master drive selected
		{
			// Read RDB from selected drive and determine type...
			memcpy((void*)config.hardfile[0].long_name, SelectedPath, sizeof(config.hardfile[0].long_name));
			switch (GetHDFFileType(SelectedPath))
			{
			case HDF_FILETYPE_RDB:
				config.hardfile[0].enabled = HDF_FILE;
				config.hardfile[0].present = 1;
				menustate = MENU_SETTINGS_HARDFILE1;
				break;
			case HDF_FILETYPE_DOS:
				config.hardfile[0].enabled = HDF_FILE | HDF_SYNTHRDB;
				config.hardfile[0].present = 1;
				menustate = MENU_SETTINGS_HARDFILE1;
				break;
			case HDF_FILETYPE_UNKNOWN:
				config.hardfile[0].present = 1;
				if (config.hardfile[0].enabled == HDF_FILE)	// Warn if we can't detect the type
					menustate = MENU_SYNTHRDB1;
				else
					menustate = MENU_SYNTHRDB2_1;
				menusub = 0;
				break;
			case HDF_FILETYPE_NOTFOUND:
			default:
				config.hardfile[0].present = 0;
				menustate = MENU_SETTINGS_HARDFILE1;
				break;
			}
		}

		if (menusub == 4) // slave drive selected
		{
			memcpy((void*)config.hardfile[1].long_name, SelectedPath, sizeof(config.hardfile[1].long_name));
			switch (GetHDFFileType(SelectedPath))
			{
			case HDF_FILETYPE_RDB:
				config.hardfile[1].enabled = HDF_FILE;
				config.hardfile[1].present = 1;
				menustate = MENU_SETTINGS_HARDFILE1;
				break;
			case HDF_FILETYPE_DOS:
				config.hardfile[1].enabled = HDF_FILE | HDF_SYNTHRDB;
				config.hardfile[1].present = 1;
				menustate = MENU_SETTINGS_HARDFILE1;
				break;
			case HDF_FILETYPE_UNKNOWN:
				config.hardfile[1].present = 1;
				if (config.hardfile[1].enabled == HDF_FILE)	// Warn if we can't detect the type...
					menustate = MENU_SYNTHRDB1;
				else
					menustate = MENU_SYNTHRDB2_1;
				menusub = 0;
				break;
			case HDF_FILETYPE_NOTFOUND:
			default:
				config.hardfile[1].present = 0;
				menustate = MENU_SETTINGS_HARDFILE1;
				break;
			}
		}
		break;

		// check if hardfile configuration has changed
	case MENU_HARDFILE_EXIT:

		if (memcmp(config.hardfile, t_hardfile, sizeof(t_hardfile)) != 0)
		{
			menustate = MENU_HARDFILE_CHANGED1;
			menusub = 1;
		}
		else
		{
			menustate = MENU_MAIN1;
			menusub = 5;
		}

		break;

		// hardfile configuration has changed, ask user if he wants to use the new settings
	case MENU_HARDFILE_CHANGED1:
		menumask = 0x03;
		parentstate = menustate;
		OsdSetTitle("Confirm", 0);

		OsdWrite(0, "", 0, 0);
		OsdWrite(1, "    Changing configuration", 0, 0);
		OsdWrite(2, "      requires reset.", 0, 0);
		OsdWrite(3, "", 0, 0);
		OsdWrite(4, "       Reset Minimig?", 0, 0);
		OsdWrite(5, "", 0, 0);
		OsdWrite(6, "             yes", menusub == 0, 0);
		OsdWrite(7, "             no", menusub == 1, 0);

		for (int i = 8; i < OsdGetSize(); i++) OsdWrite(i, "", 0, 0);

		menustate = MENU_HARDFILE_CHANGED2;
		break;

	case MENU_HARDFILE_CHANGED2:
		if (select)
		{
			if (menusub == 0) // yes
			{
				// FIXME - waiting for user-confirmation increases the window of opportunity for file corruption!

				if ((config.hardfile[0].enabled != t_hardfile[0].enabled)
					|| (strcmp(config.hardfile[0].long_name, t_hardfile[0].long_name) != 0))
				{
					OpenHardfile(0);
					//					if((config.hardfile[0].enabled == HDF_FILE) && !FindRDB(0))
					//						menustate = MENU_SYNTHRDB1;
				}
				if (config.hardfile[1].enabled != t_hardfile[1].enabled
					|| (strcmp(config.hardfile[1].long_name, t_hardfile[1].long_name) != 0))
				{
					OpenHardfile(1);
					//					if((config.hardfile[1].enabled == HDF_FILE) && !FindRDB(1))
					//						menustate = MENU_SYNTHRDB2_1;
				}

				if (menustate == MENU_HARDFILE_CHANGED2)
				{
					ConfigIDE(config.enable_ide, config.hardfile[0].present && config.hardfile[0].enabled, config.hardfile[1].present && config.hardfile[1].enabled);
					OsdReset(RESET_NORMAL);

					menustate = MENU_NONE1;
				}
			}
			else if (menusub == 1) // no
			{
				memcpy(config.hardfile, t_hardfile, sizeof(t_hardfile)); // restore configuration
				menustate = MENU_MAIN1;
				menusub = 3;
			}
		}

		if (menu)
		{
			memcpy(config.hardfile, t_hardfile, sizeof(t_hardfile)); // restore configuration
			menustate = MENU_MAIN1;
			menusub = 3;
		}
		break;

	case MENU_SYNTHRDB1:
		menumask = 0x01;
		parentstate = menustate;
		OsdSetTitle("Warning", 0);
		OsdWrite(0, "", 0, 0);
		OsdWrite(1, " No partition table found -", 0, 0);
		OsdWrite(2, " Hardfile image may need", 0, 0);
		OsdWrite(3, " to be prepped with HDToolbox,", 0, 0);
		OsdWrite(4, " then formatted.", 0, 0);
		OsdWrite(5, "", 0, 0);
		OsdWrite(6, "", 0, 0);
		OsdWrite(7, "             OK", menusub == 0, 0);

		for (int i = 8; i < OsdGetSize(); i++) OsdWrite(i, "", 0, 0);

		menustate = MENU_SYNTHRDB2;
		break;


	case MENU_SYNTHRDB2_1:

		menumask = 0x01;
		parentstate = menustate;
		OsdSetTitle("Warning", 0);
		OsdWrite(0, "", 0, 0);
		OsdWrite(1, " No filesystem recognised.", 0, 0);
		OsdWrite(2, " Hardfile may need formatting", 0, 0);
		OsdWrite(3, " (or may simply be an", 0, 0);
		OsdWrite(4, " unrecognised filesystem)", 0, 0);
		OsdWrite(5, "", 0, 0);
		OsdWrite(6, "", 0, 0);
		OsdWrite(7, "             OK", menusub == 0, 0);

		for (int i = 8; i < OsdGetSize(); i++) OsdWrite(i, "", 0, 0);

		menustate = MENU_SYNTHRDB2;
		break;


	case MENU_SYNTHRDB2:
		if (select || menu)
		{
			if (menusub == 0) // OK
				menustate = MENU_SETTINGS_HARDFILE1;
		}
		break;


		/******************************************************************/
		/* video settings menu                                            */
		/******************************************************************/
	case MENU_SETTINGS_VIDEO1:
		menumask = 0xf;
		parentstate = menustate;
		helptext = 0; // helptexts[HELPTEXT_VIDEO];

		OsdSetTitle("Video", OSD_ARROW_LEFT | OSD_ARROW_RIGHT);
		OsdWrite(0, "", 0, 0);
		OsdWrite(1, "", 0, 0);
		strcpy(s, "  Scanlines     : ");
		strcat(s, config_scanlines_msg[config.scanlines & 0x3]);
		OsdWrite(2, s, menusub == 0, 0);
		strcpy(s, "  Video area by : ");
		strcat(s, config_blank_msg[(config.scanlines >> 6) & 3]);
		OsdWrite(3, s, menusub == 1, 0);
		strcpy(s, "  Aspect Ratio  : ");
		strcat(s, config_ar_msg[(config.scanlines >> 4) & 1]);
		OsdWrite(4, s, menusub == 2, 0);
		OsdWrite(5, "", 0, 0);
		OsdWrite(6, "", 0, 0);
		for (int i = 7; i < OsdGetSize() - 1; i++) OsdWrite(i, "", 0, 0);
		OsdWrite(OsdGetSize() - 1, STD_EXIT, menusub == 3, 0);

		menustate = MENU_SETTINGS_VIDEO2;
		break;

	case MENU_SETTINGS_VIDEO2:
		if (select)
		{
			if (menusub == 0)
			{
				config.scanlines = ((config.scanlines + 1) & 0x03) | (config.scanlines & 0xfc);
				if ((config.scanlines & 0x03) > 2)
					config.scanlines = config.scanlines & 0xfc;
				menustate = MENU_SETTINGS_VIDEO1;
				ConfigVideo(config.filter.hires, config.filter.lores, config.scanlines);
			}
			else if (menusub == 1)
			{
				config.scanlines &= ~0x80;
				config.scanlines ^= 0x40;
				menustate = MENU_SETTINGS_VIDEO1;
				ConfigVideo(config.filter.hires, config.filter.lores, config.scanlines);
			}
			else if (menusub == 2)
			{
				config.scanlines &= ~0x20; // reserved for auto-ar
				config.scanlines ^= 0x10;
				menustate = MENU_SETTINGS_VIDEO1;
				ConfigVideo(config.filter.hires, config.filter.lores, config.scanlines);
			}
			else if (menusub == 3)
			{
				menustate = MENU_MAIN1;
				menusub = 8;
			}
		}

		if (menu)
		{
			menustate = MENU_MAIN1;
			menusub = 8;
		}
		else if (right)
		{
			menustate = MENU_SETTINGS_CHIPSET1;
			menusub = 0;
		}
		else if (left)
		{
			menustate = MENU_SETTINGS_MEMORY1;
			menusub = 0;
		}
		break;

		/******************************************************************/
		/* rom file selected menu                                         */
		/******************************************************************/
	case MENU_ROMFILE_SELECTED:
		menusub = 1;
		menustate = MENU_ROMFILE_SELECTED1;
		// no break intended

	case MENU_ROMFILE_SELECTED1:
		menumask = 0x03;
		parentstate = menustate;
		OsdSetTitle("Confirm", 0);
		OsdWrite(0, "", 0, 0);
		OsdWrite(1, "       Reload Kickstart?", 0, 0);
		OsdWrite(2, "", 0, 0);
		OsdWrite(3, "              yes", menusub == 0, 0);
		OsdWrite(4, "              no", menusub == 1, 0);
		OsdWrite(5, "", 0, 0);
		OsdWrite(6, "", 0, 0);
		OsdWrite(7, "", 0, 0);
		for (int i = 8; i < OsdGetSize(); i++) OsdWrite(i, "", 0, 0);

		menustate = MENU_ROMFILE_SELECTED2;
		break;

	case MENU_ROMFILE_SELECTED2:

		if (select)
		{
			if (menusub == 0)
			{
				memcpy((void*)config.kickstart, SelectedPath, sizeof(config.kickstart));
				// reset bootscreen cursor position
				BootHome();
				OsdDisable();
				EnableOsd();
				spi8(OSD_CMD_RST);
				rstval = (SPI_RST_CPU | SPI_CPU_HLT);
				spi8(rstval);
				DisableOsd();
				UploadKickstart(config.kickstart);
				EnableOsd();
				spi8(OSD_CMD_RST);
				rstval = (SPI_RST_USR | SPI_RST_CPU);
				spi8(rstval);
				DisableOsd();
				EnableOsd();
				spi8(OSD_CMD_RST);
				rstval = 0;
				spi8(rstval);
				DisableOsd();

				menustate = MENU_NONE1;
			}
			else if (menusub == 1)
			{
				menustate = MENU_SETTINGS_MEMORY1;
				menusub = 2;
			}
		}

		if (menu)
		{
			menustate = MENU_SETTINGS_MEMORY1;
			menusub = 2;
		}
		break;

		/******************************************************************/
		/* firmware menu */
		/******************************************************************/
	case MENU_FIRMWARE1:
		helptext = helptexts[HELPTEXT_NONE];
		parentstate = menustate;

		menumask = 3;

		OsdSetTitle("FW & Core", 0);
		//OsdWrite(0, "", 0, 0);
		siprintf(s, "   ARM  s/w ver. %s", version + 5);
		OsdWrite(0, "", 0, 0);
		OsdWrite(1, s, 0, 0);

		if (is_menu_core())
		{
			OsdWrite(2, "", 0, 0);
			if (getStorage(0))
			{
				OsdWrite(3, "      Using USB storage", 0, 0);
				OsdWrite(5, "      Switch to SD card", menusub == 0, 0);
			}
			else
			{
				if (getStorage(1))
				{
					OsdWrite(3, " No USB found, using SD card", 0, 0);
					OsdWrite(5, "      Switch to SD card", menusub == 0, 0);
				}
				else
				{
					OsdWrite(3, "        Using SD card", 0, 0);
					OsdWrite(5, "    Switch to USB storage", menusub == 0, !isUSBMounted());
				}
			}
			OsdWrite(4, "", 0, 0);
			OsdWrite(6, "", 0, 0);
			OsdWrite(7,  "           NOTE:", 0, 0);
			OsdWrite(8,  "  USB storage takes longer", 0, 0);
			OsdWrite(9,  "     time to initialize", 0, 0);
			OsdWrite(10, "       upon cold boot.", 0, 0);
			OsdWrite(11, "  Use OSD or USER button to", 0, 0);
			OsdWrite(12, "     cancel USB waiting", 0, 0);
			OsdWrite(13, "     and use SD instead.", 0, 0);
			OsdWrite(14, "", 0, 0);
			menustate = MENU_STORAGE;
		}
		else
		{
			OsdWrite(2, "", 0, 0);
			OsdWrite(3, "", 0, 0);
			int len = strlen(OsdCoreName());
			if (len > 30) len = 30;
			int sp = (30 - len) / 2;
			s[0] = 0;
			for (int i = 0; i < sp; i++) strcat(s, " ");
			char *s2 = s + strlen(s);
			char *s3 = OsdCoreName();
			for (int i = 0; i < len; i++) *s2++ = *s3++;
			*s2++ = 0;
			OsdWrite(4, s, 0, 0);
			OsdWrite(5, "", 0, 0);
			OsdWrite(6, "      Change FPGA core", menusub == 0, 0);
			for (int i = 7; i < OsdGetSize() - 1; i++) OsdWrite(i, "", 0, 0);
			menustate = MENU_FIRMWARE2;
		}
		OsdWrite(OsdGetSize() - 1, STD_EXIT, menusub == 1, 0);
		break;

	case MENU_STORAGE:
		if (menu || ((menusub == 1) && select))
		{
			switch (user_io_core_type()) {
			case CORE_TYPE_MIST:
				menusub = 5;
				menustate = MENU_MIST_MAIN1;
				break;
			case CORE_TYPE_ARCHIE:
				menusub = 3;
				menustate = MENU_ARCHIE_MAIN1;
				break;
			default:
				menusub = 0;
				menustate = MENU_NONE1;
				break;
			}
		}
		else if (select)
		{
			if (menusub == 0)
			{
				if(getStorage(1) || isUSBMounted())
				{
					setStorage(!getStorage(1));
				}
			}
		}
		break;

	case MENU_FIRMWARE2:
		if (menu) {
			switch (user_io_core_type()) {
			case CORE_TYPE_MIST:
				menusub = 5;
				menustate = MENU_MIST_MAIN1;
				break;
			case CORE_TYPE_ARCHIE:
				menusub = 3;
				menustate = MENU_ARCHIE_MAIN1;
				break;
			default:
				menusub = 0;
				menustate = (is_menu_core()) ? MENU_NONE1 : MENU_8BIT_SYSTEM1;
				break;
			}
		}
		else if (select) {
			if (menusub == 0) {
				SelectFile("RBF", 0, MENU_FIRMWARE_CORE_FILE_SELECTED, MENU_FIRMWARE1, 0);
			}
			else if (menusub == 1) {
				switch (user_io_core_type()) {
				case CORE_TYPE_MIST:
					menusub = 5;
					menustate = MENU_MIST_MAIN1;
					break;
				case CORE_TYPE_ARCHIE:
					menusub = 3;
					menustate = MENU_ARCHIE_MAIN1;
					break;
				default:
					menusub = 0;
					menustate = (is_menu_core()) ? MENU_NONE1 : MENU_8BIT_SYSTEM1;
					break;
				}
			}
		}
		break;

	case MENU_FIRMWARE_CORE_FILE_SELECTED:
		// close OSD now as the new core may not even have one
		OsdDisable();

		fpga_load_rbf(SelectedPath);

		menustate = MENU_NONE1;
		break;

		/******************************************************************/
		/* error message menu                                             */
		/******************************************************************/
	case MENU_ERROR:
		if (menu)
			menustate = MENU_NONE1;
		break;

		/******************************************************************/
		/* popup info menu                                                */
		/******************************************************************/
	case MENU_INFO:

		if (menu)
			menustate = MENU_NONE1;
		else if (CheckTimer(menu_timer))
			menustate = MENU_NONE1;

		break;

		/******************************************************************/
		/* we should never come here                                      */
		/******************************************************************/
	default:
		break;
	}
}

void ScrollLongName(void)
{
	// this function is called periodically when file selection window is displayed
	// it checks if predefined period of time has elapsed and scrolls the name if necessary

	static int len;
	int max_len;

	len = strlen(DirItem[iSelectedEntry].d_name); // get name length
	if (DirItem[iSelectedEntry].d_type == DT_REG) // if a file
	{
		if (fs_ExtLen <= 3)
		{
			char e[5];
			memcpy(e + 1, fs_pFileExt, 3);
			if (e[3] == 0x20)
			{
				e[3] = 0;
				if (e[2] == 0x20)
				{
					e[2] = 0;
				}
			}
			e[0] = '.';
			e[4] = 0;
			int l = strlen(e);
			if ((len>l) && !strncasecmp(DirItem[iSelectedEntry].d_name + len - l, e, l)) len -= l;
		}
	}

	max_len = 30; // number of file name characters to display (one more required for scrolling)
	if (DirItem[iSelectedEntry].d_type == DT_DIR)
		max_len = 25; // number of directory name characters to display

	ScrollText(iSelectedEntry-iFirstEntry, DirItem[iSelectedEntry].d_name, 2, len, max_len, 1);
}

char* GetDiskInfo(char* lfn, long len)
{
	// extracts disk number substring form file name
	// if file name contains "X of Y" substring where X and Y are one or two digit number
	// then the number substrings are extracted and put into the temporary buffer for further processing
	// comparision is case sensitive

	short i, k;
	static char info[] = "XX/XX"; // temporary buffer
	static char template[4] = " of "; // template substring to search for
	char *ptr1, *ptr2, c;
	unsigned char cmp;

	if (len > 20) // scan only names which can't be fully displayed
	{
		for (i = (unsigned short)len - 1 - sizeof(template); i > 0; i--) // scan through the file name starting from its end
		{
			ptr1 = &lfn[i]; // current start position
			ptr2 = template;
			cmp = 0;
			for (k = 0; k < sizeof(template); k++) // scan through template
			{
				cmp |= *ptr1++ ^ *ptr2++; // compare substrings' characters one by one
				if (cmp)
					break; // stop further comparing if difference already found
			}

			if (!cmp) // match found
			{
				k = i - 1; // no need to check if k is valid since i is greater than zero

				c = lfn[k]; // get the first character to the left of the matched template substring
				if (c >= '0' && c <= '9') // check if a digit
				{
					info[1] = c; // copy to buffer
					info[0] = ' '; // clear previous character
					k--; // go to the preceding character
					if (k >= 0) // check if index is valid
					{
						c = lfn[k];
						if (c >= '0' && c <= '9') // check if a digit
							info[0] = c; // copy to buffer
					}

					k = i + sizeof(template); // get first character to the right of the mached template substring
					c = lfn[k]; // no need to check if index is valid
					if (c >= '0' && c <= '9') // check if a digit
					{
						info[3] = c; // copy to buffer
						info[4] = ' '; // clear next char
						k++; // go to the followwing character
						if (k < len) // check if index is valid
						{
							c = lfn[k];
							if (c >= '0' && c <= '9') // check if a digit
								info[4] = c; // copy to buffer
						}
						return info;
					}
				}
			}
		}
	}
	return NULL;
}

// print directory contents
void PrintDirectory(void)
{
	int k;
	int len;

	char s[40];
	s[32] = 0; // set temporary string length to OSD line length

	ScrollReset();

	for(int i = 0; i < OsdGetSize(); i++)
	{
		char leftchar = 0;
		memset(s, ' ', 32); // clear line buffer
		if (i < nDirEntries)
		{
			k = iFirstEntry + i;

			len = strlen(DirItem[k].d_name); // get name length

			if (!(DirItem[k].d_type == DT_DIR)) // if a file
			{
				if (fs_ExtLen <= 3)
				{
					char e[5];
					memcpy(e + 1, fs_pFileExt, 3);
					if (e[3] == 0x20)
					{
						e[3] = 0;
						if (e[2] == 0x20)
						{
							e[2] = 0;
						}
					}
					e[0] = '.';
					e[4] = 0;
					int l = strlen(e);
					if ((len>l) && !strncasecmp(DirItem[k].d_name + len - l, e, l))
					{
						len -= l;
					}
				}
			}

			if (len > 28)
			{
				len = 27; // trim display length if longer than 30 characters
				s[28] = 22;
			}

			strncpy(s + 1, DirItem[k].d_name, len); // display only name

			if (DirItem[k].d_type == DT_DIR) // mark directory with suffix
			{
				if (!strcmp(DirItem[k].d_name, ".."))
				{
					strcpy(&s[19], " <UP-DIR>");
				}
				else
				{
					strcpy(&s[22], " <DIR>");
				}
			}

			if (!i && k) leftchar = 17;
			if ((i == OsdGetSize() - 1) && (k < nDirEntries - 1)) leftchar = 16;
		}
		else
		{
			if (i == 0 && nDirEntries == 0) // selected directory is empty
				strcpy(s, "          No files!");
		}

		OsdWriteOffset(i, s, i == (iSelectedEntry - iFirstEntry), 0, 0, leftchar);
	}
}

void _strncpy(char* pStr1, const char* pStr2, size_t nCount)
{
	// customized strncpy() function to fill remaing destination string part with spaces

	while (*pStr2 && nCount)
	{
		*pStr1++ = *pStr2++; // copy strings
		nCount--;
	}

	while (nCount--)
		*pStr1++ = ' '; // fill remaining space with spaces
}

// insert floppy image pointed to to by global <file> into <drive>
void InsertFloppy(adfTYPE *drive, char* path)
{
	int writable = FileCanWrite(path);

	if (!FileOpenEx(&drive->file, path, writable ? O_RDWR | O_SYNC : O_RDONLY))
	{
		return;
	}

	unsigned char i, j;
	unsigned long tracks;

	// calculate number of tracks in the ADF image file
	tracks = drive->file.size / (512 * 11);
	if (tracks > MAX_TRACKS)
	{
		menu_debugf("UNSUPPORTED ADF SIZE!!! Too many tracks: %lu\n", tracks);
		tracks = MAX_TRACKS;
	}
	drive->tracks = (unsigned char)tracks;

	strcpy(drive->name, path);

	// initialize the rest of drive struct
	drive->status = DSK_INSERTED;
	if(writable) // read-only attribute
		drive->status |= DSK_WRITABLE;

	drive->sector_offset = 0;
	drive->track = 0;
	drive->track_prev = -1;

	menu_debugf("Inserting floppy: \"%s\"\n", path);
	menu_debugf("file writable: %d\n", writable);
	menu_debugf("file size: %lu (%lu KB)\n", drive->file.size, drive->file.size >> 10);
	menu_debugf("drive tracks: %u\n", drive->tracks);
	menu_debugf("drive status: 0x%02X\n", drive->status);
}

static void set_text(const char *message, unsigned char code)
{
	char s[40];
	char i = 0, l = 1;

	OsdWrite(0, "", 0, 0);

	do
	{
		s[i++] = *message;

		// line full or line break
		if ((i == 29) || (*message == '\n') || !*message)
		{

			s[i] = 0;
			OsdWrite(l++, s, 0, 0);
			i = 0;  // start next line
		}
	} while (*message++);

	if (code && (l <= 7))
	{
		siprintf(s, " Code: #%d", code);
		OsdWrite(l++, s, 0, 0);
	}

	while (l <= OsdGetSize()-1) OsdWrite(l++, "", 0, 0);
}

/*  Error Message */
void ErrorMessage(const char *message, unsigned char code)
{
	menustate = MENU_ERROR;

	OsdSetTitle("Error", 0);
	set_text(message, code);
	OsdEnable(0); // do not disable KEYBOARD
}

void InfoMessage(char *message)
{
	if (menustate != MENU_INFO)
	{
		OsdSetTitle("Message", 0);
		OsdEnable(0); // do not disable keyboard
	}

	set_text(message, 0);

	menu_timer = GetTimer(2000);
	menustate = MENU_INFO;
}

void EjectAllFloppies()
{
	char i;
	for (i = 0; i<drives; i++)
		df[i].status = 0;

	// harddisk
	config.hardfile[0].present = 0;
	config.hardfile[1].present = 0;
}