#ifndef BUILD_H
#define BUILD_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "main.h"

struct Campsite {
    bool is_partial;

    char *pkg_name;
    char *pkg_desc;
    char *pkg_ver;

    char **dependencies;
    char **sources;
    char **prepare_steps;
    char **post_install_steps;
    uint8_t num_dependencies; //never really should have more than 255 of each, i think this is safe
    uint8_t num_sources;
    uint8_t num_prepare_steps;
    uint8_t num_post_steps;
};

struct Campsite *parse_campsite(FILE *campsite_file);
void dismantle_campsite(struct Campsite *camp);

enum RetCode build_package(FILE *campsite_file);
enum RetCode gen_packagelist(char *shima_folder);

#endif
