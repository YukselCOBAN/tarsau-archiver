#include "../include/tarsau.h"       // Ortak sabitler ve yardımcı fonksiyon bildirimleri için eklenir.

#include <ctype.h>                   // Karakter kontrol fonksiyonları için eklenir; bu dosyada doğrudan kullanılmasa da metin kontrolüyle ilişkilidir.
#include <errno.h>                   // Sistem hatalarıyla ilgili errno kullanımı için eklenir.
#include <stdio.h>                   // fprintf, perror, FILE, fopen, fread gibi giriş/çıkış işlemleri için kullanılır.
#include <stdlib.h>                  // Genel amaçlı standart kütüphane fonksiyonları için eklenir.
#include <string.h>                  // strlen, strcmp, strrchr gibi string işlemleri için kullanılır.
#include <sys/stat.h>                // stat, mkdir ve dosya türü/izin bilgileri için kullanılır.
#include <unistd.h>                  // POSIX sistem fonksiyonları için eklenir.
#include <sys/types.h>               // off_t ve mode_t gibi sistem türlerinin tanımları için kullanılır.

// Kullanıcıya programın argüman formatlarını gösterir.
// Yanlış parametre verildiğinde veya eksik kullanımda bu fonksiyon çağrılır.
void print_usage(const char *program_name)
{
    fprintf(stderr, "Kullanim:\n");                                      // Kullanım bilgisinin başlığı yazdırılır.
    fprintf(stderr, "  %s -b dosya1 dosya2 ... [-o arsiv.sau]\n", program_name);   // -b modunun, yani arşiv oluşturmanın doğru kullanımı gösterilir.
    fprintf(stderr, "  %s -a arsiv.sau [hedef_dizin]\n", program_name);   // -a modunun, yani arşiv açmanın doğru kullanımı gösterilir.
}

// Bir dosya yolunun sonundaki asıl dosya adını çeker (Örn: /klasor/test.txt -> test.txt).
// Arşive tam yol değil, sadece dosya adı yazılacağı için bu yardımcı fonksiyon kullanılır.
const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');       // Yol içindeki son '/' karakteri aranır.
    return slash == NULL ? path : slash + 1;      // '/' yoksa tüm metin dosya adıdır; varsa sonrasındaki bölüm döndürülür.
}

// Dosya adının güvenlik ve limit sınırları içinde olup olmadığını doğrular.
// Bu kontrol, arşivden çıkarma sırasında başka klasörlere yazma veya metadata bozma riskini azaltır.
int safe_file_name(const char *name)
{
    if (name == NULL || name[0] == '\0') {        // Dosya adı NULL veya boş olamaz.
        return 0;
    }

    if (strlen(name) > TARSAU_MAX_FILE_NAME) {    // Dosya adı proje içinde belirlenen maksimum uzunluğu aşmamalıdır.
        return 0;
    }

    // Özel ayrıştırıcı karakterleri dosya isminde kullanamaz.
    for (const char *p = name; *p != '\0'; p++) { // Dosya adındaki karakterler tek tek kontrol edilir.
        if (*p == '/' || *p == '\\' || *p == ',' || *p == '|') {  // '/' ve '\\' yol ayracı, ',' ve '|' ise metadata ayracı olduğu için yasaktır.
            return 0;
        }
    }

    return 1;                                     // Dosya adı güvenli ve geçerli kabul edilir.
}

