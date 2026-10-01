#include "install.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "main.h"
#include "build.h"
#include "net.h"
#include "heliotrope.h"

//also stole this one from yuiedit :p
//might wanna make this one a little safer tho, not checking fread return >_<!! TODO
char *read_str_with_len(FILE *db_file) { //same thing as above, with frwite tho~
    uint8_t str_len;
    char *new_string;

    fread(&str_len, 1, 1, db_file);
    new_string = safe_alloc(NULL, str_len + 1);
    fread(new_string, str_len, 1, db_file); //now we can read the string safely~
    new_string[str_len] = '\0';

    return new_string;
}

struct Campsite *parse_shima_package(char *file_path) {
    FILE *pkg_file = fopen(file_path, "rb");
    if (!pkg_file) return NULL;

    char header_check[6] = {0};
    size_t amt_read = fread(header_check, 1, 6, pkg_file);
    if (amt_read != 6 || memcmp(header_check, FILE_HEADER, 6) != 0) {
        fclose(pkg_file);
        return NULL;
    }

    //valid package, let's parse~
    struct Campsite *package = safe_calloc(1, sizeof(struct Campsite));
    package->pkg_name = read_str_with_len(pkg_file);
    package->pkg_ver = read_str_with_len(pkg_file);
    package->post_install_steps = NULL;
    
    uint8_t num_post_steps = 0;
    fread(&num_post_steps, 1, 1, pkg_file);
    package->post_install_steps = safe_calloc(1, sizeof(char *) * (num_post_steps + 1));
    package->post_install_steps[num_post_steps] = NULL; //can already null term here :p
    for (int i = 0; i < (int)num_post_steps; i++) {
        package->post_install_steps[i] = read_str_with_len(pkg_file);
    }
    
    fclose(pkg_file);

    return package;
}

static char **fetch_installed_packages(void) {
    //assuming installed packages are always in /etc/shima/installed.list
    FILE *db_file = fopen("/etc/shima/installed.list", "rb");
    if (!db_file) {
        fprintf(stderr, "!! could not open installed packages file! do you have root permissions?\n");
        return NULL;
    }

    char line[512] = {0};
    char **installed_packages = safe_calloc(1, sizeof(char *));
    size_t num_installed_packages = 0;
    installed_packages[0] = NULL; //just so we're not returning Literally null
    while (fgets(line, sizeof(line), db_file) != NULL) {
        char *after_pkg = strchr(line, ':');
        if (after_pkg == NULL) continue;
        *after_pkg = '\0';

        installed_packages = safe_alloc(installed_packages, sizeof(char *) * (num_installed_packages + 2));
        installed_packages[num_installed_packages] = helio_strdup(line);
        installed_packages[num_installed_packages + 1] = NULL; //nice null termination~
        num_installed_packages++;
    }

    fclose(db_file);
    return installed_packages;
}

