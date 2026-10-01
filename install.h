#ifndef INSTALL_H
#define INSTALL_H

#include <stdlib.h>
#include <stdbool.h>
#include "main.h"
#include "build.h"

//used when rewriting install.list
#define SHIFT_BUF_SIZE 32768

struct InstallPkg {
    char *pkg_name;
    char *pkg_ver;
    char *pkg_location;
    char **post_install_hooks;
    char **pkg_files;
    bool is_dep;
};

struct Campsite *parse_shima_package(char *file_path);
enum RetCode install_provided_packages(char **local_packages, char **db_packages, bool force);
enum RetCode delete_provided_packages(char **local_packages, char **db_packages);

#endif
