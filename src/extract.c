#include "../include/extract.h"     // handle_extract fonksiyonunun bildirimi ve extract ile ilgili ortak tanımlar için eklenir.
#include "../include/tarsau.h"      // has_sau_extension, ensure_directory, join_path gibi ortak yardımcı fonksiyonlar için eklenir.

#include <stdio.h>                  // FILE, fopen, fread, fwrite, printf, perror gibi giriş/çıkış işlemleri için gereklidir.
#include <stdlib.h>                 // malloc, free, atol, strtol gibi bellek ve dönüşüm fonksiyonları için kullanılır.
#include <string.h>                 // memset, strtok, strlen, strcpy gibi string işlemleri için kullanılır.
#include <sys/stat.h>               // chmod ile dosya izinlerini ayarlamak için eklenir.
#include <unistd.h>                 // POSIX sistem fonksiyonları için kullanılır.
#include <limits.h>                 // Sistem sınırları için eklenir; bu dosyada doğrudan kullanılmasa da yol işlemleriyle ilişkilidir.

// -a parametresi ile çağrıldığında arşivi açmakla görevli fonksiyon.
// Bu fonksiyon, verilen .sau dosyasını okur ve içindeki dosyaları hedef dizine çıkarır.
void handle_extract(const char *archive_name, const char *target_dir) {
    // 1. Uzantı Kontrolü: Dosya .sau uzantılı mı?
    if (!has_sau_extension(archive_name)) {      // Ödev şartına göre arşiv dosyasının adı ".sau" ile bitmelidir.
        printf("Arşiv dosyası uygunsuz veya bozuk!\n");
        return;                                  // Uygun uzantı yoksa işlem burada güvenli şekilde sonlandırılır.
    }

    // 2. Dosyayı İkili (Binary) Modda Aç.
    FILE *archive = fopen(archive_name, "rb");  // Arşiv dosyası byte byte okunacağı için binary modda açılır.
    if (!archive) {                              // Dosya yoksa, açılamıyorsa veya erişim izni yoksa hata verilir.
        printf("Arşiv dosyası uygunsuz veya bozuk!\n");
        return;
    }

    // 3. İlk 10 Byte'tan Metadata (Organizasyon) Uzunluğunu Oku.
    char header_size_str[11];                    // İlk 10 byte ve string sonlandırıcı için 11 karakterlik alan ayrılır.
    memset(header_size_str, 0, sizeof(header_size_str));  // Buffer sıfırlanır; böylece okunan değer güvenli şekilde string olur.
    if (fread(header_size_str, 1, 10, archive) != 10) {  // Arşivin ilk 10 byte'ı okunamazsa dosya eksik veya bozuktur.
        printf("Arşiv dosyası uygunsuz veya bozuk!\n");
        fclose(archive);                         // Açılan dosya kapatılır.
        return;
    }

    // Okunan ilk 10 byte değerini sayıya çevir (Örn: "0000000125" -> 125).
    long metadata_len_val = atol(header_size_str);  // Metadata bölümünün kaç byte olduğu sayıya çevrilir.
    if (metadata_len_val <= 0) {                  // Metadata uzunluğu sıfır veya negatif olamaz.
        printf("Arşiv dosyası uygunsuz veya bozuk!\n");
        fclose(archive);
        return;
    }

    size_t metadata_len = (size_t)metadata_len_val;  // Uzunluk, fread ve malloc ile uyumlu olması için size_t türüne çevrilir.

    // 4. Metadata Bloğunu Belleğe Al (Okuma işlemi).
    char *metadata_block = (char *)malloc(metadata_len + 1);   // Metadata metni ve '\0' karakteri için dinamik bellek ayrılır.
    if (!metadata_block) {                        // Bellek ayrılamazsa program çökmemesi için işlem durdurulur.
        fclose(archive);
        return;
    }

    if (fread(metadata_block, 1, metadata_len, archive) != metadata_len) {  // Headerda belirtilen kadar metadata okunamazsa arşiv formatı bozuktur.
        printf("Arşiv dosyası uygunsuz veya bozuk!\n");
        free(metadata_block);                     // Ayrılan bellek geri verilir.
        fclose(archive);
        return;
    }
    metadata_block[metadata_len] = '\0';          // Metadata string fonksiyonlarıyla işlenebilmesi için sonlandırılır.

    // 5. Hedef Dizin Ayarı (Parametre verilmediyse mevcut dizine ".").
    const char *dest = (target_dir != NULL) ? target_dir : ".";  // Kullanıcı hedef dizin vermediyse dosyalar geçerli dizine çıkarılır.
    if (ensure_directory(dest) != 0) {            // Hedef dizin yoksa oluşturulur; varsa dizin olup olmadığı kontrol edilir.
        free(metadata_block);
        fclose(archive);
        return;
    }

    // 6. Metadata Ayrıştırma: | ile kayıtları ayırıp struct içerisine atıyoruz.
    char *record = strtok(metadata_block, "|");   // İlk dosya kaydı "|" ayracına göre alınır.
    struct FileInfo {
        char name[256];                           // Metadata içinden gelen dosya adını tutar.
        int mode;                                 // Dosyanın izin bilgisini octal değerden çevrilmiş olarak tutar.
        long size;                                // Dosyanın arşivde kaç byte yer kapladığını tutar.
    } files[32];                                  // Ödevde belirtildiği gibi en fazla 32 dosya bilgisi saklanır.

    int file_count = 0;                           // Metadata içinden başarıyla okunan dosya sayısı.
    while (record != NULL && file_count < 32) {   // Kayıt olduğu sürece ve 32 dosya sınırı aşılmadan devam edilir.
        if (strlen(record) > 0) {                 // Boş kayıt varsa işlenmez.
            char file_name[256];                  // sscanf ile okunacak geçici dosya adı.
            char mode_str[10];                    // İzin metni önce string olarak alınır.
            long file_size;                       // Dosya boyutu metadata içinden long olarak okunur.

            // Virgülle ayrılmış formattan alanları çek (isim, izin, boyut).
            if (sscanf(record, "%[^,],%[^,],%ld", file_name, mode_str, &file_size) == 3) {  // Kayıt üç parçaya doğru ayrıldıysa dosya bilgisi kabul edilir.
                strcpy(files[file_count].name, file_name);  // Dosya adı geçici değişkenden files dizisine aktarılır.
                files[file_count].mode = (int)strtol(mode_str, NULL, 8);  // İzin metni 8 tabanından sayıya çevrilir.
                files[file_count].size = file_size;   // Dosyanın byte cinsinden boyutu saklanır.
                file_count++;                     // Geçerli dosya sayısı artırılır.
            }
        }
        record = strtok(NULL, "|");               // Bir sonraki metadata kaydına geçilir.
    }

    // 7. Dosya Verilerini Oku ve Hedefe Yaz.
    for (int i = 0; i < file_count; i++) {        // Metadata sırasına göre her dosya arşivden çıkarılır.
        char final_path[512];                     // Hedef dizin + dosya adından oluşan son dosya yolu.
        if (join_path(final_path, sizeof(final_path), dest, files[i].name) != 0) {  // Yol çok uzunsa veya birleştirilemezse işlem durdurulur.
            free(metadata_block);
            fclose(archive);
            return;
        }

        FILE *out_file = fopen(final_path, "wb"); // Çıkarılacak dosya binary yazma modunda oluşturulur.
        if (!out_file) {                          // Dosya oluşturulamazsa bu dosya atlanır.
            continue;
        }

        char buffer[8192];                        // Arşivden dosya içeriğini parça parça okumak için tampon.
        long remaining = files[i].size;           // Bu dosya için okunması gereken kalan byte sayısı.
        int read_error = 0;                       // Okuma veya yazma hatası olup olmadığını takip eder.

        // Dosya boyutuna göre veriyi arşivden parça parça okuyup hedef dosyaya yaz.
        while (remaining > 0) {                   // Dosyanın tüm içeriği kopyalanana kadar döngü devam eder.
            long chunk = (remaining > (long)sizeof(buffer)) ? (long)sizeof(buffer) : remaining;  // Her seferinde en fazla buffer kadar veri okunur.
            size_t bytes_read = fread(buffer, 1, (size_t)chunk, archive);   // Arşivden sıradaki içerik parçası okunur.

            if (bytes_read == 0) {                // Veri okunamadıysa arşiv erken bitmiş veya okuma hatası oluşmuştur.
                printf("Arşiv dosyası uygunsuz veya bozuk!\n");
                read_error = 1;                   // Hata bayrağı işaretlenir.
                fclose(out_file);                 // Açık çıktı dosyası kapatılır.
                break;                            // Bu dosya için kopyalama döngüsünden çıkılır.
            }

            if (fwrite(buffer, 1, bytes_read, out_file) != bytes_read) {  // Okunan verinin tamamı hedef dosyaya yazılamazsa hata vardır.
                read_error = 1;
                fclose(out_file);
                break;
            }
            remaining -= (long)bytes_read;        // Başarıyla yazılan byte sayısı kalan miktardan düşülür.
        }

        if (read_error) {                         // Okuma/yazma hatası oluştuysa diğer dosyalara geçilmez.
            break;
        }

        fclose(out_file);                         // Dosya başarıyla yazıldıktan sonra kapatılır.

        // Çıkarılan dosyanın izinlerini (read, write, execute) orijinal haline getir.
        if (chmod(final_path, files[i].mode) == -1) { // Arşivde saklanan izin bilgisi dosyaya yeniden uygulanır.
            perror("Dosya izinleri ayarlanamadi");
        }
    }

    free(metadata_block);                         // Metadata için ayrılan bellek serbest bırakılır.

    // 8. Dosya Sonu (Trailing) Doğrulama: Tüm veriler okunduktan sonra arşivin sonuna gelinmiş olmalı.
    int trailing = fgetc(archive);                // Dosya içerikleri okunduktan sonra fazladan byte kalıp kalmadığı kontrol edilir.
    fclose(archive);                              // Arşiv dosyası kapatılır.

    if (trailing != EOF) {                        // EOF gelmezse arşivde beklenmeyen fazladan veri var demektir.
        printf("Arşiv dosyası uygunsuz veya bozuk!\n");
        return;
    }

    // İşlem başarılıysa ekrana formatlı çıktı ver.
    printf("%s dizininde ", dest);                // Başarı mesajının başında hedef dizin yazdırılır.
    for (int i = 0; i < file_count; i++) {        // Açılan dosyaların isimleri tek tek yazdırılır.
        if (i > 0) {                              // İlk dosyadan önce ayraç koyulmaz.
            if (i + 1 == file_count) printf(" ve "); // Son dosyadan önce Türkçe cümle yapısı için "ve" yazılır.
            else printf(", ");                    // Aradaki dosyalar virgülle ayrılır.
        }
        printf("%s", files[i].name);              // Dosya adı mesaj içine eklenir.
    }
    printf(" dosyalari acildi.\n");               // Başarı mesajı tamamlanır.
}