//should recursively call itself for each new package to find deps :p
static void add_pkg_dependencies(char ***package_deps, char ***package_dep_versions, char *cur_pkg_name, FILE *db_file, char **pkgs_installed) {
    //make sure we go to the start of the db each time!! we could be anywhere
    fseek(db_file, 0, SEEK_SET);

    char line[2048] = {0};
    while (fgets(line, sizeof(line), db_file) != NULL) {
        //this should skip all lines but the starting ones we need, so we can loop through the packages we want now~
        if (line[0] == '#' || line[0] == '\n' || line[0] == ' ') continue;
        if (strncmp(cur_pkg_name, line, strlen(cur_pkg_name)) == 0) break; //found a match, let's check the deps :p 
    }

    size_t num_deps = 0;
    for (int i = 0; i < 3; i++) fgets(line, sizeof(line), db_file); //just want to get to where the actual deps are :p
    while (fgets(line, sizeof(line), db_file) != NULL) {
        if (line[0] != ' ') break; //now we go until we get NOT space :p just get outta there
        //new dependency! if not installed, recusrively call to get ITS dependencies
        bool already_installed = false;
        for (int i = 0; pkgs_installed[i] != NULL; i++) {
            if (strncmp(pkgs_installed[i], line + 4, strlen(pkgs_installed[i])) == 0) { //still works, since we compare the length of "nano" against "nano-9.2-shm0" or smth
                already_installed = true;
                break; //found it, no need to keep looking~
            }
        }

        if (!already_installed) {
            *package_deps = safe_alloc(*package_deps, sizeof(char *) * (num_deps + 2));
            *package_dep_versions = safe_alloc(*package_dep_versions, sizeof(char *) * (num_deps + 2));
            memmove(line, line + 4, strlen(line) - 3); //remove the first "    " yknow
            char *dash_loc = strchr(line, '-');

            size_t dash_loc_len = strlen(dash_loc);
            dash_loc[dash_loc_len - 1] = '\0';
            char *version = dash_loc + 1;
            (*package_dep_versions)[num_deps] = helio_strdup(version);
            (*package_dep_versions)[num_deps + 1] = NULL;

            *dash_loc = '\0';
            (*package_deps)[num_deps] = helio_strdup(line);
            (*package_deps)[num_deps + 1] = NULL; //also be nice and null terminate!! we need to check that literally right under us yo mista white
            num_deps++;

            char **additional_deps = NULL;
            char **additional_dep_versions = NULL;
            add_pkg_dependencies(&additional_deps, &additional_dep_versions, (*package_deps)[num_deps - 1], db_file, pkgs_installed);
            if (additional_deps == NULL) break; //if additional_deps == NULL already, we know to just like. get out lol

            //ok, now append all the additional deps we may have now >_<
            for (int i = 0; additional_deps[i] != NULL; i++) {
                *package_deps = safe_alloc(*package_deps, sizeof(char *) * (num_deps + 2));
                *package_dep_versions = safe_alloc(*package_dep_versions, sizeof(char *) * (num_deps + 2));

                (*package_deps)[num_deps] = additional_deps[i]; //pointer can stay where she is~
                (*package_deps)[num_deps + 1] = NULL;
                (*package_dep_versions)[num_deps] = additional_dep_versions[i];
                (*package_dep_versions)[num_deps + 1] = NULL;
                num_deps++;
            }
            free(additional_deps);
            free(additional_dep_versions);
        }
    }
}


//helper function to actually install each package~
static int install_pkg(const unsigned int cur_pkg, const unsigned int total_pkg, char *pkg_name, char *pkg_version, char *pkg_location, char **posthooks, FILE *install_list, bool is_dep) {
    //i added a fuckass feature to heliotrope that actually comes in clutch for deletion for us lmao
    char **installed_files = NULL;

    enum HelioReturnCode extract_ret = helio_extract(pkg_location, false, ROOT_DIR, false, false, &installed_files);
    if (extract_ret != HELIO_SUCCESS) {
        fprintf(stderr, "error \"%s\" occured while extracting %s to the root directory.\n", helio_error_to_string(extract_ret), pkg_name);
        return 1;
    } else {
        printf("[%u/%u] installed package %s-%s\n", cur_pkg, total_pkg, pkg_name, pkg_version);

        //now that we've installed the package, we can also run posthooks :0
        for (int i = 0; posthooks != NULL && posthooks[i] != NULL; i++) {
            printf("* %s\n", posthooks[i]);
            FILE *cmd_ptr = popen(posthooks[i], "r"); //dont need the output of this for anything, just need to know that it did run :p
            if (!cmd_ptr) {
                fprintf(stderr, "failed to run post-install command.\n");
                continue;
            }

            int ret_status = pclose(cmd_ptr);
            if (ret_status != 0) {
                fprintf(stderr, "command returned with code %i.\n", ret_status);
            }
        }

        char append_installed_list[256] = {0};
        if (is_dep) {
            snprintf(append_installed_list, 256, "%s: %s-%s (dependency)\n", pkg_name, pkg_name, pkg_version);
        } else {
            snprintf(append_installed_list, 256, "%s: %s-%s (selected)\n", pkg_name, pkg_name, pkg_version);
        }
        fwrite(append_installed_list, 1, strlen(append_installed_list), install_list);

        //and finally, writing to the installed.list with all of our thingies >.<
        for (int i = 0; installed_files[i] != NULL; i++) {
            //its ok to spam fwrite bc it buffers actual writes~
            fwrite(" ", 1, 1, install_list);
            fwrite(ROOT_DIR, 1, strlen(ROOT_DIR), install_list);
            fwrite(installed_files[i], 1, strlen(installed_files[i]), install_list);
            fwrite("\n", 1, 1, install_list);
            free(installed_files[i]);
        }
        free(installed_files);

        return 0;
    }
}

