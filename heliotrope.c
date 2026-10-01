#ifndef _WIN32
#define _POSIX_C_SOURCE 200112L
#endif

#include "heliotrope.h"

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define MKDIR(dir) _mkdir(dir)
#else
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <utime.h>
#include <fcntl.h>
#include <dirent.h>
#define MKDIR(dir) mkdir(dir, 0755);
#endif

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static uint16_t two_byte_to_int(const unsigned char byte_1, const unsigned char byte_2) {
    return byte_1 | (byte_2 << 8);
}

static uint32_t four_byte_to_int(const unsigned char byte_1, const unsigned char byte_2, const unsigned char byte_3, const unsigned char byte_4) {
    return byte_1 | (byte_2 << 8) | (byte_3 << 16) | (byte_4 << 24);
}

static void int_to_two_bytes(const uint16_t value, unsigned char *buf) {
    buf[0] = value & 0xFF;
    buf[1] = (value >> 8) & 0xFF;
}

static void int_to_four_bytes(const uint32_t value, unsigned char *buf) {
    buf[0] = value & 0xFF;
    buf[1] = (value >> 8)  & 0xFF;
    buf[2] = (value >> 16) & 0xFF;
    buf[3] = (value >> 24) & 0xFF;
}

#define MAX_CHUNK 131072

static const unsigned char eocd_header[] = {0x50, 0x4b, 0x05, 0x06};
static const unsigned char cd_header[] = {0x50, 0x4b, 0x01, 0x02};
static const unsigned char local_file_header[] = {0x50, 0x4b, 0x03, 0x04};
static enum HelioReturnCode extract_file(FILE *file, uint32_t offset, const char *directory_path) {
    fseek(file, offset, SEEK_SET);
    unsigned char local_header[30] = {0};
    unsigned char header_compare[4] = {0};

    size_t fread_return = fread(local_header, 1, 30, file);
    memcpy(header_compare, local_header, 4);
    if (fread_return != 30 || memcmp(header_compare, local_file_header, sizeof(header_compare)) != 0) {
        return HELIO_FILE_INVALID;
    }

    //compression method check~
    uint16_t compression_method = two_byte_to_int(local_header[8], local_header[9]);
    if (compression_method != 8 && compression_method != 0) {
        return HELIO_INVALID_COMPRESSION_METHOD; //anything else is not valid~
    }

    uint16_t file_name_len = two_byte_to_int(local_header[26], local_header[27]);
    uint16_t extra_field_len = two_byte_to_int(local_header[28], local_header[29]);
    uint32_t compressed_size = four_byte_to_int(local_header[18], local_header[19], local_header[20], local_header[21]);
    uint32_t uncompressed_size = four_byte_to_int(local_header[22], local_header[23], local_header[24], local_header[25]);
    unsigned char file_name[file_name_len + 1];
    fread(file_name, 1, file_name_len, file);
    file_name[file_name_len] = '\0';

    if (compressed_size == 0) { //size of 0, must be a folder!
        char *full_folder_path = helio_get_path(directory_path, (char *)file_name);
        if (!full_folder_path) {
            return HELIO_FILE_INVALID;
        }
        helio_mkdir(full_folder_path);
        free(full_folder_path);
    } 

    char *slash = strrchr((char *)file_name, '/');
    if (slash != NULL) { //creates all parent directories :3
        size_t directory_len = slash - (char *)file_name + 1;

        char new_directory[directory_len + 1];
        memcpy(new_directory, file_name, directory_len);
        new_directory[directory_len] = '\0';

        char *full_folder_path = helio_get_path(directory_path, new_directory);
        if (!full_folder_path) {
            return HELIO_FILE_INVALID;
        }
        helio_mkdir(full_folder_path);
        free(full_folder_path);
    }

    //now we're at the compressed data :0~!!
    fseek(file, extra_field_len, SEEK_CUR);

    size_t input_chunk_size = MAX_CHUNK;
    size_t output_chunk_size = MAX_CHUNK;
    if (compressed_size < MAX_CHUNK) {
        input_chunk_size = compressed_size; //for files smaller, this saves a bit of memory~
    }
    if (uncompressed_size < MAX_CHUNK) {
        output_chunk_size = uncompressed_size;
    }
    unsigned char input_buf[input_chunk_size];
    unsigned char output_buf[output_chunk_size];

