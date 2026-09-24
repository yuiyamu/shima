#include "main.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "build.h"
#include "install.h"
#include "heliotrope.h"

static void print_help(void) {
    printf("shima package manager, version %s\n\n", VERSION);
    printf("build [file/folder]       builds a package (or folder of packages) with the specified path to a campsite file\n");
    printf("install [name/file]       installs package(s) with the name (searches local db) or a local file\n");
    printf("remove [name]             removes the named package(s) if it's installed on the system\n");
    printf("update                    pulls the latest package list from your configured server\n");
    printf("gen-pkglist [folder]      generates a pkglist.shmdb for a folder of campsites (useful for making repos)\n");
    printf("help                      prints this message~\n");
}

int main(int argc, char **argv) {
    if (argc <= 1) {
        printf("shima package manager, version %s\n\nuse \"shima help\" for more information.\n", VERSION);
        return 0;
    } else if (argc > 1) {
        if (strcmp(argv[1], "build") == 0) { //ok, we're building~ we need to check for the next argument for our campsite file
            if (argc == 3) {
                //first, do we actually have a dir? :0
                if (helio_dir_exists(argv[2])) {
                    //yeah!! we should build everything in here
                    char **dir_list = helio_list_dir(argv[2], false);
                    chdir(argv[2]);
                    printf("!! building everything in folder %s...\n", argv[2]);
                    bool build_errors = false;
                    for (int i = 0; dir_list[i] != NULL; i++) {
                        if (strncmp(strrchr(dir_list[i], '.'), ".campsite", strlen(".campsite")) == 0) { //at least has .campsite, let's try it~
                            printf("entering %s...\n", dir_list[i]);
                            FILE *cur_campsite = fopen(dir_list[i], "rb");
                            if (!cur_campsite) {
                                fprintf(stderr, "!! an error occured opening campsite file %s, continuing...\n", dir_list[i]);
                                build_errors = true;
                            }

                            enum RetCode build_ret = build_package(cur_campsite); //this fcloses for us :p
                            if (build_ret != SHM_SUCCESS) {
                                fprintf(stderr, "!! an error occured building %s, continuing...\n", dir_list[i]);
                                build_errors = true;
                            }
                        }
                        free(dir_list[i]);
                    }
                    free(dir_list);

                    if (build_errors) {
                        fprintf(stderr, "!! errors occured building some packages.\n");
                        return SHM_BULK_BUILD_ERRORS;
                    } else {
                        printf("!! successfully built all packages !!\n");
                        return SHM_SUCCESS;
                    }
                } else {
                    //let's try and open the file, and if it's invalid we exit >.<
                    FILE *campsite = fopen(argv[2], "rb");
                    if (!campsite) {
                        fprintf(stderr, "unable to open provided campsite file \"%s\".\n", argv[2]);
                        return SHM_INVALID_FILE;
                    } else {
                        return build_package(campsite);
                    }
                }
            } else goto bad_args;
        } else if (strcmp(argv[1], "install") == 0) { //install, also need to see what package we're installing.
            //if we have a local package (./ or some sort of path), then we find that file!
            //otherwise, we need to go digging in the db for it >.<
            if (argc >= 3) {
                //we could have any number of args after this, for any number of packages >:3
                char **local_packages = NULL;
                char **db_packages = NULL;
                unsigned int num_local = 0;
                unsigned int num_db = 0;
                for (int i = 2; i < argc; i++) {
                    if (argv[i][0] == '.' && argv[i][1] == '/') { //local file, so we should find it right here >.<
                        FILE *test_local_file = fopen(argv[i], "rb");
                        if (!test_local_file) {
                            fprintf(stderr, "!! could not find local file %s to install from.\n", argv[i]);
                            return SHM_UNKNOWN_PACKAGE;
                        }
                        fclose(test_local_file);

                        //ok, this file does exist~ let's add it to what we want to try to install
                        local_packages = safe_alloc(local_packages, sizeof(char *) * (num_local + 1));
                        local_packages[num_local] = strdup(argv[i]);
                        num_local++;
                    } else { //this is something we should find in our db presumably~
                        db_packages = safe_alloc(db_packages, sizeof(char *) * (num_db + 1));
                        db_packages[num_db] = strdup(argv[i]);
                        num_db++;
                    }
                }

                //all the packages are added here, so toss them over to parse/look in db for~
                local_packages = safe_alloc(local_packages, sizeof(char *) * (num_local + 1));
                local_packages[num_local] = NULL; //also null terminate before sending them in :p
                db_packages = safe_alloc(db_packages, sizeof(char *) * (num_db + 1));
                db_packages[num_db] = NULL;
                
                return install_provided_packages(local_packages, db_packages);
            } else goto bad_args;
        } else if (strcmp(argv[1], "remove") == 0) {
            
        } else if (strcmp(argv[1], "gen-pkglist") == 0) { //takes in a folder fULL of shimas and makes a package list :D
            if (argc == 3) {
                if (helio_dir_exists(argv[2])) {
                    return gen_packagelist(argv[2]);
                } else {
                    fprintf(stderr, "unable to open provided package folder \"%s\".\n", argv[2]);
                    return SHM_INVALID_FILE;
                }
            } else goto bad_args;
        } else if (strcmp(argv[1], "update") == 0) {
            if (argc == 2) { //only "shima update" will trigger this :p

            } else goto bad_args;
        } else if (strcmp(argv[1], "help") == 0) {
            print_help();
            return SHM_SUCCESS;
        } else {
            fprintf(stderr, "unknown command provided.\n\nuse \"shima help\" for assistance.\n");
            return SHM_UNKNOWN_COMMAND;
        }
    }

    return SHM_SUCCESS;

    bad_args:
        fprintf(stderr, "bad arguments provided.\n\nuse \"shima help\" for assistance.\n");
        return SHM_BAD_ARGS;
}