static bool is_pkg_already_installed(char **pkgs_installed, char *cur_pkg_name) {
    //ok, so valid package~ do we have it installed already? we're not dealing with versioning right now...
    bool skip_package = false;
    for (int j = 0; pkgs_installed[j] != NULL; j++) {
        if (strncmp(cur_pkg_name, pkgs_installed[j], strlen(cur_pkg_name)) == 0) {
            skip_package = true;
            break;
        }
    }

    return skip_package;
}

FILE *open_db_file(enum RetCode *error_code) {
    //assuming db is always in /etc/shima/pkglist.shmdb
    FILE *db_file = fopen("/etc/shima/pkglist.shmdb", "rb");
    if (!db_file) {
        fprintf(stderr, "!! could not open db file, meaning it likely does not exist! run \"shima update\" to get the latest database information.\n");
        *error_code = SHM_FILESYSTEM_ERROR;
        return NULL;
    }

    size_t header_length = strlen(DB_HEADER);
    char *header_check = safe_calloc(1, header_length);
    size_t amt_read = fread(header_check, 1, header_length, db_file);
    if (amt_read != header_length || memcmp(header_check, DB_HEADER, header_length) != 0) {
        fclose(db_file);
        free(header_check);
        fprintf(stderr, "!! db file seems to be corrupt. run \"shima update\" to get the latest database information.\n");
        *error_code = SHM_CORRUPT_DB;
        return NULL;

    }
    free(header_check);

    return db_file;
}

enum RetCode install_provided_packages(char **local_packages, char **db_packages, bool force) {
    //local packages are easier to validate, so let's check that first :p
    //in case deps are also local, we need to see if other .shm exist in the same folder >.<
    char **pkgs_installed = fetch_installed_packages();
    if (pkgs_installed == NULL) return SHM_FILESYSTEM_ERROR;

    //also grab the local folder ls for later~ could be useful :D
    char cwd[1024] = {0};
    getcwd(cwd, sizeof(cwd)); //getting shit RIGHT HERE :D
    char **local_dir_ls = helio_list_dir(cwd, false);

    enum RetCode db_open_ret = SHM_SUCCESS;
    FILE *db_file = open_db_file(&db_open_ret);
    if (db_open_ret != SHM_SUCCESS) return db_open_ret;

    /* local */
    struct InstallPkg **packages = NULL;
    size_t num_packages = 0;
    for (int i = 0; local_packages[i] != NULL; i++) {
        struct Campsite *cur_pkg = parse_shima_package(local_packages[i]);
        if (cur_pkg == NULL) {
            fprintf(stderr, "package %s could not be parsed.\n", local_packages[i]);
        }

        if (!force) {
            if (is_pkg_already_installed(pkgs_installed, cur_pkg->pkg_name)) {
                printf("package %s is already installed, skipping...\n", cur_pkg->pkg_name);
                continue;
            }
        }

        //valid package, can add to our list~
        packages = safe_alloc(packages, sizeof(struct InstallPkg *) * (num_packages + 1));
        packages[num_packages] = safe_calloc(1, sizeof(struct InstallPkg));
        packages[num_packages]->pkg_name = cur_pkg->pkg_name; //just pointer copy~
        packages[num_packages]->pkg_ver = cur_pkg->pkg_ver;
        packages[num_packages]->is_dep = false;
        packages[num_packages]->pkg_location = local_packages[i];
        packages[num_packages]->post_install_hooks = cur_pkg->post_install_steps;

        free(cur_pkg->pkg_desc);
        free(cur_pkg);

        num_packages++;
    }

