#include "main.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "build.h"
#include "install.h"
#include "net.h"
#include "heliotrope.h"

static void print_help(void) {
    printf("shima package manager, version %s\n\n", VERSION);
    printf("build [file/folder]       builds a package (or folder of packages) with the specified path to a campsite file\n");
    printf("install [name/file]       installs package(s) with the name (searches local db) or a local file\n");
    printf("force-install [name/file] forces a reinstall, even if you already have the package installed\n");
    printf("remove [name]             removes the named package(s) if it's installed on the system\n");
    printf("update                    pulls the latest package list from your configured server\n");
    printf("gen-pkglist [folder]      generates a pkglist.shmdb for a folder of campsites (useful for making repos)\n");
    printf("help                      prints this message~\n");
}

static enum RetCode update_local_files(void) {
    //ok! first we open up our pkglist, get our server url, and then just fetch with da fetcher
    char pkglist_path[] = "/etc/shima/pkglist.shmdb";
    FILE *db_file = fopen(pkglist_path, "wb");
    if (!db_file) {
        fprintf(stderr, "!! failed to open the local file database! check if you have root permissions.\n");
        return SHM_FILESYSTEM_ERROR;
    }

    FILE *sources_list = fopen("/etc/shima/sources.list", "rb");
    if (!sources_list) {
        fprintf(stderr, "!! failed to open the sources.list! check if you have root permissions.\n");
        return SHM_FILESYSTEM_ERROR;
    }

    char source[1024] = {0};
    fgets(source, sizeof(source), sources_list); //also only use first source here like the downloads lmao~
    fclose(sources_list);
    
    size_t source_len = strlen(source);
    if (source_len == 0) {
        fprintf(stderr, "!! nothing was found within your sources file. please add an online package repository~\n");
        return SHM_FILESYSTEM_ERROR;
    } else {
        if (source[source_len - 1] == '\n') source[source_len - 1] = '\0'; //we dont like new lines in our goddamn source
    }
    
    char fetch_url[2048] = {0}; 
    snprintf(fetch_url, sizeof(fetch_url), "%spkglist.shmdb", source);
    download_file(fetch_url, db_file);

    printf("successfully updated package database.\n");
    return SHM_SUCCESS;
}

static struct PkgArgInfo parse_args_for_pkgs(int argc, char **argv) {
    struct PkgArgInfo pkgs = {0};
    
    for (int i = 2; i < argc; i++) {
        if (argv[i][0] == '.' && argv[i][1] == '/') { //local file, so we should find it right here >.<
            FILE *test_local_file = fopen(argv[i], "rb");
            if (!test_local_file || argv[i][2] == '\0') { //if it just Ends too
                fprintf(stderr, "!! could not find local file %s to install from.\n", argv[i]);
                return pkgs;
            }
            fclose(test_local_file);

            //ok, this file does exist~ let's add it to what we want to try to install
            pkgs.local_packages = safe_alloc(pkgs.local_packages, sizeof(char *) * (pkgs.num_local + 1));
            pkgs.local_packages[pkgs.num_local] = helio_strdup(argv[i]);
            pkgs.num_local++;
        } else { //this is something we should find in our db presumably~
            pkgs.db_packages = safe_alloc(pkgs.db_packages, sizeof(char *) * (pkgs.num_db + 1));
            pkgs.db_packages[pkgs.num_db] = helio_strdup(argv[i]);
            pkgs.num_db++;
        }
    }

    //all the packages are added here, so toss them over to parse/look in db for~
    pkgs.local_packages = safe_alloc(pkgs.local_packages, sizeof(char *) * (pkgs.num_local + 1));
    pkgs.local_packages[pkgs.num_local] = NULL; //also null terminate before sending them in :p
    pkgs.db_packages = safe_alloc(pkgs.db_packages, sizeof(char *) * (pkgs.num_db + 1));
    pkgs.db_packages[pkgs.num_db] = NULL;

    return pkgs;
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
        } else if (strcmp(argv[1], "install") == 0 || strcmp(argv[1], "force-install") == 0) { //install, also need to see what package we're installing
            //if we have a local package (./ or some sort of path), then we find that file!
            //otherwise, we need to go digging in the db for it >.<
            bool force = false;
            if (strcmp(argv[1], "force-install") == 0) force = true;

            if (argc >= 3) {
                //we could have any number of args after this, for any number of packages >:3
                struct PkgArgInfo pkgs = parse_args_for_pkgs(argc, argv);
                if (pkgs.num_db == 0 && pkgs.num_local == 0) return SHM_FILESYSTEM_ERROR;
                return install_provided_packages(pkgs.local_packages, pkgs.db_packages, force);
            } else goto bad_args;
        } else if (strcmp(argv[1], "remove") == 0) {
            //kinda same thing with installation, we get what packages need to be removed and just take care of em boss~
                struct PkgArgInfo pkgs = parse_args_for_pkgs(argc, argv);
                if (pkgs.num_db == 0 && pkgs.num_local == 0) return SHM_FILESYSTEM_ERROR;
                return delete_provided_packages(pkgs.local_packages, pkgs.db_packages);
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
                return update_local_files();
            } else goto bad_args;
        } else if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--h") == 0 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-help") == 0) {
            print_help();
            return SHM_SUCCESS;
        } else if (strcmp(argv[1], "version") == 0 || strcmp(argv[1], "--v") == 0 || strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-version") == 0) {
            printf("shima package manager, version %s\n", VERSION);
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
