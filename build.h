#ifndef BUILD_H
#define BUILD_H

#include <stdio.h>
#include <stdint.h>
#include "main.h"

struct Campsite {
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

enum RetCode build_package(FILE *campsite_file);
enum RetCode gen_packagelist(char *shima_folder);

#endif
