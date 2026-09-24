#ifndef HELIOTROPE_H_
#define HELIOTROPE_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

enum HelioReturnCode {
  HELIO_SUCCESS,
  HELIO_FILE_NOT_EXIST,
  HELIO_FILE_INVALID,
  HELIO_INVALID_COMPRESSION_METHOD,
  HELIO_DEFLATE_ERROR,
  HELIO_FILESYSTEM_ERROR
};

struct HelioFile {
  size_t uncompressed_size;
  size_t compressed_size;
  uint32_t crc_uncompressed;
  unsigned char *compressed_data; //only need to hold compressed in memory~
  int method;
  
  char *file_name;
  size_t file_name_len;
};

void helio_mkdir(const char *dir_path);
bool helio_dir_exists(const char *directory);
char *helio_get_path(const char *prev_dir, const char *name);
char **helio_list_dir(const char *directory, bool recusrive);

// (extract) leave base_directory NULL for ./ for
enum HelioReturnCode helio_extract(char *filename, bool verbose, char *base_directory, bool create_extract_folder, bool remove_after_extract);
enum HelioReturnCode helio_compress(char *folder_path, char *filename, char *extension, bool verbose);

void *safe_alloc(void *ptr, size_t bytes);
void *safe_calloc(size_t num_elements, size_t element_size);

#endif