    //let's create the file >.<
    char *file_path = helio_get_path(directory_path, (char *)file_name);
    if (!file_path) return HELIO_FILE_INVALID;

    //we could get things that are just Not files through here. in order to deal with that, dir check~
    if (uncompressed_size == 0) {
        return HELIO_SUCCESS;
    }

    //test if it's a symlink, can make problems when we reextract somewhere :p
    struct stat sym_test;
    if (lstat(file_path, &sym_test) == 0) {
        if (S_ISLNK(sym_test.st_mode)) {
            unlink(file_path);
        }
    }

    FILE *new_file = fopen(file_path, "w");
    if (!new_file) return HELIO_FILESYSTEM_ERROR;
    free(file_path);

    if (compression_method == 8) {
        //for all of the compressed data, we literally just feed it to zlib and it takes care of things for us =w=
        //we really just have to worry about parsing the .zip format, not huffman tree fuckery
        z_stream strm = {0};
        int inflate_ret = inflateInit2(&strm, -MAX_WBITS);
        if (inflate_ret != Z_OK) {
            return HELIO_DEFLATE_ERROR; //not checking every single error >_<,, but its daijoubu
        }

        size_t remaining_bytes = compressed_size; //max at first
        while (inflate_ret != Z_STREAM_END && remaining_bytes > 0) { //0 bytes left to read
            size_t bytes_to_read = remaining_bytes < input_chunk_size? remaining_bytes : input_chunk_size;
            strm.avail_in = fread(input_buf, 1, bytes_to_read, file);
            strm.next_in = input_buf;

            //this do while loop case is something i honestly don't understand very well,, but i can't be bothered to rewrite
            do {
                strm.next_out = output_buf;
                strm.avail_out = output_chunk_size;
                inflate_ret = inflate(&strm, Z_NO_FLUSH);
                if (inflate_ret != Z_OK && inflate_ret != Z_STREAM_END) {
                    return HELIO_DEFLATE_ERROR;
                }
                size_t bytes_out = output_chunk_size - strm.avail_out;
                fwrite(output_buf, 1, bytes_out, new_file);
            } while (strm.avail_out == 0);
        }
        inflateEnd(&strm);
    } else { //if uncompressed, no use for any of this =w=
        size_t remaining_bytes = compressed_size;
        while (remaining_bytes > 0) {
            size_t to_read = remaining_bytes < output_chunk_size? remaining_bytes : output_chunk_size;
            size_t bytes_read = fread(output_buf, 1, to_read, file);
            if (bytes_read == 0) break;

            fwrite(output_buf, 1, bytes_read, new_file);
            remaining_bytes -= bytes_read;
        }
    }

    fclose(new_file);

    return HELIO_SUCCESS;
}


enum HelioReturnCode helio_extract(char *filename, bool verbose, char *base_directory, bool create_extract_folder, bool remove_after_extract, char ***files_extracted) {        
    FILE *file = fopen(filename, "rb");
    if (file == NULL) return HELIO_FILE_NOT_EXIST;
    //we don't look at the start of the zip file, since technically the first file offset could be anywhere
    //most zip files will have the first file start at, well, the first byte - but this doesn't have to be the case

    //time to actually parse this shit >:3 since .osz files have no comments, everything will just be a fixed end - 22
    //to where the start of the Embodiment of Central Directory is
    fseek(file, -22, SEEK_END);
    unsigned char eocd[22] = {0};
    size_t fread_return = fread(eocd, 1, 22, file);

    unsigned char header_compare[4] = {0};
    memcpy(header_compare, eocd, 4);
    if (fread_return != 22 || memcmp(header_compare, eocd_header, sizeof(header_compare)) != 0) {
        fclose(file);
        return HELIO_FILE_INVALID;
    }

