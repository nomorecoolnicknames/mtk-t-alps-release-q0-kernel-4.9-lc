#ifndef _LINUX_M3NOTE_FACTORY_H
#define _LINUX_M3NOTE_FACTORY_H

#include <linux/m3note_board.h>

struct m3note_factory_pair {
	const char *serial;
	const char *psn;
	const char *psn_key;
	enum m3note_board_id board;
};

static int m3note_factory_word(const char *s, unsigned int n, const char *word)
{
	unsigned int i;

	for (i = 0; i < n; i++)
		if (!word[i] || s[i] != word[i])
			return 0;
	return word[n] == '\0';
}

static int m3note_factory_space(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* The table admits complete captured pairs, never serial prefixes or DT IDs. */
static enum m3note_board_id m3note_factory_parse(const char *line,
	const struct m3note_factory_pair *pairs, unsigned int count,
	enum m3note_board_id selected)
{
	char serial[64] = { 0 }, psn[64] = { 0 };
	unsigned int seen_serial = 0, seen_psn = 0, matches = 0, i;
	const char *psn_key = 0;
	unsigned int psn_key_len = 0;
	enum m3note_board_id physical = M3NOTE_BOARD_UNKNOWN;

	if (!line || selected == M3NOTE_BOARD_UNKNOWN)
		return M3NOTE_BOARD_UNKNOWN;
	while (*line) {
		const char *start, *equal, *end;
		unsigned int key_len, value_len;
		char *target = 0;

		while (m3note_factory_space(*line))
			line++;
		if (!*line)
			break;
		start = line;
		while (*line && !m3note_factory_space(*line))
			line++;
		end = line;
		for (equal = start; equal < end && *equal != '='; equal++)
			;
		key_len = equal - start;
		if (m3note_factory_word(start, key_len, "androidboot.serialno")) {
			if (seen_serial++)
				return M3NOTE_BOARD_UNKNOWN;
			target = serial;
		} else if (m3note_factory_word(start, key_len,
					      "androidboot.serialno.psn") ||
			   m3note_factory_word(start, key_len, "psn")) {
			if (seen_psn++)
				return M3NOTE_BOARD_UNKNOWN;
			psn_key = key_len == 3 ? "psn" : "androidboot.serialno.psn";
			psn_key_len = key_len;
			target = psn;
		} else if (m3note_factory_word(start, key_len, "serialno") ||
			   m3note_factory_word(start, key_len, "serial") ||
			   m3note_factory_word(start, key_len, "androidboot.serialnumber") ||
			   m3note_factory_word(start, key_len, "androidboot.serial_no") ||
			   m3note_factory_word(start, key_len, "androidboot.serial") ||
			   m3note_factory_word(start, key_len, "androidboot.serialnum") ||
			   m3note_factory_word(start, key_len, "androidboot.psn") ||
			   m3note_factory_word(start, key_len, "androidboot.serialno_psn")) {
			return M3NOTE_BOARD_UNKNOWN;
		}
		if (!target)
			continue;
		if (equal == end)
			return M3NOTE_BOARD_UNKNOWN;
		value_len = end - equal - 1;
		if (!value_len || value_len >= sizeof(serial))
			return M3NOTE_BOARD_UNKNOWN;
		for (i = 0; i < value_len; i++) {
			char c = equal[i + 1];
			if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
				return M3NOTE_BOARD_UNKNOWN;
			target[i] = c;
		}
	}
	if (seen_serial != 1 || seen_psn != 1)
		return M3NOTE_BOARD_UNKNOWN;
	for (i = 0; i < count; i++) {
		unsigned int s = 0, p = 0;

		while (serial[s] && serial[s] == pairs[i].serial[s])
			s++;
		while (psn[p] && psn[p] == pairs[i].psn[p])
			p++;
		if (!serial[s] && !pairs[i].serial[s] &&
		    !psn[p] && !pairs[i].psn[p] &&
		    m3note_factory_word(psn_key,
			psn_key_len, pairs[i].psn_key)) {
			physical = pairs[i].board;
			matches++;
		}
	}
	return matches == 1 && physical == selected ? physical :
		M3NOTE_BOARD_UNKNOWN;
}
#endif
