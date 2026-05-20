#include "tarsau.h"

#include <stdio.h>
#include <string.h>

static int run_build_mode(int argc, char **argv)
{
    const char *inputs[TARSAU_MAX_FILES];
    const char *archive_path = TARSAU_DEFAULT_ARCHIVE;
    size_t input_count = 0;
    int output_seen = 0;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (output_seen || i + 1 >= argc) {
                print_usage(argv[0]);
                return 1;
            }
            archive_path = argv[++i];
            output_seen = 1;
            continue;
        }

        if (input_count >= TARSAU_MAX_FILES) {
            fprintf(stderr, "Giris dosyasi sayisi en fazla %d olabilir.\n", TARSAU_MAX_FILES);
            return 1;
        }

        inputs[input_count++] = argv[i];
    }

    if (input_count == 0) {
        print_usage(argv[0]);
        return 1;
    }

    if (create_archive(inputs, input_count, archive_path) != 0) {
        return 1;
    }

    printf("Dosyalar birlestirildi.\n");
    return 0;
}

static int run_extract_mode(int argc, char **argv)
{
    if (argc != 4) {
        print_usage(argv[0]);
        return 1;
    }

    return extract_archive(argv[2], argv[3]);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "-b") == 0) {
        return run_build_mode(argc, argv);
    }

    if (strcmp(argv[1], "-a") == 0) {
        return run_extract_mode(argc, argv);
    }

    print_usage(argv[0]);
    return 1;
}