    //okay, time to read the eocd. all we really want is the number of records, size, and offset of start =w=
    //stuff like disk number is beyond the scope of this project
    uint16_t num_files = two_byte_to_int(eocd[10], eocd[11]); //number of total records
    uint32_t central_directory_size = four_byte_to_int(eocd[12], eocd[13], eocd[14], eocd[15]);
    uint32_t central_directory_start_offset = four_byte_to_int(eocd[16], eocd[17], eocd[18], eocd[19]);

    //that's everything we need from the eocd~ time to actually read the central directory!
    fseek(file, central_directory_start_offset, SEEK_SET);
    unsigned char *central_directory = safe_calloc(1, central_directory_size); //heap, since wow we can have a cd that is larger than the stack sometimes
    fread_return = fread(central_directory, 1, central_directory_size, file);

    memcpy(header_compare, central_directory, 4);
    if (fread_return != central_directory_size || memcmp(header_compare, cd_header, sizeof(header_compare))) { //we can assume each file entry after is valid probably lol
        fclose(file);
        return HELIO_FILE_INVALID;
    }

    //making output directory, additional directories inside must be made seperately~
    char *folder_name = NULL;
    if (create_extract_folder) {
        folder_name = helio_strdup(filename);
        int filename_offset = 0;
        if (folder_name[0] == '.' && folder_name[1] == '/') {
            //for purposes down the line, we'll make sure that there's no "./" in front~
            memmove(folder_name, folder_name + 2, strlen(folder_name) - 1);
            filename_offset = 2;
        }
        folder_name[strrchr(filename, '.') - filename - filename_offset] = '\0';
    } else {
        //otherwise, folder_name just needs to be an empty string >.<
        folder_name = safe_calloc(1, sizeof(char));
    }

    char *folder_path = NULL;
    if (base_directory == NULL) {
        folder_path = helio_strdup(folder_name);
    } else {
        folder_path = helio_get_path(base_directory, folder_name);
    }

    if (create_extract_folder) helio_mkdir(folder_path);

    //each file has its own little central directory >_<!! we need to get all the values we want from her~
    size_t offset = 0;
    if (files_extracted != NULL) {
        *files_extracted = safe_calloc(num_files + 1, sizeof(char *));
        (*files_extracted)[num_files] = NULL; //already can null term~
    }
    for (int i = 0; i < num_files; i++) {
        //lowkey wont bother with crc32. if its corrupt its corrupt bro LOL
        uint16_t dos_time = two_byte_to_int(central_directory[12 + offset], central_directory[13 + offset]);
        uint16_t dos_date = two_byte_to_int(central_directory[14 + offset], central_directory[15 + offset]);

        //uncompressed size used for symlinking :p
        uint32_t uncompressed_size = four_byte_to_int(central_directory[24 + offset], central_directory[25 + offset], central_directory[26 + offset], central_directory[27 + offset]);
        uint16_t file_name_len = two_byte_to_int(central_directory[28 + offset], central_directory[29 + offset]);
        uint16_t extra_field_len = two_byte_to_int(central_directory[30 + offset], central_directory[31 + offset]);
        uint16_t file_comment_len = two_byte_to_int(central_directory[32 + offset], central_directory[33 + offset]);
        uint32_t external_file_attributes = four_byte_to_int(central_directory[38 + offset], central_directory[39 + offset], central_directory[40 + offset], central_directory[41 + offset]);
        uint32_t file_offset = four_byte_to_int(central_directory[42 + offset], central_directory[43 + offset], central_directory[44 + offset], central_directory[45 + offset]);

        unsigned char filename[file_name_len + 1];
        memcpy(filename, central_directory + 46 + offset, file_name_len);
        filename[file_name_len] = '\0';
        if (verbose) {
            printf("  %s/%s\n", folder_path, filename);
        }
        if (files_extracted != NULL) {
            (*files_extracted)[i] = helio_strdup(filename);
        }

        enum HelioReturnCode file_code = extract_file(file, file_offset, folder_path);
        if (file_code != HELIO_SUCCESS) {
            return file_code;
        }

        //also need to restore file permissions~ this is shima exclusive for now >.<
        //this also only works with unix but i cant be bothered to not do unix right now
        char *file_path = helio_get_path(folder_path, (char *)filename);
        external_file_attributes = external_file_attributes >> 16; //shift 16 bits for unix perms~
        if (S_ISLNK(external_file_attributes)) { //need to see if this is a symlink, and then properly link it if so!!
            char *link_to = safe_calloc(1, uncompressed_size + 1);
            FILE *sym_file = fopen(file_path, "rb");
            if (!sym_file) return HELIO_FILESYSTEM_ERROR;

            fread(link_to, 1, uncompressed_size, sym_file);
            fclose(sym_file);
            link_to[uncompressed_size] = '\0'; //ok, nice and safe string now~
            
            //first unlink for safety >_<
            unlink(file_path);
            if (symlink(link_to, file_path) == -1) return HELIO_FILESYSTEM_ERROR;
            free(link_to);
        } else {
            mode_t file_perms = external_file_attributes & 0777;
            chmod(file_path, file_perms);
        }

        //finally, we gotta restore the file modification time >_> ughhh
#ifndef _WIN32        
        struct tm unix_time = {0}; //stupid ass struct that deals with unix time

        unix_time.tm_mday = dos_date & 0x1F;
        unix_time.tm_mon = ((dos_date >> 5) & 0x0F) - 1;
        unix_time.tm_year = ((dos_date >> 9) & 0x7F) + 80;
        unix_time.tm_sec = (dos_time & 0x1F) * 2;
        unix_time.tm_min = (dos_time >> 5) & 0x3F;
        unix_time.tm_hour = (dos_time >> 11) & 0x1F;
        unix_time.tm_isdst = -1; //is it daylight savings? figure it out bozo.

        //for older posix, we need to stat the file and then use That as the access time :p
        struct stat file_stat;
        time_t access_time = time(NULL); //just default jan 1 1970 if we cant access its not a big deal
        if (stat(file_path, &file_stat) != 0) {
            access_time = file_stat.st_atime;
        }

        //ok now can update our file time :3
        struct utimbuf time_struct;
        time_struct.actime = access_time; //no access time idc
        time_struct.modtime = mktime(&unix_time);
        utime(file_path, &time_struct);
#elif
#endif

        free(file_path);

        offset += 46 + file_name_len + extra_field_len + file_comment_len;
    }

