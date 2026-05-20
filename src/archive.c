#include "tarsau.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#define _GNU_SOURCE
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    const char *input_path;
    char name[TARSAU_MAX_FILE_NAME + 1];
    mode_t mode;
    off_t size;
} ArchiveInput;

typedef struct {
    char name[TARSAU_MAX_FILE_NAME + 1];
    mode_t mode;
    off_t size;
} ArchiveEntry;

static int copy_exact_bytes(FILE *source, FILE *destination, off_t byte_count)
{
    unsigned char buffer[8192];
    off_t remaining = byte_count;

    while (remaining > 0) {
        size_t chunk = remaining > (off_t)sizeof(buffer) ? sizeof(buffer) : (size_t)remaining;
        size_t bytes_read = fread(buffer, 1, chunk, source);

        if (bytes_read == 0) {
            if (ferror(source)) {
                perror("Okuma hatasi");
            } else {
                fprintf(stderr, "Arsiv dosyasi beklenenden once bitti.\n");
            }
            return -1;
        }

        if (fwrite(buffer, 1, bytes_read, destination) != bytes_read) {
            perror("Yazma hatasi");
            return -1;
        }

        remaining -= (off_t)bytes_read;
    }

    return 0;
}

static int append_metadata(char *metadata, size_t metadata_size, const ArchiveInput *info, int first)
{
    size_t used = strlen(metadata);
    int written = snprintf(
        metadata + used,
        metadata_size - used,
        "%s%s,%04o,%lld",
        first ? "" : "|",
        info->name,
        (unsigned int)(info->mode & 0777),
        (long long)info->size);

    if (written < 0 || (size_t)written >= metadata_size - used) {
        fprintf(stderr, "Organizasyon bolumu cok buyuk.\n");
        return -1;
    }

    return 0;
}

static int duplicate_name(const ArchiveInput *files, size_t count, const char *name)
{
    for (size_t i = 0; i < count; i++) {
        if (strcmp(files[i].name, name) == 0) {
            return 1;
        }
    }

    return 0;
}

static int collect_input_info(const char **input_paths, size_t file_count, ArchiveInput *files)
{
    long long total_size = 0;

    for (size_t i = 0; i < file_count; i++) {
        const char *name = base_name(input_paths[i]);
        struct stat st;
        off_t file_size = 0;

        if (!safe_file_name(name)) {
            fprintf(stderr, "%s giris dosyasinin formati uyumsuzdur!\n", name);
            return -1;
        }

        if (duplicate_name(files, i, name)) {
            fprintf(stderr, "%s dosya adi birden fazla kullanilamaz.\n", name);
            return -1;
        }

        int validation = validate_ascii_text_file(input_paths[i], name, &file_size);
        if (validation <= 0) {
            return -1;
        }

        if (stat(input_paths[i], &st) != 0) {
            perror(input_paths[i]);
            return -1;
        }

        total_size += (long long)file_size;
        if (total_size > TARSAU_MAX_TOTAL_SIZE) {
            fprintf(stderr, "Giris dosyalarinin toplam boyutu 200 MB'i gecemez.\n");
            return -1;
        }

        files[i].input_path = input_paths[i];
        files[i].mode = st.st_mode & 0777;
        files[i].size = file_size;
        strncpy(files[i].name, name, TARSAU_MAX_FILE_NAME);
        files[i].name[TARSAU_MAX_FILE_NAME] = '\0';
    }

    return 0;
}

int create_archive(const char **input_paths, size_t file_count, const char *archive_path)
{
    if (file_count == 0 || file_count > TARSAU_MAX_FILES) {
        fprintf(stderr, "Giris dosyasi sayisi 1 ile %d arasinda olmalidir.\n", TARSAU_MAX_FILES);
        return -1;
    }

    ArchiveInput files[TARSAU_MAX_FILES];
    memset(files, 0, sizeof(files));

    if (collect_input_info(input_paths, file_count, files) != 0) {
        return -1;
    }

    char metadata[16384] = "";
    for (size_t i = 0; i < file_count; i++) {
        if (append_metadata(metadata, sizeof(metadata), &files[i], i == 0) != 0) {
            return -1;
        }
    }

    FILE *archive = fopen(archive_path, "wb");
    if (archive == NULL) {
        perror(archive_path);
        return -1;
    }

    size_t metadata_len = strlen(metadata);
    if (fprintf(archive, "%010zu", metadata_len) != TARSAU_HEADER_SIZE) {
        perror(archive_path);
        fclose(archive);
        return -1;
    }

    if (fwrite(metadata, 1, metadata_len, archive) != metadata_len) {
        perror(archive_path);
        fclose(archive);
        return -1;
    }

    for (size_t i = 0; i < file_count; i++) {
        FILE *input = fopen(files[i].input_path, "rb");
        if (input == NULL) {
            perror(files[i].input_path);
            fclose(archive);
            return -1;
        }

        int copy_result = copy_exact_bytes(input, archive, files[i].size);
        fclose(input);

        if (copy_result != 0) {
            fclose(archive);
            return -1;
        }
    }

    if (fclose(archive) != 0) {
        perror(archive_path);
        return -1;
    }

    return 0;
}

static int parse_non_negative_size(const char *text, off_t *size_out)
{
    if (text == NULL || text[0] == '\0') {
        return -1;
    }

    char *end = NULL;
    errno = 0;
    long long value = strtoll(text, &end, 10);

    if (errno != 0 || end == text || *end != '\0' || value < 0) {
        return -1;
    }

    *size_out = (off_t)value;
    return 0;
}

