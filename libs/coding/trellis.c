/*
 * This file is part of dmrshark.
 *
 * dmrshark is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * dmrshark is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with dmrshark.  If not, see <http://www.gnu.org/licenses/>.
**/

#include DEFAULTCONFIG

#include "trellis.h"

#include <libs/daemon/console.h>

#include <stdlib.h>

static uint8_t trellis_dibit_interleave_matrix[] = { // See DMR AI protocol spec. page 130.
	0,	1,	8,	9,	16,	17,	24,	25,	32,	33,	40,	41,	48,	49,	56,	57,	64,	65,	72,	73,	80,	81,	88,	89,	96,	97,
	2,	3,	10,	11,	18,	19,	26,	27,	34,	35,	42,	43,	50,	51,	58,	59,	66,	67,	74,	75,	82,	83,	90,	91,
	4,	5,	12,	13,	20,	21,	28,	29,	36,	37,	44,	45,	52,	53,	60,	61,	68,	69,	76,	77,	84,	85,	92,	93,
	6,	7,	14,	15,	22,	23,	30,	31,	38,	39,	46,	47,	54,	55,	62,	63,	70,	71,	78,	79,	86,	87,	94,	95
};

static uint8_t trellis_trellis_encoder_state_transition_table[] = { // See DMR AI protocol spec. page 129.
	0,	8,	4,	12,	2,	10,	6,	14,
	4,	12,	2,	10,	6,	14,	0,	8,
	1,	9,	5,	13,	3,	11,	7,	15,
	5,	13,	3,	11,	7,	15,	1,	9,
	3,	11,	7,	15,	1,	9,	5,	13,
	7,	15,	1,	9,	5,	13,	3,	11,
	2,	10,	6,	14,	0,	8,	4,	12,
	6,	14,	0,	8,	4,	12,	2,	10
};

trellis_dibits_t *trellis_extract_dibits(dmrpacket_payload_info_bits_t *info_bits) {
	static trellis_dibits_t dibits;
	loglevel_t loglevel = console_get_loglevel();
	int i;

	if (info_bits == NULL)
		return NULL;

	if (loglevel.flags.dmrdata && loglevel.flags.debug) {
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "trellis: extracting dibits\n");
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "  input: ");
		for (i = 0; i < 196; i += 2)
			console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "%u%u ", info_bits->bits[i], info_bits->bits[i+1]);
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "\n");
	}

	for (i = 0; i < 196; i += 2) {
		// Calculate the dibits from the 4FSK symbol mapping, see DMR AI protocol spec. page 111.
		if (info_bits->bits[i] == 0 && info_bits->bits[i+1] == 1)
			dibits.dibits[i/2] = 3;
		else if (info_bits->bits[i] == 0 && info_bits->bits[i+1] == 0)
			dibits.dibits[i/2] = 1;
		else if (info_bits->bits[i] == 1 && info_bits->bits[i+1] == 0)
			dibits.dibits[i/2] = -1;
		else if (info_bits->bits[i] == 1 && info_bits->bits[i+1] == 1)
			dibits.dibits[i/2] = -3;
	}

	if (loglevel.flags.dmrdata && loglevel.flags.debug) {
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "  output: ");
		for (i = 0; i < 98; i++)
			console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "%d ", dibits.dibits[i]);
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "\n");
	}

	return &dibits;
}

dmrpacket_payload_info_bits_t *trellis_construct_payload_info_bits(trellis_dibits_t *dibits) {
	static dmrpacket_payload_info_bits_t info_bits;
	loglevel_t loglevel = console_get_loglevel();
	int i;

	if (dibits == NULL)
		return NULL;

	if (loglevel.flags.dmrdata && loglevel.flags.debug) {
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "trellis: constructing payload info bits from dibits\n");
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "  input: ");
		for (i = 0; i < sizeof(trellis_dibits_t); i++)
			console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "%d ", dibits->dibits[i]);
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "\n");
	}

	for (i = 0; i < sizeof(trellis_dibits_t); i++) {
		switch (dibits->dibits[i]) {
			case +3: info_bits.bits[i*2] = 0; info_bits.bits[i*2+1] = 1; break;
			case +1: info_bits.bits[i*2] = 0; info_bits.bits[i*2+1] = 0; break;
			case -1: info_bits.bits[i*2] = 1; info_bits.bits[i*2+1] = 0; break;
			case -3: info_bits.bits[i*2] = 1; info_bits.bits[i*2+1] = 1; break;
			default: break;
		}
	}

	if (loglevel.flags.dmrdata && loglevel.flags.debug) {
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "  output: ");
		for (i = 0; i < sizeof(dmrpacket_payload_info_bits_t); i += 2)
			console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "%u%u ", info_bits.bits[i], info_bits.bits[i+1]);
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "\n");
	}

	return &info_bits;
}