    if (remove_after_extract) {
        if (remove(filename) != 0) {
            fprintf(stderr, "failed to remove archive at %s\n", filename);
        }
    }
    fclose(file);
    free(folder_path);
    free(folder_name);
    free(central_directory);

    return HELIO_SUCCESS;
}

enum HelioReturnCode helio_compress(char *folder_path, char *filename, char *extension, bool verbose) {
    //first, get into our directory and list everything - we can compress each file, and then make our files based off of those~
    int folder_path_end = strlen(folder_path);
    if (folder_path[folder_path_end] == '/') folder_path[folder_path_end] = '\0'; //ending / is not nice.

    char **dir_list = helio_list_dir(folder_path, true);
    if (!dir_list) return HELIO_FILE_NOT_EXIST;

    struct HelioFile **files = NULL;
    int num_files = 0;

    char zip_file_name[strlen(filename) + strlen(extension) + 1];
    sprintf(zip_file_name, "%s%s", filename, extension);
    FILE *zip_file = fopen(zip_file_name, "ab"); //append, shima only~
    if (zip_file == NULL) return HELIO_FILESYSTEM_ERROR;

    //since on shima we add an offset, we need to account for this in the zip file creation ^^
    size_t initial_file_offset = ftell(zip_file);

    //every file should be created at the same time, so let's just resolve that right away hehe~
    //also kinda. whatever code but its ok.
    time_t current_time = time(NULL);
    struct tm *ima = localtime(&current_time);
    uint16_t zip_time = (ima->tm_hour << 11) | (ima->tm_min  << 5) | (ima->tm_sec  / 2);
    uint16_t zip_date = ((ima->tm_year - 80) << 9) | ((ima->tm_mon + 1) << 5) | (ima->tm_mday); //zip wants time from 1980 in a weird format >_>