    /* db */
    for (int i = 0; db_packages[i] != NULL; i++) {
        if (!force) {
            if (is_pkg_already_installed(pkgs_installed, db_packages[i])) {
                printf("package %s is already installed, skipping...\n", db_packages[i]);
                continue;
            }
        }

        //let's go digging in da db for each >_< similar to how we add deps in a sec
        //make sure we go to the start of the db each time!! we could be anywhere
        fseek(db_file, 0, SEEK_SET);
    
        char line[2048] = {0};
        while (fgets(line, sizeof(line), db_file) != NULL) {
            //this should skip all lines but the starting ones we need, so we can loop through the packages we want now~
            if (line[0] == '#' || line[0] == '\n' || line[0] == ' ') continue;
            if (strncmp(db_packages[i], line, strlen(db_packages[i])) == 0) break; //found a match!! let's look at the info for this package
        }

        //this next line should be the version! thats kinda all we need for this pass~ :3
        fgets(line, sizeof(line), db_file);
        char *pkg_ver;
        get_colon_parsed_string(line, &pkg_ver);

        //valid package, can add to our list~
        packages = safe_alloc(packages, sizeof(struct InstallPkg *) * (num_packages + 1));
        packages[num_packages] = safe_calloc(1, sizeof(struct InstallPkg));
        packages[num_packages]->pkg_name = db_packages[i]; //just pointer copy~
        packages[num_packages]->pkg_ver = pkg_ver;
        packages[num_packages]->is_dep = false;
        packages[num_packages]->pkg_location = NULL; //don't know!!!!! we need to go online for this >_<
        packages[num_packages]->post_install_hooks = NULL; //also will be determined later

        num_packages++;
    }

    /* deps */
    size_t num_initial_packages = num_packages;
    for (int i = 0; i < (int)num_initial_packages; i++) {
        char **cur_deps = NULL;
        char **cur_dep_versions = NULL;
        
        add_pkg_dependencies(&cur_deps, &cur_dep_versions, packages[i]->pkg_name, db_file, pkgs_installed);
        if (cur_deps == NULL) continue;

        for (int j = 0; cur_deps[j] != NULL; j++) {
            //ok one more loop we need to see if this is already a dep we got.
            bool already_added = false;
            for (int c = 0; c < (int)num_packages; c++) {
                if (strcmp(packages[c]->pkg_name, cur_deps[j]) == 0) {
                    already_added = true;
                    break;
                }
            }
            
            if (!already_added) {
                //now with dependencies in hand, we can add to our actual packages >.<
                packages = safe_alloc(packages, sizeof(struct InstallPkg *) * (num_packages + 1));
                packages[num_packages] = safe_calloc(1, sizeof(struct InstallPkg));
                packages[num_packages]->pkg_name = cur_deps[j];
                packages[num_packages]->pkg_ver = cur_dep_versions[j];
                packages[num_packages]->is_dep = true;
                packages[num_packages]->pkg_location = NULL; //will see later~

                num_packages++;
            }
        }
        free(cur_deps);
    }
    fclose(db_file);

