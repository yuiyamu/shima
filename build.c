#include "build.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include "main.h"
#include "install.h"
#include "heliotrope.h"

enum section {
    NONE,
    INFO,
    SOURCES,
    DEPENDS,
    PREPARE,
    POST_INSTALL
};

static enum section get_section(const char *line) {
    enum section section = NONE;
    if (strncmp(line, "= info =", strlen("= info =")) == 0) section = INFO;
    else if (strncmp(line, "= sources =", strlen("= sources =")) == 0) section = SOURCES;
    else if (strncmp(line, "= depends =", strlen("= depends =")) == 0) section = DEPENDS;
    else if (strncmp(line, "= prepare =", strlen("= prepare =")) == 0) section = PREPARE;
    else if (strncmp(line, "= post-install =", strlen("= post-install =")) == 0) section = POST_INSTALL;

    return section;
}

static char *prepare_extracted_string(char *line) {
    char *prep_str = helio_strdup(line);
    int line_len = strlen(prep_str);
    prep_str[line_len - 1] = '\0';
    return prep_str;
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

struct Campsite *parse_campsite(FILE *campsite_file) {
    if (campsite_file == NULL) return NULL;

    char line[1024] = {0}; //should be more than we reasonably need
    enum section cur_section = NONE;

    //first, check for campsite header~
    fgets(line, sizeof(line), campsite_file);
    if (strncmp(line, CAMPSITE_COMPARE, strlen(CAMPSITE_COMPARE)) != 0) {
        fprintf(stderr, "!! provided campsite file does not match the current version.\nprovided: %s | current version %s\n", line, CAMPSITE_COMPARE);
        return NULL;
    }

    //ok, valid campsite :D
    struct Campsite *camp = safe_calloc(1, sizeof(struct Campsite));
    camp->dependencies = NULL;
    camp->sources = NULL;
    camp->prepare_steps = NULL;
    camp->post_install_steps = NULL;
    camp->num_dependencies = 0;
    camp->num_sources = 0;
    camp->num_prepare_steps = 0;
    camp->num_post_steps = 0;

    while (fgets(line, sizeof(line), campsite_file) != NULL) {
        if (line[0] == '=') { //checking section, all will be like "= section ="
            cur_section = get_section(line);
            continue;
        }
        if (line[0] == '\n' || line[0] == '#') continue;

        switch (cur_section) {
            case INFO: {
                if (strncmp(line, "name", strlen("name")) == 0) {
                    get_colon_parsed_string(line, &camp->pkg_name);
                } else if (strncmp(line, "desc", strlen("desc")) == 0) {
                    get_colon_parsed_string(line, &camp->pkg_desc);
                } else if (strncmp(line, "pkgver", strlen("pkgver")) == 0) {
                    get_colon_parsed_string(line, &camp->pkg_ver);
                }
                break;
            }
            case SOURCES: {
                //right now we'll only deal with one source (the actual source package itself :p)
                //could add patches later, but that's kinda beyond the scope of this
                //so basically, we just get one and we're out yo
                camp->num_sources = 1; //static for now ofc =w=
                camp->sources = safe_alloc(camp->sources, camp->num_sources * sizeof(char *));
                camp->sources[0] = helio_strdup(line);
                camp->sources[0][strlen(camp->sources[0]) - 1] = '\0'; //new line at the end of this~
                break;
            }
            case DEPENDS: {
                //can be literally just the same as prepare steps lol
                camp->dependencies = safe_alloc(camp->dependencies, (camp->num_dependencies + 1) * sizeof(char *));
                camp->dependencies[camp->num_dependencies] = prepare_extracted_string(line);
                camp->num_dependencies++;
                break;
            }
            case PREPARE: {
                //these are all of the steps we take to prepare. scary...
                //assumed to be ran from cd'd inside the extracted folder
                camp->prepare_steps = safe_alloc(camp->prepare_steps, (camp->num_prepare_steps + 1) * sizeof(char *));
                camp->prepare_steps[camp->num_prepare_steps] = prepare_extracted_string(line);
                camp->num_prepare_steps++;
                break;
            }
            case POST_INSTALL: {
                camp->post_install_steps = safe_alloc(camp->post_install_steps, (camp->num_post_steps + 1) * sizeof(char *));
                camp->post_install_steps[camp->num_post_steps] = prepare_extracted_string(line);
                camp->num_post_steps++;
                break;
            }
            case NONE: {
                break;
            }
        }
    }

    return camp;
}

static void dismantle_campsite(struct Campsite *camp) {
    for (int i = 0; i < camp->num_prepare_steps; i++) {
        free(camp->prepare_steps[i]);
    }
    free(camp->prepare_steps);

    for (int i = 0; i < camp->num_dependencies; i++) {
        free(camp->dependencies[i]);
    }
    free(camp->dependencies);

    for (int i = 0; i < camp->num_sources; i++) {
        free(camp->sources[i]);
    }
    free(camp->sources);

    for (int i = 0; i < camp->num_post_steps; i++) {
        free(camp->post_install_steps[i]);
    }
    free(camp->post_install_steps);

    free(camp->pkg_desc);
    free(camp->pkg_name);
    free(camp->pkg_ver);
    free(camp);
}

static int write_str_with_len(char *string, FILE *db_file) {
    uint8_t cur_str_len = strlen(string);
    size_t written = fwrite(&cur_str_len, 1, 1, db_file);
    if (written != 1) return 1;

    written = fwrite(string, 1, cur_str_len, db_file);
    if (written != cur_str_len) return 1;

    return 0;
}

enum RetCode build_package(FILE *campsite_file) {
    struct Campsite *camp = parse_campsite(campsite_file);
    if (camp == NULL) {
        fclose(campsite_file);
        return SHM_INVALID_CAMPSITE_VER;
    } else {
        fclose(campsite_file);
    }

    //now, even tho it's not null, we need to check for the fields we have populated >:3
    if (!camp->pkg_name || !camp->pkg_desc || !camp->pkg_desc || camp->num_sources == 0 || camp->num_prepare_steps == 0) {
        fprintf(stderr, "!! provided camp file does not have all fields filled!\n");
        dismantle_campsite(camp);
        return SHM_MALFORMED_CAMPSITE;
    }
    printf("creating package %s-%s...\n\n", camp->pkg_name, camp->pkg_ver);

    /* source extraction and build */
    //the first source should always be our zip file, so.. just invoke helio Lol
    printf("extracting main source package %s...\n", camp->sources[0]);
    enum HelioReturnCode helio_return = helio_extract(camp->sources[0], false, NULL, true, false);
    if (helio_return != HELIO_SUCCESS) {
        fprintf(stderr, "!! error while extracting %s.\n", camp->sources[0]);
        dismantle_campsite(camp);
        return SHM_EXTRACT_ERROR;
    }

    char *new_folder_name = helio_strdup(camp->sources[0]);
    new_folder_name[strrchr(new_folder_name, '.') - new_folder_name] = '\0';
    if (new_folder_name[0] == '.' && new_folder_name[1] == '/') { //this should be the case, you should make it like this but just in case not, its an if :p
        memmove(new_folder_name, new_folder_name + 2, strlen(new_folder_name) - 1);
    }

    char cwd_buf[512] = {0};
    char *extract_directory = helio_get_path(getcwd(cwd_buf, sizeof(cwd_buf)), new_folder_name);
    printf("moving into %s...\n", extract_directory);
    int cd_ret = chdir(new_folder_name);
    if (cd_ret != 0) {
        fprintf(stderr, "!! error while changing directories. do you have permission to modify the local filesystem?\n");
        dismantle_campsite(camp);
        return SHM_FILESYSTEM_ERROR;
    }

    for (int i = 0; i < camp->num_prepare_steps; i++) {
        if (strncmp(camp->prepare_steps[i], "!~!", 3) == 0) { //special marker to use the safer helio_mkdir rather than mkdir system call
            memmove(camp->prepare_steps[i], camp->prepare_steps[i] + 3, strlen(camp->prepare_steps[i]) - 2);
            printf("* mkdir %s (with helio_mkdir)\n", camp->prepare_steps[i]);
            helio_mkdir(camp->prepare_steps[i]);
            continue;
        }

        printf("* %s\n", camp->prepare_steps[i]);
        if (strncmp(camp->prepare_steps[i], "cd ", 3) == 0) { //need to handle changing directories :p
            memmove(camp->prepare_steps[i], camp->prepare_steps[i] + 3, strlen(camp->prepare_steps[i]) - 2); //not used again so memmove is ok
            chdir(camp->prepare_steps[i]);
            continue;
        }

        int cmd_ret = system(camp->prepare_steps[i]);
        if (cmd_ret != 0) {
            fprintf(stderr, "\n!! provided prepare steps ran a command with a non-zero exit code (error) !!\ncommand: %s\nplease double check your campsite file and try again.\n", camp->prepare_steps[i]);
            dismantle_campsite(camp);
            return SHM_PREPARE_CMD_ERROR;
        }
    }
    free(new_folder_name);

    //should have a built package in ./shima now, time to get it outta there + go back to the main dir + delete the folder + yeah
    chdir(cwd_buf); //back to our original place :D
    char package_name[256] = {0};
    snprintf(package_name, sizeof(package_name), "%s-%s", camp->pkg_name, camp->pkg_ver);

    char *shima_folder = helio_get_path(extract_directory, "shima");
    free(extract_directory);
    printf("build successful. making compressed package %s.shm...\n", package_name);

    //time to tack on our metadata!! we need to put it at the start, bc zip is bottom up :p
    char zip_file_name[strlen(package_name) + 5];
    sprintf(zip_file_name, "%s.shm", package_name);
    FILE *zip_file = fopen(zip_file_name, "wb");
    if (zip_file == NULL) {
        fprintf(stderr, "!! unable to open local shm file %s!\n", zip_file_name);
    }

    if (fwrite(FILE_HEADER, 1, 6, zip_file) != 6) goto fwrite_error; //should always be 6 long
    if (write_str_with_len(camp->pkg_name, zip_file)) goto fwrite_error;
    if (write_str_with_len(camp->pkg_ver, zip_file)) goto fwrite_error;

    //here, we also include post install hooks!! :3
    if (fwrite(&camp->num_post_steps, 1, 1, zip_file) != 1) goto fwrite_error;
    for (int i = 0; i < (int)camp->num_post_steps; i++) {
        if (write_str_with_len(camp->post_install_steps[i], zip_file)) goto fwrite_error;
    }
    //also don't need something to signify the end, bc the start of the zip will always be "PK" (thats our tell to get out :p)

    fclose(zip_file);

    enum HelioReturnCode compress_ret =  helio_compress(shima_folder, package_name, ".shm", false); //compressed, but now we want to add our metadata to the top >.<
    if (compress_ret != HELIO_SUCCESS) {
        fprintf(stderr, "!! an error occured while creating package archive %s.shm (helio error: %s).\n", package_name, helio_error_to_string(compress_ret));
        free(shima_folder);
        dismantle_campsite(camp);
        return SHM_COMPRESS_ERROR;
    }

    free(shima_folder);
    dismantle_campsite(camp);

    return SHM_SUCCESS;

    fwrite_error:
        fprintf(stderr, "!! fwrite returned an unexpected value while making package.\n");
        dismantle_campsite(camp);
        return SHM_FILESYSTEM_ERROR;
}

enum RetCode gen_packagelist(char *shima_folder) {
    //first things first, let's get all of the campsites in this folder~
    char **folder_ls = helio_list_dir(shima_folder, false);
    struct Campsite **campsites = NULL;
    size_t num_campsites = 0;

    chdir(shima_folder); //all operations within here, might as well chdir~
    for (int i = 0; folder_ls[i] != NULL; i++) {
        char *file_ext = strrchr(folder_ls[i], '.');
        if (file_ext != NULL && strncmp(file_ext, ".campsite", strlen(".campsite")) == 0) {
            campsites = safe_alloc(campsites, sizeof(struct Campsite *) * (num_campsites + 1));

            FILE *new_campsite = fopen(folder_ls[i], "r");
            if (!new_campsite) continue;
            campsites[num_campsites] = parse_campsite(new_campsite);
            fclose(new_campsite);

            if (campsites[num_campsites] == NULL) {
                fprintf(stderr, "found a campsite at %s, but was unable to parse it.\n", folder_ls[i]);
            }

            num_campsites++;
        }
        free(folder_ls[i]);
    }
    free(folder_ls);

    //ok! got all the campsites, now let's make a proper file >.<
    FILE *pkglist_file = fopen("pkglist.shmdb", "w");
    size_t db_header_length = strlen(DB_HEADER);
    size_t write_ret = fwrite(DB_HEADER, 1, db_header_length, pkglist_file);
    if (write_ret != db_header_length) {
        fclose(pkglist_file);
        return SHM_FILESYSTEM_ERROR;
    }
    for (int i = 0; i < (int)num_campsites; i++) {
        char cur_pkg_buf[1024] = {0};
        if (campsites[i] == NULL) continue;

        int chars_wrote = sprintf(cur_pkg_buf, "\n%s\n  version: %s\n  desc: %s\n  deps:\n", campsites[i]->pkg_name, campsites[i]->pkg_ver, campsites[i]->pkg_desc);
        for (int j = 0; j < campsites[i]->num_dependencies; j++) {
            int dep_wrote = 0;
            dep_wrote = sprintf(cur_pkg_buf + chars_wrote, "    %s\n", campsites[i]->dependencies[j]);
            chars_wrote += dep_wrote;
            free(campsites[i]->dependencies[j]);
        }
        fwrite(cur_pkg_buf, 1, strlen(cur_pkg_buf), pkglist_file);
        printf("added package %s to db.\n", campsites[i]->pkg_name);

        //ok, now clean up our campsite >.<
        for (int j = 0; j < campsites[i]->num_prepare_steps; j++) {
            free(campsites[i]->prepare_steps[j]);
        }
        for (int j = 0; j < campsites[i]->num_sources; j++) {
            free(campsites[i]->sources[j]);
        }
        free(campsites[i]->prepare_steps);
        free(campsites[i]->sources);
        free(campsites[i]->dependencies);
        free(campsites[i]->pkg_name);
        free(campsites[i]->pkg_ver);
        free(campsites[i]->pkg_desc);
        free(campsites[i]);
    }
    free(campsites);
    fclose(pkglist_file);

    printf("\nsuccessfully generated package list~\n");

    return SHM_SUCCESS;
}
