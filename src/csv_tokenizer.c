#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "debug.h"
#include "csv_tokenizer.h"


struct csv_tokenizer {
	const char* restrict buffer;
	Cell* restrict cells;
	Cell const* restrict cells_end;

	char separator;

	// tokenizer state
	bool prev_newline;
	bool prev_cell;

	bool prev_quote;
	bool in_quote;
};

struct csv_tokenizer* setup_tokenizer(char separator, const char* restrict buffer, Cell* restrict cells, size_t cell_size) {
	struct csv_tokenizer* tokenizer = malloc(sizeof(struct csv_tokenizer));
	tokenizer->separator = separator;
	tokenizer->buffer = buffer;
	tokenizer->cells = cells;
	tokenizer->cells_end = cells + cell_size - 2; // two room at the end

	tokenizer->prev_newline = false;

	tokenizer->prev_quote = false;
	tokenizer->prev_cell = false;
	tokenizer->in_quote = false;
	return tokenizer;
}

void free_tokenizer(struct csv_tokenizer* restrict tokenizer) {
	free(tokenizer);
}

void tokenize_cells(struct csv_tokenizer* restrict tokenizer, size_t buffer_offset, size_t buffer_read, size_t* restrict buffer_consumed, size_t* restrict cells_found, bool* restrict last_full) {
	const char* restrict current_char = tokenizer->buffer + buffer_offset;
	const char* restrict char_end = tokenizer->buffer + buffer_read;
	const char* restrict char_end4 = tokenizer->buffer + buffer_read - 4;
	const char* restrict current_start = current_char;

	Cell* restrict cell = tokenizer->cells;
	LOG_V("tokenizer-start\t%d, %d, %d, %d %c (%lu)\n", tokenizer->prev_quote, tokenizer->in_quote, tokenizer->prev_newline, tokenizer->prev_cell, *current_char, buffer_offset );

	*last_full = true;
	if (tokenizer->prev_quote) {
		tokenizer->prev_quote = false;
		tokenizer->in_quote = false;
		if (*current_char == '"') {
			// escaped quote so we don't have to decrease the first char
			goto IN_QUOTE;
		}
		current_char--; // jump back, since starts with increment
		goto AFTER_QUOTE;
	}
	if (tokenizer->in_quote) {
		tokenizer->in_quote = false;
		current_char--; // jump back, since the loops starts with increment
		goto IN_QUOTE;
	}
	else if (tokenizer->prev_newline) {
		tokenizer->prev_newline = false;
		if ((*current_char == '\n' || *current_char == '\r')) {
			while (++current_char < char_end && (*current_char == '\n' || *current_char == '\r'));
		}
	}
	else if (tokenizer->prev_cell) {
		tokenizer->prev_cell = false;
		current_char--; // jump back, since the loops starts with increment
		goto NORMAL_CELL;
	}
	else if (*current_char == tokenizer->separator) {
		cell->start = current_start;
		cell->length = 0;
		cell++;
		current_char++;
		current_start = current_char;
	}

	while (current_char < char_end) {
		if (*current_char == '"') {
IN_QUOTE:
			while (++current_char < char_end) {
				if (*current_char == '"') {
					const char* peek = current_char + 1;
					if (peek == char_end) {
						// at the end of stream and not sure if escaped or not
						tokenizer->prev_quote = true;
						*last_full = false;
						break;
					}
					else if (*peek == '"') {
						current_char++;
						continue;
					}
					else {
						break;
					}
				}
			}
AFTER_QUOTE:
			if (current_char != char_end) {
				current_char++;
			}
			cell->start = current_start;
			cell->length = (size_t)((current_char)-current_start);
			cell++;

			if (current_char == char_end) {
				if (*(current_char-1) != '"' || *(current_char-2) == '"' ||  current_char - 1 == current_start) {
					tokenizer->in_quote = true;
					*last_full = false;
					break;
				}
				*last_full = false; // is this correct? does it ever happen?
				break;
			}
			
			if (*current_char == '\n' || *current_char == '\r') {
				cell->start = NULL;
				cell->length = -1;
				cell++;
				// consume newline
				while (++current_char < char_end && (*current_char == '\n' || *current_char == '\r'));
				if (current_char == char_end) {
					// we stopped inside a new_line
					tokenizer->prev_newline = true;
					break;
				}
				current_start = current_char;
			}
			else if (*current_char == tokenizer->separator) {
				current_char++;
				current_start = current_char;
			}
			else {
				fprintf(stderr, "Invalid character: \"%c (\\%d)\" found after end of cell\n",*current_char, *current_char);
				exit(1);
				return;
			}
			if (cell >= tokenizer->cells_end) {
				break;
			}
		}
		else if (*current_char == tokenizer->separator) {
			// an empty cell somewhere in the middle
			cell->start = current_start;
			cell->length = 0;
			cell++;
			current_start = ++current_char;
			if (cell >= tokenizer->cells_end) {
				break;
			}
		}
		else if (*current_char == '\n' || *current_char == '\r') {
			// an newline means that we had an empty cell as last cell of the
			// row
			cell->start = current_start;
			cell->length = 0;
			cell++;

			cell->start = NULL;
			cell->length = -1;
			cell++;
			// consume newline
			while (++current_char < char_end && (*current_char == '\n' || *current_char == '\r'));
			if (current_char == char_end) {
				// we stopped inside a new_line
				tokenizer->prev_newline = true;
				break;
			}
			current_start = current_char;
			if (cell >= tokenizer->cells_end) {
				break;
			}
		}
		else {
			// start of a new field
NORMAL_CELL:;
			char sep = tokenizer->separator;
			while (current_char < char_end4) {
				if (current_char[1] == sep ||current_char[1] == '\n' || current_char[1] == '\r')  {
					current_char += 1;
					goto FOUND_CELL_END;
				}
				if (current_char[2] == sep ||current_char[2] == '\n' || current_char[2] == '\r')  {
					current_char += 2;
					goto FOUND_CELL_END;
				}
				if (current_char[3] == sep ||current_char[3] == '\n' || current_char[3] == '\r')  {
					current_char += 3;
					goto FOUND_CELL_END;
				}
				if (current_char[4] == sep ||current_char[4] == '\n' || current_char[4] == '\r')  {
					current_char += 4;
					goto FOUND_CELL_END;
				}
				current_char += 4;
			}
			while (++current_char < char_end &&	*current_char != tokenizer->separator && *current_char != '\n' && *current_char != '\r');
FOUND_CELL_END:
			cell->start = current_start;
			cell->length = (size_t)((current_char)-current_start);
			cell++;

			if (current_char == char_end) {
				if (*(current_char-1) != tokenizer->separator) {
					tokenizer->prev_cell = true;
					*last_full = false;
					break;
				}
			}
			else if (*current_char == '\n' || *current_char == '\r') {
				cell->start = NULL;
				cell->length = -1;
				cell++;
				// consume newline
				while (++current_char < char_end && (*current_char == '\n' || *current_char == '\r'));
				if (current_char == char_end) {
					// we stopped inside a new_line
					tokenizer->prev_newline = true;
					break;
				}
				current_start = current_char;
			}
			else if (*current_char == tokenizer->separator) {
				current_char++;
				current_start = current_char;
			}
			else {
				fprintf(stderr, "Invalid character-2: \"%c\" found after end of cell\n",*current_char);
				exit(1);
				return;
			}
			if (cell >= tokenizer->cells_end) {
				break;
			}
		}
	}
	*buffer_consumed = (size_t)(current_char - tokenizer->buffer);
	*cells_found = (size_t)(cell - tokenizer->cells);

	LOG_V("tokenizer-done\t%d, %d, %d, %d %c (%lu) %d\n", tokenizer->prev_quote, tokenizer->in_quote, tokenizer->prev_newline, tokenizer->prev_cell, *(current_char-1), *buffer_consumed  , *last_full);
}