    /* install */
    bool canceled = false;
    bool errors = false;
    if (num_packages > 0) {
        printf("shima will add the following package(s): ");
        for (int i = 0; i < (int)num_initial_packages; i++) printf("%s ", packages[i]->pkg_name);
        printf("\n");
    
        if (num_initial_packages != num_packages) {
            printf("  in addition, the following packages will be added as dependencies: ");
            for (int i = (int)num_initial_packages; i < (int)num_packages; i++) printf("%s ", packages[i]->pkg_name);
            printf("\n");
        }
        
        printf(" (this will be installed to your root directory, %s.)\n", ROOT_DIR); //MAKING ABSOLUTE SURE I DONT BRICK MY GLIBC AGAIN

        char response;
        do {
            printf("\nwould you like to continue with the installation? [Y/n] ");
            scanf(" %c", &response);
        } while (response != 'n' && response != 'y');

        if (response == 'n') {
            canceled = true;
        } else {
            bool parsed_local = false;
            size_t num_local = 0;
            struct Campsite **local_shima_pkgs = NULL;
            int *campsite_file_num = NULL; //im a dumbass and need to associate file w numba yo........

            //we need to also update the installed.list file. it def exists since we read it earlier :p
            FILE *install_list = fopen("/etc/shima/installed.list", "ab");
            if (!install_list) {
                fprintf(stderr, "!! failed to open the installed.list! check if you have root permissions.\n");
                return SHM_FILESYSTEM_ERROR;
            }

            //also need to open up the server location :3
            FILE *sources_list = fopen("/etc/shima/sources.list", "rb");
            if (!sources_list) {
                fprintf(stderr, "!! failed to open the sources.list! check if you have root permissions.\n");
                return SHM_FILESYSTEM_ERROR;
            }

            char source[1024] = {0};
            fgets(source, sizeof(source), sources_list); //we only use the first source LOLOLOL
            size_t source_len = strlen(source);
            if (source_len == 0) {
                fprintf(stderr, "!! nothing was found within your sources file. please add an online package repository~\n");
                return SHM_FILESYSTEM_ERROR;
            } else {
                if (source[source_len - 1] == '\n') source[source_len - 1] = '\0'; //we dont like new lines in our goddamn source
            }

            /* package location resolving */
            bool error_finding_locations = false;
            for (int i = 0; i < (int)num_packages; i++) {
                //if we don't have a pkg location we go searching locally >_< otherwise assume online from da repos
                if (packages[i]->pkg_location == NULL) {
                    if (!parsed_local) {
                        for (int j = 0; local_dir_ls[j] != NULL; j++) {
                            char *file_ext = strrchr(local_dir_ls[j], '.');
                            if (file_ext != NULL && strncmp(file_ext, ".shm", 4) == 0 && strlen(file_ext) == (size_t)4) { //last one prevents .shmdb :p
                                local_shima_pkgs = safe_alloc(local_shima_pkgs, sizeof(struct Campsite *) * (num_local + 2));
                                campsite_file_num = safe_alloc(campsite_file_num, sizeof(int) * (num_local + 1));
                                local_shima_pkgs[num_local] = parse_shima_package(local_dir_ls[j]);
                                local_shima_pkgs[num_local + 1] = NULL;
                                campsite_file_num[num_local] = j;
                                num_local++;
                            }
                        }
                        parsed_local = true;
                    }

                    for (int j = 0; j < (int)num_local; j++) {
                        if (local_shima_pkgs[j]->pkg_name != NULL && strcmp(packages[i]->pkg_name, local_shima_pkgs[j]->pkg_name) == 0) {
                            packages[i]->pkg_location = helio_strdup(local_dir_ls[campsite_file_num[j]]);
                            packages[i]->post_install_hooks = local_shima_pkgs[j]->post_install_steps;
                            break;
                        }
                    }

                    if (packages[i]->pkg_location == NULL) {
                        //didn't find it locally, so it must be a package we can fetch from online!!
                        //let's download it, put it in /tmp, and leave that as the location >.<
                        char fetch_url[2048] = {0}; 
                        char outfile_path[512] = {0};
                        snprintf(fetch_url, sizeof(fetch_url), "%s%s-%s.shm", source, packages[i]->pkg_name, packages[i]->pkg_ver);
                        snprintf(outfile_path, sizeof(outfile_path), "/tmp/%s-%s.shm", packages[i]->pkg_name, packages[i]->pkg_ver);

                        FILE *shm_file = fopen(outfile_path, "wb");
                        if (shm_file == NULL) {
                            fprintf(stderr, "!! unable to create temporary download file... for some reason? this shouldn't ever happen unless /tmp is not a directory on your computer.. 【・_・?】\n");
                            return SHM_FILESYSTEM_ERROR;
                        }

                        int download_ret = download_file(fetch_url, shm_file);
                        if (download_ret == 0) {
                            packages[i]->pkg_location = helio_strdup(outfile_path);
                        }
                    }
                }
            }

            /* actual installation loop~ */
            if (error_finding_locations) {
                fprintf(stderr, "could not find all packages, aborting...\n");
                errors = true;
            }
            for (int i = 0; error_finding_locations == false && i < (int)num_packages; i++) {
                if (packages[i]->pkg_location != NULL) {
                    int install_ret = install_pkg((unsigned int)i + 1, num_packages, packages[i]->pkg_name, packages[i]->pkg_ver, packages[i]->pkg_location, packages[i]->post_install_hooks, install_list, packages[i]->is_dep);
                    if (install_ret != 0) errors = true;
                } else {
                    fprintf(stderr, "could not find a location to install %s from.\n", packages[i]->pkg_name);
                }
            }

            free(campsite_file_num);
            fclose(install_list);

            //gotta cleanup local shima~
            if (local_shima_pkgs != NULL) {
                for (int i = 0; local_shima_pkgs[i] != NULL; i++) {
                    for (int j = 0; j < local_shima_pkgs[i]->num_post_steps; j++) {
                        free(local_shima_pkgs[i]->post_install_steps[j]);
                    }
                    free(local_shima_pkgs[i]->post_install_steps);
                    free(local_shima_pkgs[i]->pkg_name);
                    free(local_shima_pkgs[i]->pkg_ver);
                    free(local_shima_pkgs[i]);
                }
                free(local_shima_pkgs);
            }
        }
    }

