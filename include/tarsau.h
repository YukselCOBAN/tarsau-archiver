#ifndef TARSAU_H
#define TARSAU_H

#include <stddef.h>
#include <sys/types.h>

#define TARSAU_HEADER_SIZE 10
#define TARSAU_MAX_FILES 32
#define TARSAU_MAX_FILE_NAME 255
#define TARSAU_MAX_TOTAL_SIZE (200LL * 1024LL * 1024LL)
#define TARSAU_DEFAULT_ARCHIVE "a.sau"

int create_archive(const char **input_paths, size_t file_count, const char *archive_path);
int extract_archive(const char *archive_path, const char *destination_dir);

void print_usage(const char *program_name);
const char *base_name(const char *path);
int safe_file_name(const char *name);
int validate_ascii_text_file(const char *path, const char *display_name, off_t *size_out);
int ensure_directory(const char *path);
int join_path(char *buffer, size_t buffer_size, const char *dir, const char *file);
int has_sau_extension(const char *filename);
#endif