    int skipped_num = 0;
    for (; dir_list[num_files] != NULL; num_files++) {
        //alright. for each file, let's compress the sucker >:3
        files = safe_alloc(files, (num_files + 1) * sizeof(struct HelioFile *));
        files[num_files] = safe_calloc(1, sizeof(struct HelioFile));
        files[num_files]->compressed_data = NULL;
        files[num_files]->method = -1; //if we skip, we know this file is invalid by the method~

        //now we read ^^
        char *file_path = helio_get_path(folder_path, dir_list[num_files]);
        if (helio_dir_exists(file_path)) { //if it's actually a directory!!
            free(file_path);
            free(dir_list[num_files]); //we tell our stuff later on to skip this >.<
            dir_list[num_files] = NULL;
            skipped_num++;
            continue; //we just continue, zip extractor doesn't care if we list directory paths neatly
        }

        //before anything else, we must see if this file is actually a symlink, and not blindly follow it.
        struct stat file_stat;
        if (lstat(file_path, &file_stat) == -1) {
            fprintf(stderr, "  * warning: could not stat %s, skipping...\n", file_path);
            continue;
        }
        if (S_ISLNK(file_stat.st_mode)) {
            //now this gets rather interesting. this file is a symlink, meaning we just store (0) where the link goes to =w=
            unsigned char sym_buf[1024] = {0};
            // ^ i think this is the only fixed length buffer we have that can technically overflow and segfault... 
            //bit unsafe, but it's an edge case to have 1024+ chars in a symlink
            
            ssize_t amt_read = readlink(file_path, (char *)sym_buf, sizeof(sym_buf) - 1);
            if (amt_read == -1) {
                fprintf(stderr, "  * warning: could not read symlink %s, skipping...\n", file_path);
                continue;
            }
            sym_buf[amt_read] = '\0'; //this needs manual null termination lol~
            
            //since we're just storing, we can set things like compressed and decompressed size rn
            files[num_files]->compressed_size = amt_read;
            files[num_files]->uncompressed_size = amt_read;
            files[num_files]->crc_uncompressed = crc32(0L, Z_NULL, 0);
            files[num_files]->crc_uncompressed = crc32(files[num_files]->crc_uncompressed, sym_buf, amt_read);
            files[num_files]->compressed_data = (unsigned char *)helio_strdup((char *)sym_buf);
            files[num_files]->method = 0x00; //store!!
        } else {
            FILE *file = fopen(file_path, "rb");
            if (!file) {
                fprintf(stderr, "  * warning: couldn't open file %s, skipping...\n", file_path);
                continue;
            }

            if (verbose) {
                printf("  %s\n", file_path);
            }
            free(file_path);

            int flush;
            z_stream strm = {0};
            unsigned char input_buf[MAX_CHUNK]; //unlike with decompression, we don't know ahead of time how much will be in each chunk~
            unsigned char output_buf[MAX_CHUNK];

            int deflate_ret = deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
            if (deflate_ret != Z_OK) {
                fprintf(stderr, "  * warning: error while compressing %s, skipping...\n", file_path);
                continue;
            }

            //also init crc32 calc. used for data verification ofc~
            files[num_files]->crc_uncompressed = crc32(0L, Z_NULL, 0);

            //compress chunks until we reach eof~
            size_t total_bytes_read = 0;
            do {
                strm.avail_in = fread(input_buf, 1, MAX_CHUNK, file);
                files[num_files]->crc_uncompressed = crc32(files[num_files]->crc_uncompressed, input_buf, strm.avail_in);
                files[num_files]->uncompressed_size += strm.avail_in;
                if (ferror(file)) { //if we read 0 bytes and it's eof, いいじゃん。そうでなければ、いいじゃないよ
                    deflateEnd(&strm);
                    fprintf(stderr, "  * warning: error while compressing %s, skipping...\n", file_path);
                    continue;
                }

                flush = feof(file)? Z_FINISH : Z_NO_FLUSH; //if it's eof, we say finish :D yay
                strm.next_in = input_buf;
                do {
                    strm.avail_out = MAX_CHUNK;
                    strm.next_out = output_buf;
                    int deflate_ret = deflate(&strm, flush);
                    if (deflate_ret == Z_STREAM_ERROR) {
                        fprintf(stderr, "  * warning: error while compressing %s, skipping...\n", file_path);
                        continue;
                    }

                    //write to total buffer now :D
                    files[num_files]->compressed_data = safe_alloc(files[num_files]->compressed_data, strm.total_out);
                    memcpy(files[num_files]->compressed_data + total_bytes_read, output_buf, strm.total_out - total_bytes_read);
                    total_bytes_read = strm.total_out;
                } while (strm.avail_out == 0);
            } while (flush != Z_FINISH);

            files[num_files]->compressed_size = total_bytes_read;
            files[num_files]->method = 0x08; //don't forget to set to deflate!!
            deflateEnd(&strm); //done with deflate :D
            fclose(file);
        }

        //alright~ we have the zip file open, now it's time to write the local header for this file >w<
        unsigned char local_header[30] = {0};
        memcpy(local_header, local_file_header, 4);
        local_header[4] = 0x14; //min version, always 0x14
        local_header[8] = files[num_files]->method; //we can store or deflate, symlinks are just store :p
        int_to_two_bytes(zip_time, local_header + 10);
        int_to_two_bytes(zip_date, local_header + 12);
        int_to_four_bytes(files[num_files]->crc_uncompressed, local_header + 14); //crc32~ data validation shit
        int_to_four_bytes(files[num_files]->compressed_size, local_header + 18); //compressed size
        int_to_four_bytes(files[num_files]->uncompressed_size, local_header + 22); //uncompressed size
        int_to_four_bytes(strlen(dir_list[num_files]), local_header + 26); //file name len

        size_t written = fwrite(local_header, 1, 30, zip_file);
        if (written != 30) return HELIO_FILESYSTEM_ERROR; //these are legit hare blocking errors that we'd like to Not just warn about =w=

        written = fwrite(dir_list[num_files], 1, strlen(dir_list[num_files]), zip_file);
        if (written != (unsigned long)strlen(dir_list[num_files])) return HELIO_FILESYSTEM_ERROR;

        written = fwrite(files[num_files]->compressed_data, 1, files[num_files]->compressed_size, zip_file);
        if (written != (unsigned long)files[num_files]->compressed_size) return HELIO_FILESYSTEM_ERROR;
    }