    /* cleanup */
    for (int i = 0; local_packages[i] != NULL; i++) {
        free(local_packages[i]);
        local_packages[i] = NULL;
    }
    free(local_packages);
    local_packages = NULL;

    for (int i = 0; db_packages[i] != NULL; i++) {
        free(db_packages[i]);
        db_packages[i] = NULL;
    }
    free(db_packages);
    db_packages = NULL;

    for (int i = 0; pkgs_installed[i] != NULL; i++) {
        free(pkgs_installed[i]);
        pkgs_installed[i] = NULL;
    }
    free(pkgs_installed);
    pkgs_installed = NULL;

    for (int i = 0; i < (int)num_packages; i++) {
        //if (packages[i]->pkg_name != NULL) free(packages[i]->pkg_name);
        //if (packages[i]->pkg_ver != NULL) free(packages[i]->pkg_ver);
        //if (packages[i]->pkg_location != NULL) free(packages[i]->pkg_location);
    }
    free(packages);
 
    for (int i = 0; local_dir_ls[i] != NULL; i++) {
        free(local_dir_ls[i]);
    }
    free(local_dir_ls);

    if (num_packages > 0 && !canceled && !errors) {
        printf("successfully finished installing all packages.\n");
    } else if (errors) {
        fprintf(stderr, "package installation completed with errors.\n");
    } else {
        printf("nothing new to install, exiting shima.\n");
    }

    return SHM_SUCCESS;
}

enum RetCode delete_provided_packages(char **local_packages, char **db_packages) {
    //no automatically deleting deps for right now, since something else could be a dep of something else blah blah..
    //but i do want to add at some point methinks

    char **pkgs_installed = fetch_installed_packages();
    if (pkgs_installed == NULL) return SHM_FILESYSTEM_ERROR;

    /* local */
    struct InstallPkg **packages = NULL;
    size_t num_packages = 0;
    for (int i = 0; local_packages[i] != NULL; i++) {
        struct Campsite *cur_pkg = parse_shima_package(local_packages[i]);
        if (cur_pkg == NULL) {
            fprintf(stderr, "package %s could not be parsed.\n", local_packages[i]);
        }

        if (!is_pkg_already_installed(pkgs_installed, cur_pkg->pkg_name)) {
            fprintf(stderr, "%s is not installed! cannot remove.\n", cur_pkg->pkg_name);
            continue;
        }

        //valid package, can add to our list~
        packages = safe_alloc(packages, sizeof(struct InstallPkg *) * (num_packages + 1));
        packages[num_packages] = safe_calloc(1, sizeof(struct InstallPkg));
        packages[num_packages]->pkg_name = cur_pkg->pkg_name; //just pointer copy~
        packages[num_packages]->is_dep = false;

        free(cur_pkg->pkg_desc);
        free(cur_pkg);

        num_packages++;
    }

    /* db */
    for (int i = 0; db_packages[i] != NULL; i++) {
        if (!is_pkg_already_installed(pkgs_installed, db_packages[i])) {
            fprintf(stderr, "%s is not installed! cannot remove.\n", db_packages[i]);
            continue;
        }

        //valid package, can add to our list~
        packages = safe_alloc(packages, sizeof(struct InstallPkg *) * (num_packages + 1));
        packages[num_packages] = safe_calloc(1, sizeof(struct InstallPkg));
        packages[num_packages]->pkg_name = db_packages[i]; //just pointer copy~
        packages[num_packages]->is_dep = false;

        num_packages++;
    }