// Dosyanın yalnızca ASCII (1 byte metin) karakterlerinden oluşup oluşmadığını tarar.
// Ödevde giriş dosyalarının sadece metin dosyası ve ASCII olması istendiği için bu kontrol zorunludur.
int validate_ascii_text_file(const char *path, const char *display_name, off_t *size_out)
{
    struct stat st;                               // Dosyanın türünü ve boyutunu öğrenmek için kullanılır.
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {  // Dosya yoksa, bilgisi alınamıyorsa veya normal dosya değilse kabul edilmez.
        fprintf(stderr, "%s giris dosyasinin formati uyumsuzdur!\n", display_name);
        return 0;
    }

    FILE *file = fopen(path, "rb");              // Dosya byte byte kontrol edileceği için binary okuma modunda açılır.
    if (file == NULL) {                          // Dosya açılamazsa izin veya yol hatası olabilir.
        perror(path);
        return -1;
    }

    unsigned char buffer[8192];                  // Dosya içeriğini parça parça okumak için tampon.
    size_t bytes_read;                           // Her fread çağrısında kaç byte okunduğunu tutar.
    // Dosya bitene kadar tüm baytları ASCII uyumluluğuna göre (0-127 arası) kontrol et.
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {  // Dosya 8192 byte'lık parçalar halinde okunur.
        for (size_t i = 0; i < bytes_read; i++) {  // Okunan parçadaki her byte ayrı ayrı kontrol edilir.
            unsigned char c = buffer[i];          // Kontrol edilen karakter unsigned olarak alınır.
            if (c == '\0' || c > 127) { // Sadece ASCII metinlere izin ver. NULL byte veya 127 üstü karakter metin/ASCII kabul edilmez.
                fclose(file);                     // Dosya kapatılarak kaynak sızıntısı önlenir.
                fprintf(stderr, "%s giris dosyasinin formati uyumsuzdur!\n", display_name);
                return 0;                         // Format uyumsuzluğu bildirilir.
            }
        }
    }

    if (ferror(file)) {                           // fread döngüsü dosya sonu dışında bir hata ile bittiyse kontrol edilir.
        perror(path);
        fclose(file);
        return -1;
    }

    fclose(file);                                 // ASCII kontrolü tamamlandıktan sonra dosya kapatılır.
    *size_out = st.st_size;                       // Dosyanın boyutu çağıran fonksiyona aktarılır.
    return 1;                                     // Dosya geçerli ASCII metin dosyasıdır.
}

// Hedef dizinin var olup olmadığını kontrol eder, yoksa yeni oluşturur (mkdir).
// -a modunda dosyaların çıkarılacağı klasörün hazır hale getirilmesini sağlar.
int ensure_directory(const char *path)
{
    struct stat st;                               // Yolun mevcut olup olmadığını ve türünü öğrenmek için kullanılır.
    if (stat(path, &st) == 0) {                   // Yol zaten varsa türü kontrol edilir.
        if (S_ISDIR(st.st_mode)) {                // Mevcut yol bir dizinse sorun yoktur.
            return 0; // Dizin zaten var.
        }
        fprintf(stderr, "%s bir dizin degil.\n", path);  // Aynı isimde dosya varsa hedef dizin olarak kullanılamaz.
        return -1;
    }

    // Dizin oluştur (0755 izinleri ile).
    if (mkdir(path, 0755) != 0) {                 // Dizin yoksa 0755 izinleriyle oluşturulur.
        perror(path);                             // mkdir başarısız olursa sistem hatası yazdırılır.
        return -1;
    }

    return 0;                                     // Dizin hazır durumdadır.
}

// Hedef dizin ismi ile dosya ismini uygun formatta uç uca ekler.
// Örneğin "cikti" ve "t1.txt" değerlerinden "cikti/t1.txt" yolu oluşturulur.
int join_path(char *buffer, size_t buffer_size, const char *dir, const char *file)
{
    int written = snprintf(buffer, buffer_size, "%s/%s", dir, file);   // Hedef dizin ve dosya adı araya '/' koyularak birleştirilir.
    if (written < 0 || (size_t)written >= buffer_size) {   // snprintf hata verirse veya buffer yetmezse yol fazla uzundur.
        fprintf(stderr, "Dosya yolu cok uzun.\n");
        return -1;
    }

    return 0;                                     // Yol güvenli şekilde oluşturulmuştur.
}

// Dosyanın .sau uzantısı ile bitip bitmediğini denetler.
// -a modunda uygun olmayan arşiv adı verilirse ödevde istenen hata mesajı basılmalıdır.
int has_sau_extension(const char *filename) {
    size_t len = strlen(filename);                // Dosya adının toplam uzunluğu hesaplanır.
    if (len < 5) return 0;                        // "a.sau" en kısa geçerli örnek olduğu için daha kısa adlar geçersizdir.
    return strcmp(filename + len - 4, ".sau") == 0;  // Son 4 karakter ".sau" ise dosya adı geçerli arşiv uzantısına sahiptir.
}