    //done with all of the files!!!! now, we need to do the central directory and eosd
    //each file gets its own fun little cd :3
    size_t cd_size = 0;
    long start_offset = ftell(zip_file); //get current pos >_<
    size_t file_dir_offset = initial_file_offset;
    for (int i = 0; i < num_files; i++) {
        if (dir_list[i] == NULL) continue; //ones that are dirs
        if (files[i]->method == -1) continue; //skipped above :3

        //ooh also!! getting unix permission bits for this file ^-^ only in cd ig
        struct stat file_stat;
        char *file_path = helio_get_path(folder_path, dir_list[i]);
        if (lstat(file_path, &file_stat) == -1) {
            return HELIO_FILESYSTEM_ERROR; //this should not happen, we've already skipped the files above that we couldn't stat/open
        }
        uint32_t permissions = file_stat.st_mode << 16; //making it happier for zip~
        free(file_path);

        unsigned char file_cd[46] = {0};
        memcpy(file_cd, cd_header, 4);
        file_cd[4] = 0x1E;
        file_cd[5] = 0x03; //unix host!! supports symlinks and type shit yo.
        file_cd[6] = 0x14; //min version, always 0x14
        file_cd[10] = files[i]->method; //HATE. HATE. LET ME TELL YOU HOW MUCH IVE- its actually not that bad
        int_to_two_bytes(zip_time, file_cd + 12);
        int_to_two_bytes(zip_date, file_cd + 14);
        int_to_four_bytes(files[i]->crc_uncompressed, file_cd + 16);
        int_to_four_bytes(files[i]->compressed_size, file_cd + 20); //compressed size
        int_to_four_bytes(files[i]->uncompressed_size, file_cd + 24); //uncompressed size
        int_to_two_bytes(strlen(dir_list[i]), file_cd + 28); //file name len~
        int_to_four_bytes(permissions, file_cd + 38);
        int_to_four_bytes(file_dir_offset, file_cd + 42);

        size_t written = fwrite(file_cd, 1, 46, zip_file);
        if (written != 46) return HELIO_FILESYSTEM_ERROR;

        //now also write filename~
        written = fwrite(dir_list[i], 1, strlen(dir_list[i]), zip_file);
        if (written != (unsigned long)strlen(dir_list[i])) return HELIO_FILESYSTEM_ERROR;

        cd_size += 46 + strlen(dir_list[i]);
        file_dir_offset += strlen(dir_list[i]) + files[i]->compressed_size + 30;
    }