    /* removal time >:3 */
    bool canceled = false;
    bool errors = false;
    if (num_packages > 0) {
        printf("shima will remove the following package(s): ");
        for (int i = 0; i < (int)num_packages; i++) printf("%s ", packages[i]->pkg_name);
        printf("\n");

        char response;
        do {
            printf("\nwould you like to continue with the removal? [Y/n] ");
            scanf(" %c", &response);
        } while (response != 'n' && response != 'y');

        if (response == 'n') {
            canceled = true;
        } else {
            //we need to also update the installed.list file. it def exists since we read it earlier :p
            FILE *install_list = fopen("/etc/shima/installed.list", "r+");
            if (!install_list) {
                fprintf(stderr, "!! failed to open the installed.list! check if you have root permissions.\n");
                return SHM_FILESYSTEM_ERROR;
            }

            for (int i = 0; i < (int)num_packages; i++) {
                //all we need to do is just remove everything listed in install.list, then remove its entry :D
                //posthooks aren't included here, but maybe in the future we could do post removal hooks :0

                //make sure we go to the start of the db each time!! we could be anywhere
                fseek(install_list, 0, SEEK_SET);

                char line[1024] = {0};
                size_t remove_start_pos = 0;
                while (fgets(line, sizeof(line), install_list) != NULL) {
                    if (strncmp(packages[i]->pkg_name, line, strlen(packages[i]->pkg_name)) == 0) {
                        break; //found a match ^-^
                    } else {
                        remove_start_pos += strlen(line);
                    }
                }
                
                //we should have all the files here! just go until not ' ' first
                char *prev_dir = NULL;
                while (fgets(line, sizeof(line), install_list) != NULL) {
                    if (line[0] != ' ') break;

                    char *remove_file = line + 1;
                    remove_file[strlen(remove_file) - 1] = '\0'; //ending '\n' no good
                    int remove_ret = remove(remove_file);

                    char *file_loc = strrchr(remove_file, '/');
                    *file_loc = '\0';
                    if (prev_dir == NULL || strcmp(prev_dir, remove_file) != 0) {
                        if (prev_dir != NULL) {
                            //attempt to remove directory, will only succeed if empty (das ok, it can fail)
                            int remove_attempt = rmdir(prev_dir);
                            while (remove_attempt == 0) { //could remove that, what about next dir up~?
                                char *prev_loc = strrchr(prev_dir, '/');
                                *prev_loc = '\0';
                                remove_attempt = rmdir(prev_dir);
                                //this doesn't nuke *everything*, but does a pretty good damn job at it methinks :p
                            }
                            free(prev_dir);
                        } 
                        prev_dir = helio_strdup(remove_file);
                    }

                    if (remove_ret != 0) {
                        fprintf(stderr, "!! error while removing file %s: %s\n", remove_file, strerror(errno));
                        errors = true;
                    }
                }

                /* removal from installed.list */
                if (!errors) { //success!! let's remove this package from the installed.list~
                    fseek(install_list, remove_start_pos, SEEK_SET);
                    
                    //removing lines is actually quite complicated >.< let's find where this ends first
                    fgets(line, sizeof(line), install_list); //first line will be the package line (no ' ' at start)
                    size_t remove_ending_pos = remove_start_pos + strlen(line);
                    while (fgets(line, sizeof(line), install_list) && line[0] == ' ') {
                        remove_ending_pos += strlen(line); //start of next line~
                    }

                    //now, shift everything after upwards!! can deal with raw binary for this its בסדר
                    unsigned char shift_buf[SHIFT_BUF_SIZE];
                    long read_pos = remove_ending_pos;
                    long write_pos = remove_start_pos;

                    while (1) { //simpler to just to inf loop and break, tho a little jank imo lmao
                        fseek(install_list, read_pos, SEEK_SET);
                        size_t amt_read = fread(shift_buf, 1, SHIFT_BUF_SIZE, install_list);
                        if (amt_read == 0) break;
                        
                        fseek(install_list, write_pos, SEEK_SET);
                        fwrite(shift_buf, 1, amt_read, install_list);
                        
                        read_pos += amt_read;
                        write_pos += amt_read;
                    }

                    //finally =w= delete da tail
                    fflush(install_list);
                    ftruncate(fileno(install_list), write_pos);
                }
            }
        }
    }

    /* cleanup */
    for (int i = 0; local_packages[i] != NULL; i++) {
        free(local_packages[i]);
        local_packages[i] = NULL;
    }
    free(local_packages);
    local_packages = NULL;

    for (int i = 0; db_packages[i] != NULL; i++) {
        free(db_packages[i]);
        db_packages[i] = NULL;
    }
    free(db_packages);
    db_packages = NULL;

    for (int i = 0; pkgs_installed[i] != NULL; i++) {
        free(pkgs_installed[i]);
        pkgs_installed[i] = NULL;
    }
    free(pkgs_installed);
    pkgs_installed = NULL;

    if (num_packages > 0 && !canceled && !errors) {
        printf("successfully finished removing all packages.\n");
    } else if (errors) {
        fprintf(stderr, "package removal completed with errors.\n");
    } else {
        printf("nothing to remove, exiting shima.\n");
    }

    return SHM_SUCCESS;
}