static int parse_permissions(const char *text, mode_t *mode_out)
{
    if (text == NULL || text[0] == '\0') {
        return -1;
    }

    for (const char *p = text; *p != '\0'; p++) {
        if (*p < '0' || *p > '7') {
            return -1;
        }
    }

    char *end = NULL;
    long value = strtol(text, &end, 8);
    if (end == text || *end != '\0' || value < 0 || value > 0777) {
        return -1;
    }

    *mode_out = (mode_t)value;
    return 0;
}

static int parse_record(char *record, ArchiveEntry *entry)
{
    char *first_comma = strchr(record, ',');
    if (first_comma == NULL) {
        return -1;
    }

    char *second_comma = strchr(first_comma + 1, ',');
    if (second_comma == NULL || strchr(second_comma + 1, ',') != NULL) {
        return -1;
    }

    *first_comma = '\0';
    *second_comma = '\0';

    const char *name = record;
    const char *permissions = first_comma + 1;
    const char *size_text = second_comma + 1;

    if (!safe_file_name(name)) {
        return -1;
    }

    if (parse_permissions(permissions, &entry->mode) != 0) {
        return -1;
    }

    if (parse_non_negative_size(size_text, &entry->size) != 0) {
        return -1;
    }

    strncpy(entry->name, name, TARSAU_MAX_FILE_NAME);
    entry->name[TARSAU_MAX_FILE_NAME] = '\0';
    return 0;
}

static int parse_metadata(char *metadata, ArchiveEntry *entries, size_t *entry_count)
{
    size_t count = 0;
    char *saveptr = NULL;
    char *record = strtok_r(metadata, "|", &saveptr);

    while (record != NULL) {
        if (count >= TARSAU_MAX_FILES) {
            return -1;
        }

        if (parse_record(record, &entries[count]) != 0) {
            return -1;
        }

        count++;
        record = strtok_r(NULL, "|", &saveptr);
    }

    if (count == 0) {
        return -1;
    }

    *entry_count = count;
    return 0;
}

static int read_metadata_header(FILE *archive, size_t *metadata_len)
{
    char header[TARSAU_HEADER_SIZE + 1];

    if (fread(header, 1, TARSAU_HEADER_SIZE, archive) != TARSAU_HEADER_SIZE) {
        fprintf(stderr, "Arsiv dosyasi formati hatali.\n");
        return -1;
    }

    header[TARSAU_HEADER_SIZE] = '\0';
    for (int i = 0; i < TARSAU_HEADER_SIZE; i++) {
        if (!isdigit((unsigned char)header[i])) {
            fprintf(stderr, "Arsiv dosyasi formati hatali.\n");
            return -1;
        }
    }

    *metadata_len = (size_t)strtoull(header, NULL, 10);
    if (*metadata_len == 0) {
        fprintf(stderr, "Arsiv dosyasi formati hatali.\n");
        return -1;
    }

    return 0;
}

static void print_extraction_message(const char *destination_dir, const ArchiveEntry *entries, size_t entry_count)
{
    printf("%s dizininde ", destination_dir);

    for (size_t i = 0; i < entry_count; i++) {
        if (i > 0) {
            if (i + 1 == entry_count) {
                printf(" ve ");
            } else {
                printf(", ");
            }
        }
        printf("%s", entries[i].name);
    }

    printf(" dosyalari acildi.\n");
}

int extract_archive(const char *archive_path, const char *destination_dir)
{
    FILE *archive = fopen(archive_path, "rb");
    if (archive == NULL) {
        perror(archive_path);
        return 1;
    }

    size_t metadata_len = 0;
    if (read_metadata_header(archive, &metadata_len) != 0) {
        fclose(archive);
        return 1;
    }

    char *metadata = malloc(metadata_len + 1);
    if (metadata == NULL) {
        fprintf(stderr, "Bellek ayrilamadi.\n");
        fclose(archive);
        return 1;
    }

    if (fread(metadata, 1, metadata_len, archive) != metadata_len) {
        fprintf(stderr, "Arsiv dosyasi formati hatali.\n");
        free(metadata);
        fclose(archive);
        return 1;
    }
    metadata[metadata_len] = '\0';

    ArchiveEntry entries[TARSAU_MAX_FILES];
    size_t entry_count = 0;
    if (parse_metadata(metadata, entries, &entry_count) != 0) {
        fprintf(stderr, "Arsiv dosyasi formati hatali.\n");
        free(metadata);
        fclose(archive);
        return 1;
    }

    free(metadata);

    if (ensure_directory(destination_dir) != 0) {
        fclose(archive);
        return 1;
    }

    for (size_t i = 0; i < entry_count; i++) {
        char output_path[PATH_MAX];
        if (join_path(output_path, sizeof(output_path), destination_dir, entries[i].name) != 0) {
            fclose(archive);
            return 1;
        }

        FILE *output = fopen(output_path, "wb");
        if (output == NULL) {
            perror(output_path);
            fclose(archive);
            return 1;
        }

        int copy_result = copy_exact_bytes(archive, output, entries[i].size);
        if (fclose(output) != 0) {
            perror(output_path);
            copy_result = -1;
        }

        if (copy_result != 0) {
            fclose(archive);
            return 1;
        }

        if (chmod(output_path, entries[i].mode) != 0) {
            perror(output_path);
            fclose(archive);
            return 1;
        }
    }

    int trailing = fgetc(archive);
    if (trailing != EOF) {
        fprintf(stderr, "Arsiv dosyasi formati hatali.\n");
        fclose(archive);
        return 1;
    }

    if (fclose(archive) != 0) {
        perror(archive_path);
        return 1;
    }

    print_extraction_message(destination_dir, entries, entry_count);
    return 0;
}