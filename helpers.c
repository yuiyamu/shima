#include "helpers.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "heliotrope.h"

void move_to_matching_string(char *str_match, FILE *file, char **matching_line) {
    //make sure we go to the start each time!! we could be anywhere
    fseek(file, 0, SEEK_SET);

    char line[2048] = {0};
    while (fgets(line, sizeof(line), file) != NULL) {
        //this should skip all lines but the starting ones we need, so we can loop through the packages we want now~
        if (line[0] == '#' || line[0] == '\n' || line[0] == ' ') continue;
        if (strncmp(str_match, line, strlen(str_match)) == 0) break;
    }

    if (matching_line != NULL) {
        *matching_line = helio_strdup(line);
    }
}

//taken straight from yuiedit :p
void get_colon_parsed_string(const char *original, char **storage) {
    char *sub = strchr(original, ':') + 1;
    if (sub[0] == ' ') {
        sub++;
    }

    size_t copy_len = strlen(sub);
    if (sub[copy_len - 1] == '\n') {
        sub[copy_len - 1] = '\0';
    } else if (sub[copy_len - 2] == '\r' && sub[copy_len - 1] == '\n') {
        sub[copy_len - 2] = '\0';
    }

    *storage = helio_strdup(sub);
}

char *prepare_extracted_string(char *line) {
    char *prep_str = helio_strdup(line);
    int line_len = strlen(prep_str);
    prep_str[line_len - 1] = '\0';
    return prep_str;
}

void free_strarr(char ***strarr, int num_elements) {
    if (strarr == NULL || *strarr == NULL) return; //already nulled out yo >_<

    char **cur_str = *strarr;
    if (num_elements > 0) { //if we're actually given this :0 otherwise assume null term array
        for (int i = 0; i < num_elements; i++) {
            free(*cur_str);
            *cur_str = NULL;
            cur_str++;
        }
    } else {
        for (; *cur_str != NULL; cur_str++) {
            free(*cur_str);
            *cur_str = NULL;
        }
    }

    free(*strarr);
    *strarr = NULL;
}

//also stole this one from yuiedit :p
char *read_str_with_len(FILE *file) { //same thing as above, with frwite tho~
    uint8_t str_len;
    char *new_string;

    size_t read = fread(&str_len, 1, 1, file);
    if (read != 1) return NULL;

    new_string = safe_alloc(NULL, str_len + 1);
    read = fread(new_string, 1, str_len, file); //now we can read the string safely~
    if (read != str_len) return NULL;
    new_string[str_len] = '\0';

    return new_string;
}

int write_str_with_len(char *string, FILE *file) {
    uint8_t cur_str_len = strlen(string);
    size_t written = fwrite(&cur_str_len, 1, 1, file);
    if (written != 1) return 1;

    written = fwrite(string, 1, cur_str_len, file);
    if (written != cur_str_len) return 1;

    return 0;
}
