#include "../include/extract.h"
#include "../include/tarsau.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>

void handle_extract(const char *archive_name, const char *target_dir) {
    // 1. Uzantı Kontrolü
    if (!has_sau_extension(archive_name)) {
        printf("Arşiv dosyası uygunsuz veya bozuk!\n");
        return;
    }

    // 2. Dosyayı İkili (Binary) Modda Aç
    FILE *archive = fopen(archive_name, "rb");
    if (!archive) {
        printf("Arşiv dosyası uygunsuz veya bozuk!\n");
        return;
    }

    // 3. İlk 10 Byte'tan Metadata Uzunluğunu Oku
    char header_size_str[11];
    memset(header_size_str, 0, sizeof(header_size_str));
    if (fread(header_size_str, 1, 10, archive) != 10) {
        printf("Arşiv dosyası uygunsuz veya bozuk!\n");
        fclose(archive);
        return;
    }

    // Arkadaşın buraya doğrudan metadata uzunluğunu yazmış (Örn: "0000000125")
    long metadata_len_val = atol(header_size_str);
    if (metadata_len_val <= 0) {
        printf("Arşiv dosyası uygunsuz veya bozuk!\n");
        fclose(archive);
        return;
    }

    size_t metadata_len = (size_t)metadata_len_val;

    // 4. Metadata Bloğunu Belleğe Al
    char *metadata_block = (char *)malloc(metadata_len + 1);
    if (!metadata_block) {
        fclose(archive);
        return;
    }

    if (fread(metadata_block, 1, metadata_len, archive) != metadata_len) {
        printf("Arşiv dosyası uygunsuz veya bozuk!\n");
        free(metadata_block);
        fclose(archive);
        return;
    }
    metadata_block[metadata_len] = '\0';

    // 5. Hedef Dizin Ayarı
    const char *dest = (target_dir != NULL) ? target_dir : ".";
    if (ensure_directory(dest) != 0) {
        free(metadata_block);
        fclose(archive);
        return;
    }

    // 6. Metadata Ayrıştırma
    char *record = strtok(metadata_block, "|");
    struct FileInfo {
        char name[256];
        int mode;
        long size;
    } files[32];

    int file_count = 0;
    while (record != NULL && file_count < 32) {
        if (strlen(record) > 0) {
            char file_name[256];
            char mode_str[10];
            long file_size;

            if (sscanf(record, "%[^,],%[^,],%ld", file_name, mode_str, &file_size) == 3) {
                strcpy(files[file_count].name, file_name);
                files[file_count].mode = (int)strtol(mode_str, NULL, 8);
                files[file_count].size = file_size;
                file_count++;
            }
        }
        record = strtok(NULL, "|");
    }

    // 7. Dosya Verilerini Oku ve Hedefe Yaz
    for (int i = 0; i < file_count; i++) {
        char final_path[512];
        if (join_path(final_path, sizeof(final_path), dest, files[i].name) != 0) {
            free(metadata_block);
            fclose(archive);
            return;
        }

        FILE *out_file = fopen(final_path, "wb");
        if (!out_file) {
            continue; 
        }

        char buffer[8192];
        long remaining = files[i].size;
        int read_error = 0;

        while (remaining > 0) {
            long chunk = (remaining > (long)sizeof(buffer)) ? (long)sizeof(buffer) : remaining;
            size_t bytes_read = fread(buffer, 1, (size_t)chunk, archive);

            if (bytes_read == 0) {
                printf("Arşiv dosyası uygunsuz veya bozuk!\n");
                read_error = 1;
                fclose(out_file);
                break;
            }

            if (fwrite(buffer, 1, bytes_read, out_file) != bytes_read) {
                read_error = 1;
                fclose(out_file);
                break;
            }
            remaining -= (long)bytes_read;
        }

        if (read_error) {
            break;
        }

        fclose(out_file);

        if (chmod(final_path, files[i].mode) == -1) {
            perror("Dosya izinleri ayarlanamadi");
        }
    }

    free(metadata_block);

    // 8. Dosya Sonu (Trailing) Doğrulama
    int trailing = fgetc(archive);
    fclose(archive);

    if (trailing != EOF) {
        printf("Arşiv dosyası uygunsuz veya bozuk!\n");
        return;
    }

    // Arkadaşının beklediği başarı çıktısı
    printf("%s dizininde ", dest);
    for (int i = 0; i < file_count; i++) {
        if (i > 0) {
            if (i + 1 == file_count) printf(" ve ");
            else printf(", ");
        }
        printf("%s", files[i].name);
    }
    printf(" dosyalari acildi.\n");
}
