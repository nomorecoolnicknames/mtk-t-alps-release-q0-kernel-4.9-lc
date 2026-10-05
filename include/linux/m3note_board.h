#ifndef _LINUX_M3NOTE_BOARD_H
#define _LINUX_M3NOTE_BOARD_H

enum m3note_board_id {
	M3NOTE_BOARD_UNKNOWN,
	M3NOTE_BOARD_M681,
	M3NOTE_BOARD_L681,
};

enum m3note_board_id m3note_board_id(void);

#endif
