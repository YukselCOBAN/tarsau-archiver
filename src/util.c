#include "tarsau.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void print_usage(const char *program_name)
{
    fprintf(stderr, "Kullanim:\n");
    fprintf(stderr, "  %s -b dosya1 dosya2 ... [-o arsiv.sau]\n", program_name);
    fprintf(stderr, "  %s -a arsiv.sau hedef_dizin\n", program_name);
}

const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash == NULL ? path : slash + 1;
}

int safe_file_name(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return 0;
    }

    if (strlen(name) > TARSAU_MAX_FILE_NAME) {
        return 0;
    }

    for (const char *p = name; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\' || *p == ',' || *p == '|') {
            return 0;
        }
    }

    return 1;
}

int validate_ascii_text_file(const char *path, const char *display_name, off_t *size_out)
{
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        fprintf(stderr, "%s giris dosyasinin formati uyumsuzdur!\n", display_name);
        return 0;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        perror(path);
        return -1;
    }

    unsigned char buffer[8192];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        for (size_t i = 0; i < bytes_read; i++) {
            unsigned char c = buffer[i];
            if (c == '\0' || c > 127) {
                fclose(file);
                fprintf(stderr, "%s giris dosyasinin formati uyumsuzdur!\n", display_name);
                return 0;
            }
        }
    }

    if (ferror(file)) {
        perror(path);
        fclose(file);
        return -1;
    }

    fclose(file);
    *size_out = st.st_size;
    return 1;
}

int ensure_directory(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return 0;
        }
        fprintf(stderr, "%s bir dizin degil.\n", path);
        return -1;
    }

    if (mkdir(path, 0755) != 0) {
        perror(path);
        return -1;
    }

    return 0;
}

int join_path(char *buffer, size_t buffer_size, const char *dir, const char *file)
{
    int written = snprintf(buffer, buffer_size, "%s/%s", dir, file);
    if (written < 0 || (size_t)written >= buffer_size) {
        fprintf(stderr, "Dosya yolu cok uzun.\n");
        return -1;
    }

    return 0;
}