trellis_dibits_t *trellis_deinterleave_dibits(trellis_dibits_t *dibits) {
	static trellis_dibits_t deinterleaved_dibits;
	loglevel_t loglevel = console_get_loglevel();
	int i;

	if (dibits == NULL)
		return NULL;

	if (loglevel.flags.dmrdata && loglevel.flags.debug) {
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "trellis: deinterleaving dibits\n");
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "  input: ");
		for (i = 0; i < 98; i++)
			console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "%d ", dibits->dibits[i]);
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "\n");
	}

	for (i = 0; i < 98; i++)
		deinterleaved_dibits.dibits[trellis_dibit_interleave_matrix[i]] = dibits->dibits[i];

	if (loglevel.flags.dmrdata && loglevel.flags.debug) {
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "  output: ");
		for (i = 0; i < 98; i++)
			console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "%d ", deinterleaved_dibits.dibits[i]);
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "\n");
	}

	return &deinterleaved_dibits;
}

trellis_dibits_t *trellis_interleave_dibits(trellis_dibits_t *dibits) {
	static trellis_dibits_t interleaved_dibits;
	loglevel_t loglevel = console_get_loglevel();
	int i;

	if (dibits == NULL)
		return NULL;

	if (loglevel.flags.dmrdata && loglevel.flags.debug) {
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "trellis: interleaving dibits\n");
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "  input: ");
		for (i = 0; i < sizeof(trellis_dibits_t); i++)
			console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "%d ", dibits->dibits[i]);
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "\n");
	}

	for (i = 0; i < 98; i++)
		interleaved_dibits.dibits[i] = dibits->dibits[trellis_dibit_interleave_matrix[i]];

	if (loglevel.flags.dmrdata && loglevel.flags.debug) {
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "  output: ");
		for (i = 0; i < sizeof(trellis_dibits_t); i++)
			console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "%d ", interleaved_dibits.dibits[i]);
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "\n");
	}

	return &interleaved_dibits;
}

trellis_constellationpoints_t *trellis_getconstellationpoints(trellis_dibits_t *deinterleaved_dibits) {
	static trellis_constellationpoints_t constellationpoints;
	loglevel_t loglevel = console_get_loglevel();
	int i;

	if (deinterleaved_dibits == NULL)
		return NULL;

	if (loglevel.flags.dmrdata && loglevel.flags.debug) {
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "trellis: calculating constellation points from dibits\n");
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "  input: ");
		for (i = 0; i < 98; i++)
			console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "%d ", deinterleaved_dibits->dibits[i]);
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "\n");
	}

	for (i = 0; i < 98; i += 2) {
		if		(deinterleaved_dibits->dibits[i] == +1 && deinterleaved_dibits->dibits[i+1] == -1) constellationpoints.points[i/2] = 0;
		else if	(deinterleaved_dibits->dibits[i] == -1 && deinterleaved_dibits->dibits[i+1] == -1) constellationpoints.points[i/2] = 1;
		else if	(deinterleaved_dibits->dibits[i] == +3 && deinterleaved_dibits->dibits[i+1] == -3) constellationpoints.points[i/2] = 2;
		else if	(deinterleaved_dibits->dibits[i] == -3 && deinterleaved_dibits->dibits[i+1] == -3) constellationpoints.points[i/2] = 3;
		else if	(deinterleaved_dibits->dibits[i] == -3 && deinterleaved_dibits->dibits[i+1] == -1) constellationpoints.points[i/2] = 4;
		else if	(deinterleaved_dibits->dibits[i] == +3 && deinterleaved_dibits->dibits[i+1] == -1) constellationpoints.points[i/2] = 5;
		else if	(deinterleaved_dibits->dibits[i] == -1 && deinterleaved_dibits->dibits[i+1] == -3) constellationpoints.points[i/2] = 6;
		else if	(deinterleaved_dibits->dibits[i] == +1 && deinterleaved_dibits->dibits[i+1] == -3) constellationpoints.points[i/2] = 7;
		else if	(deinterleaved_dibits->dibits[i] == -3 && deinterleaved_dibits->dibits[i+1] == +3) constellationpoints.points[i/2] = 8;
		else if	(deinterleaved_dibits->dibits[i] == +3 && deinterleaved_dibits->dibits[i+1] == +3) constellationpoints.points[i/2] = 9;
		else if	(deinterleaved_dibits->dibits[i] == -1 && deinterleaved_dibits->dibits[i+1] == +1) constellationpoints.points[i/2] = 10;
		else if	(deinterleaved_dibits->dibits[i] == +1 && deinterleaved_dibits->dibits[i+1] == +1) constellationpoints.points[i/2] = 11;
		else if	(deinterleaved_dibits->dibits[i] == +1 && deinterleaved_dibits->dibits[i+1] == +3) constellationpoints.points[i/2] = 12;
		else if	(deinterleaved_dibits->dibits[i] == -1 && deinterleaved_dibits->dibits[i+1] == +3) constellationpoints.points[i/2] = 13;
		else if	(deinterleaved_dibits->dibits[i] == +3 && deinterleaved_dibits->dibits[i+1] == +1) constellationpoints.points[i/2] = 14;
		else if	(deinterleaved_dibits->dibits[i] == -3 && deinterleaved_dibits->dibits[i+1] == +1) constellationpoints.points[i/2] = 15;
	}

	if (loglevel.flags.dmrdata && loglevel.flags.debug) {
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "  output: ");
		for (i = 0; i < 49; i++)
			console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "%u ", constellationpoints.points[i]);
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "\n");
	}

	return &constellationpoints;
}