    //eosd!!
    unsigned char embodiment_of_scarlet_devil[22] = {0};
    memcpy(embodiment_of_scarlet_devil, eocd_header, 4);
    int_to_two_bytes(num_files - skipped_num, embodiment_of_scarlet_devil + 8);
    int_to_two_bytes(num_files - skipped_num, embodiment_of_scarlet_devil + 10);
    int_to_four_bytes(cd_size, embodiment_of_scarlet_devil + 12);
    int_to_four_bytes(start_offset, embodiment_of_scarlet_devil + 16);

    size_t written = fwrite(embodiment_of_scarlet_devil, 1, 22, zip_file);
    if (written != 22) return HELIO_FILESYSTEM_ERROR;

    //finally, destroy everything >.<
    for (int i = 0; i < num_files; i++) {
        if (dir_list[i] != NULL) {
            free(dir_list[i]);
            free(files[i]->compressed_data);
        }
        free(files[i]);
    }
    free(files);
    free(dir_list);
    fclose(zip_file);

    return HELIO_SUCCESS;
}

char *helio_error_to_string(enum HelioReturnCode ret_code) {
    switch (ret_code) {
        case HELIO_SUCCESS: {
            return "success";
        }
        case HELIO_FILE_NOT_EXIST: {
            return "provided file does not exist";
        }
        case HELIO_FILE_INVALID: {
            return "provided file was invalid";
        }
        case HELIO_INVALID_COMPRESSION_METHOD: {
            return "zip file uses an unsupported compression method";
        }
        case HELIO_DEFLATE_ERROR: {
            return "error while uncompressing deflate stream";
        }
        case HELIO_FILESYSTEM_ERROR: {
            return "unable to read/write to the filesystem";
        }
    }
}

__attribute__((noreturn)) void memory_fail_exit(void) {
    fprintf(stderr, "memory allocation call failed, cannot continue execution >_<;;\n");
    abort();
}

//should also include safe versions of functions like malloc and whatever here~
//maybe also string function :0
void *safe_alloc(void *ptr, size_t bytes) {
    if (bytes == 0) return NULL; //0 alloc causes a NULL, and we read this as a fail >.<

    void *return_ptr = realloc(ptr, bytes);
    if (!return_ptr) memory_fail_exit();

    return return_ptr;
}

void *safe_calloc(size_t num_elements, size_t element_size) {
    if (num_elements == 0 || element_size == 0) return NULL;

    void *return_ptr = calloc(num_elements, element_size);
    if (!return_ptr) memory_fail_exit();

    return return_ptr;
}

void helio_del_strarr(char ***strarr) {
    if (strarr == NULL || *strarr == NULL) return; //already nulled out yo >_<

    for (char **cur_str = *strarr; *cur_str != NULL; cur_str++) {
        free(*cur_str);
        *cur_str = NULL;
    }
    free(*strarr);
    *strarr = NULL;
}

//only defining this since we may not have strdup in POSIX-2001
//and like... in windows lmfao
char *helio_strdup(const char *string) {
    size_t alloc_size = strlen(string) + 1; //take a WILD guess as to what the +1 is for. really.
    char *dup_string = safe_calloc(1, alloc_size);

    if (dup_string == NULL) return NULL;

    return memcpy(dup_string, string, alloc_size); //return pointer~
}

