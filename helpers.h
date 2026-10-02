#ifndef HELPERS_H
#define HELPERS_H

#include <stdlib.h>
#include <stdio.h>

//can leave matching_line NULL
void move_to_matching_string(char *str_match, FILE *file, char **matching_line);
void get_colon_parsed_string(const char *original, char **storage);
char *prepare_extracted_string(char *line);
void free_strarr(char ***strarr, int num_elements);

//both of these are strictly uint8_t strings, so one byte to tell len (256 max length)
char *read_str_with_len(FILE *file);
int write_str_with_len(char *string, FILE *file);

#endif