trellis_dibits_t *trellis_construct_deinterleaved_dibits(trellis_constellationpoints_t *constellationpoints) {
	static trellis_dibits_t deinterleaved_dibits;
	loglevel_t loglevel = console_get_loglevel();
	int i;

	if (constellationpoints == NULL)
		return NULL;

	if (loglevel.flags.dmrdata && loglevel.flags.debug) {
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "trellis: constructing dibits from constellation points\n");
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "  input: ");
		for (i = 0; i < sizeof(trellis_constellationpoints_t); i++)
			console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "%u ", constellationpoints->points[i]);
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "\n");
	}

	for (i = 0; i < sizeof(trellis_constellationpoints_t); i++) {
		switch (constellationpoints->points[i]) {
			case 0: deinterleaved_dibits.dibits[i*2]  = +1; deinterleaved_dibits.dibits[i*2+1] = -1; break;
			case 1: deinterleaved_dibits.dibits[i*2]  = -1; deinterleaved_dibits.dibits[i*2+1] = -1; break;
			case 2: deinterleaved_dibits.dibits[i*2]  = +3; deinterleaved_dibits.dibits[i*2+1] = -3; break;
			case 3: deinterleaved_dibits.dibits[i*2]  = -3; deinterleaved_dibits.dibits[i*2+1] = -3; break;
			case 4: deinterleaved_dibits.dibits[i*2]  = -3; deinterleaved_dibits.dibits[i*2+1] = -1; break;
			case 5: deinterleaved_dibits.dibits[i*2]  = +3; deinterleaved_dibits.dibits[i*2+1] = -1; break;
			case 6: deinterleaved_dibits.dibits[i*2]  = -1; deinterleaved_dibits.dibits[i*2+1] = -3; break;
			case 7: deinterleaved_dibits.dibits[i*2]  = +1; deinterleaved_dibits.dibits[i*2+1] = -3; break;
			case 8: deinterleaved_dibits.dibits[i*2]  = -3; deinterleaved_dibits.dibits[i*2+1] = +3; break;
			case 9: deinterleaved_dibits.dibits[i*2]  = +3; deinterleaved_dibits.dibits[i*2+1] = +3; break;
			case 10: deinterleaved_dibits.dibits[i*2] = -1; deinterleaved_dibits.dibits[i*2+1] = +1; break;
			case 11: deinterleaved_dibits.dibits[i*2] = +1; deinterleaved_dibits.dibits[i*2+1] = +1; break;
			case 12: deinterleaved_dibits.dibits[i*2] = +1; deinterleaved_dibits.dibits[i*2+1] = +3; break;
			case 13: deinterleaved_dibits.dibits[i*2] = -1; deinterleaved_dibits.dibits[i*2+1] = +3; break;
			case 14: deinterleaved_dibits.dibits[i*2] = +3; deinterleaved_dibits.dibits[i*2+1] = +1; break;
			case 15: deinterleaved_dibits.dibits[i*2] = -3; deinterleaved_dibits.dibits[i*2+1] = +1; break;
			default: break;
		}
	}

	if (loglevel.flags.dmrdata && loglevel.flags.debug) {
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "  output: ");
		for (i = 0; i < sizeof(trellis_dibits_t); i++)
			console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "%d ", deinterleaved_dibits.dibits[i]);
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "\n");
	}

	return &deinterleaved_dibits;
}