void helio_mkdir(const char *dir_path) { //makes parent directories too :3
    char *dir_copy = helio_strdup(dir_path);  //make a copy we can modify
    char *char_ptr = NULL;
    int mk_return = 0;
    if (dir_copy[strlen(dir_copy) - 1] == '/') { //we usually shouldn't get this with a slash at the end, but just in case~
        dir_copy[strlen(dir_copy) - 1] = 0;
    }

    for (char_ptr = dir_copy + 1; *char_ptr; char_ptr++) {
        if (*char_ptr == '/') { //we increase what character we're on until we get to a / :3
            *char_ptr = 0;
            mk_return = MKDIR(dir_copy);
            if (mk_return != 0 && errno != EEXIST && errno != EISDIR) {
                fprintf(stderr, "got error \"%s\" while trying to create directory %s >.<\n", strerror(errno), dir_path);
            }
            *char_ptr = '/';
        }
    }

    mk_return = MKDIR(dir_copy); //now we can make the final path yayyyy
    if (mk_return != 0 && errno != EEXIST && errno != EISDIR) {
        fprintf(stderr, "got error \"%s\" while trying to create directory %s >.<\n", strerror(errno), dir_path);
    }

    free(dir_copy);
}

bool helio_dir_exists(const char *directory) {
    #ifdef _WIN32
    DWORD attrib = GetFileAttributesA(directory); //fucked up windows shit
    return (attrib != INVALID_FILE_ATTRIBUTES && (attrib & FILE_ATTRIBUTE_DIRECTORY));
    #else
    struct stat dir_stat;
    return (stat(directory, &dir_stat) == 0 && S_ISDIR(dir_stat.st_mode));
    #endif
}

//super fucking useful!! caller must free >_<,,
char *helio_get_path(const char *prev_dir, const char *name) {
    size_t new_path_len = strlen(prev_dir) + strlen(name) + 2; //two new characters - / and \0
    char *path = safe_calloc(1, new_path_len);

    int length_written = sprintf(path, "%s/%s", prev_dir, name);
    if (length_written != (int)(new_path_len - 1)) { //im kinda sus of this but maybe this is just okay
        free(path);
        return NULL;
    }

    return path;
}

char **helio_list_dir(const char *directory, bool recusrive) {
    DIR *dir = opendir(directory);
    if (dir == NULL) {
        fprintf(stderr, "couldn't open directory %s @_@!! it likely isn't a directory.\n", directory);
        return NULL;
    }

    struct dirent *entry = readdir(dir);
    int dir_entries = 0; //first, let's count how many entries are right here =w=
    while (entry != NULL) {
        dir_entries++;
        entry = readdir(dir);
    }

    char **directory_entries = safe_calloc(dir_entries + 1, sizeof(char *));
    rewinddir(dir); //we're at the end of the directory after the while loop, so we need to rewind >.<

    int i = 0;
    while ((entry = readdir(dir)) != NULL) { //need another loop to read over the contents again~~
        if (entry->d_name[0] == '.') { //skip hidden files like ., .., .DS_Store, etc~
            continue;
        }

        //while some files don't have extentions of course, anything used here will likely have an extension and therefore
        //anything not having one will be a folder. we can recusively call ourselves to get the contents of each folder~
        char *new_dir_path = helio_get_path(directory, entry->d_name);
        if (helio_dir_exists(new_dir_path) && recusrive) { //none found
            char **new_directory_contents = helio_list_dir(new_dir_path, recusrive);
            if (new_directory_contents == NULL) {
                free(new_dir_path);
                continue;
            }

            //count the number of entries in the directory we have here >.<
            int num_new_files = 0;
            while (new_directory_contents[num_new_files] != NULL) {
                num_new_files++;
            }

            dir_entries += num_new_files;
            directory_entries = safe_alloc(directory_entries, (dir_entries + 1) * sizeof(char *));
            for (int j = 0; j < num_new_files; j++) {
                directory_entries[i + j] = helio_get_path(entry->d_name, new_directory_contents[j]);
                free(new_directory_contents[j]);
            }
            i += num_new_files;
            free(new_dir_path);
            free(new_directory_contents);
            continue;
        }
        free(new_dir_path);

        directory_entries[i] = helio_strdup(entry->d_name);
        i++;
    }

    directory_entries[i] = NULL; //the array needs to be null terminated too :3
    closedir(dir);

    return directory_entries;
}
