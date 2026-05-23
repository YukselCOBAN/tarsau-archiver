#include "../include/tarsau.h"       // Arşiv oluşturma, kullanım mesajı ve sabitler için ortak başlık dosyası.
#include "../include/extract.h"      // -a modunda çağrılan handle_extract fonksiyonunun bildirimi için eklenir.

#include <stdio.h>                   // printf ve fprintf gibi ekrana çıktı işlemleri için kullanılır.
#include <string.h>                  // strcmp ile komut satırı parametrelerini karşılaştırmak için kullanılır.

// -b parametresi ile program çalıştırıldığında (Arşivleme Modu) tetiklenir.
// Bu fonksiyon komut satırındaki giriş dosyalarını ve isteğe bağlı -o arşiv adını ayrıştırır.
static int run_build_mode(int argc, char **argv)
{
    const char *inputs[TARSAU_MAX_FILES];        // Arşivlenecek giriş dosyalarının yollarını tutan dizi.
    const char *archive_path = TARSAU_DEFAULT_ARCHIVE; // Varsayılan arşiv adı a.sau.
    size_t input_count = 0;                      // Kullanıcının verdiği giriş dosyası sayısı.
    int output_seen = 0;                         // -o parametresinin daha önce kullanılıp kullanılmadığını takip eder.

    // Argümanları tara, giriş dosyalarını al ve -o parametresi ile özel isim belirtilmişse tespit et.
    for (int i = 2; i < argc; i++) {             // argv[0] program adı, argv[1] "-b" olduğu için tarama 2. indisten başlar.
        if (strcmp(argv[i], "-o") == 0) {        // Kullanıcı çıktı arşiv adını belirtmek istiyorsa -o kullanır.
            if (output_seen || i + 1 >= argc) {  // -o iki kez verilmişse veya ardından dosya adı yoksa kullanım hatasıdır.
                print_usage(argv[0]);            // Doğru kullanım şekli ekrana yazdırılır.
                return 1;                        // Hatalı kullanım için program hata kodu döndürür.
            }
            archive_path = argv[++i];            // -o'dan sonraki argüman arşiv dosyasının adı olarak alınır.
            output_seen = 1;                     // -o parametresinin kullanıldığı işaretlenir.
            continue;                            // Bu argüman dosya girişi olarak değerlendirilmez.
        }

        if (input_count >= TARSAU_MAX_FILES) {   // Ödev şartına göre en fazla 32 giriş dosyası kabul edilir.
            fprintf(stderr, "Giris dosyasi sayisi en fazla %d olabilir.\n", TARSAU_MAX_FILES);
            return 1;                            // Limit aşılırsa arşivleme başlatılmaz.
        }

        inputs[input_count++] = argv[i];         // Normal argümanlar giriş dosyası listesine eklenir.
    }

    if (input_count == 0) {                      // -b modunda en az bir giriş dosyası verilmelidir.
        print_usage(argv[0]);
        return 1;
    }

    // Arşiv oluşturma fonksiyonunu çağır.
    if (create_archive(inputs, input_count, archive_path) != 0) {  // Dosya doğrulama veya arşiv yazma sırasında hata olursa başarısız döner.
        return 1;
    }

    printf("Dosyalar birlestirildi.\n");         // Arşivleme başarılı olursa kullanıcıya bilgi mesajı verilir.
    return 0;                                    // Başarılı çıkış kodu.
}

// -a parametresi ile program çalıştırıldığında (Çıkarma Modu) tetiklenir.
// Bu fonksiyon arşiv adını ve varsa hedef dizin parametresini alıp çıkarma fonksiyonuna gönderir.
static int run_extract_mode(int argc, char **argv)
{
    // -a parametresi için argüman sayısı 3 veya 4 olmalıdır.
    // Örn: ./tarsau -a test.sau  (argc = 3)
    // Örn: ./tarsau -a test.sau hedef_dir (argc = 4)
    if (argc < 3 || argc > 4) {                  // Arşiv adı zorunlu, hedef dizin ise isteğe bağlıdır.
        print_usage(argv[0]);                    // Yanlış sayıda parametre verilirse kullanım bilgisi gösterilir.
        return 1;
    }

    const char *archive_name = argv[2];          // -a'dan sonraki ilk parametre arşiv dosyasının adıdır.
    const char *target_dir = (argc == 4) ? argv[3] : NULL;   // Dördüncü parametre varsa hedef dizindir, yoksa NULL gönderilir.

    // Çıkarma işlemine özel hazırlanan handle_extract fonksiyonunu tetikler.
    handle_extract(archive_name, target_dir);    // Arşiv açma işleminin asıl yükü bu fonksiyonda yapılır.
    return 0;                                    // handle_extract kendi mesajlarını bastığı için burada 0 dönülür.
}

// Programın başlangıç (Main) noktası.
// Komut satırından gelen ilk parametreye göre programı arşivleme veya çıkarma moduna yönlendirir.
int main(int argc, char **argv)
{
    // En az 2 argüman olmalı (programın adı ve çalıştırılacak bayrak: -b veya -a).
    if (argc < 2) {                              // Kullanıcı hiç mod belirtmemişse program ne yapacağını bilemez.
        print_usage(argv[0]);                    // Doğru kullanım şekli gösterilir.
        return 1;
    }

    // Birleştirme Modu.
    if (strcmp(argv[1], "-b") == 0) {            // İlk argüman -b ise arşiv oluşturma modu çalıştırılır.
        return run_build_mode(argc, argv);
    }

    // Çıkarma Modu.
    if (strcmp(argv[1], "-a") == 0) {            // İlk argüman -a ise arşiv açma modu çalıştırılır.
        return run_extract_mode(argc, argv);
    }

    // Hatalı veya eksik kullanımda kullanım şablonunu göster.
    print_usage(argv[0]);                        // -b veya -a dışında bir parametre verilirse kullanıcı bilgilendirilir.
    return 1;                                    // Geçersiz mod için hata kodu döndürülür.
}