trellis_tribits_t *trellis_extract_tribits(trellis_constellationpoints_t *constellationpoints) {
	static trellis_tribits_t tribits;
	int i, j, row_start;
	flag_t match;
	trellis_tribit_t last_state = 0;
	loglevel_t loglevel = console_get_loglevel();

	if (constellationpoints == NULL)
		return NULL;

	if (loglevel.flags.dmrdata && loglevel.flags.debug) {
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "trellis: extracting tribits from constellation points\n");
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "  input: ");
		for (i = 0; i < 49; i++)
			console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "%u ", constellationpoints->points[i]);
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "\n");
	}

	for (i = 0; i < 49; i++) {
		row_start = last_state*8;
		match = 0;
		for (j = row_start; j < row_start+8; j++) {
			// Check if this constellation point matches an element of this row of the state table.
			if (constellationpoints->points[i] == trellis_trellis_encoder_state_transition_table[j]) {
				match = 1;
				last_state = j-row_start;
				tribits.tribits[i] = last_state;
			}
		}

		// If no match found then we have a problem.
		if (match == 0) {
			console_log("trellis: trellis tribit extract error, data is corrupted\n");
			return NULL;
		}
	}

	if (loglevel.flags.dmrdata && loglevel.flags.debug) {
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "  output: ");
		for (i = 0; i < 48; i++)
			console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "%u ", tribits.tribits[i]);
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "\n");
	}

	return &tribits;
}

trellis_constellationpoints_t *trellis_construct_constellationpoints(trellis_tribits_t *tribits) {
	static trellis_constellationpoints_t constellationpoints;
	int i, row_start;
	trellis_tribit_t last_state = 0;
	loglevel_t loglevel = console_get_loglevel();

	if (tribits == NULL)
		return NULL;

	if (loglevel.flags.dmrdata && loglevel.flags.debug) {
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "trellis: constructing constellation points from tribits\n");
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "  input: ");
		for (i = 0; i < sizeof(trellis_tribits_t); i++)
			console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "%u ", tribits->tribits[i]);
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "\n");
	}

	for (i = 0; i < sizeof(trellis_tribits_t); i++) {
		row_start = last_state*8;
		constellationpoints.points[i] = trellis_trellis_encoder_state_transition_table[row_start+tribits->tribits[i]];
		last_state = tribits->tribits[i];
	}
	constellationpoints.points[i] = trellis_trellis_encoder_state_transition_table[last_state*8];

	if (loglevel.flags.dmrdata && loglevel.flags.debug) {
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "  output: ");
		for (i = 0; i < sizeof(trellis_constellationpoints_t); i++)
			console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "%u ", constellationpoints.points[i]);
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "\n");
	}

	return &constellationpoints;
}

dmrpacket_data_binary_t *trellis_extract_binary(trellis_tribits_t *tribits) {
	static dmrpacket_data_binary_t binary;
	int i;
	loglevel_t loglevel = console_get_loglevel();

	if (tribits == NULL)
		return NULL;

	if (loglevel.flags.dmrdata && loglevel.flags.debug) {
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "trellis: extracting binary data from tribits\n");
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "  input: ");
		for (i = 0; i < 48; i++)
			console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "%u ", tribits->tribits[i]);
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "\n");
	}

	for (i = 0; i < 144; i += 3) {
		binary.bits[i] =	((tribits->tribits[i/3] & 0b100) > 0);
		binary.bits[i+1] =	((tribits->tribits[i/3] & 0b010) > 0);
		binary.bits[i+2] =	((tribits->tribits[i/3] & 0b001) > 0);
	}

	if (loglevel.flags.dmrdata && loglevel.flags.debug) {
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "  output: ");
		for (i = 0; i < 144; i += 3)
			console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "%u%u%u ", binary.bits[i], binary.bits[i+1], binary.bits[i+2]);
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "\n");
	}

	return &binary;
}

trellis_tribits_t *trellis_construct_tribits(dmrpacket_data_binary_t *binary) {
	static trellis_tribits_t tribits;
	int i;
	loglevel_t loglevel = console_get_loglevel();

	if (binary == NULL)
		return NULL;

	if (loglevel.flags.dmrdata && loglevel.flags.debug) {
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "trellis: constructing tribits from binary data\n");
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "  input: ");
		for (i = 0; i < sizeof(dmrpacket_data_binary_t); i += 3)
			console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "%u%u%u ", binary->bits[i], binary->bits[i+1], binary->bits[i+2]);
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "\n");
	}

	for (i = 0; i < sizeof(dmrpacket_data_binary_t); i += 3) {
		tribits.tribits[i/3] =	(binary->bits[i] == 1) << 2 |
								(binary->bits[i+1] == 1) << 1 |
								(binary->bits[i+2] == 1);
	}

	if (loglevel.flags.dmrdata && loglevel.flags.debug) {
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "  output: ");
		for (i = 0; i < sizeof(trellis_tribits_t); i++)
			console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "%u ", tribits.tribits[i]);
		console_log(LOGLEVEL_CODING LOGLEVEL_DEBUG "\n");
	}

	return &tribits;
}
