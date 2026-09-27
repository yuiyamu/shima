#ifndef INSTALL_H
#define INSTALL_H

#include <stdlib.h>
#include <stdbool.h>
#include "main.h"
#include "build.h"

struct InstallPkg {
    char *pkg_name;
    char *pkg_ver;
    char *pkg_location;
    char **post_install_hooks;
    bool is_dep;
};

struct Campsite *parse_shima_package(char *file_path);
enum RetCode install_provided_packages(char **local_packages, char **db_packages, bool force);

#endif
