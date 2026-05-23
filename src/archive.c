#include "tarsau.h"          // Projenin ortak sabitlerini ve fonksiyon bildirimlerini kullanmak için eklenir.

#include <ctype.h>           // isdigit gibi karakter kontrol fonksiyonları için kullanılır.
#include <errno.h>           // strtoll gibi dönüşümlerde oluşan hata durumlarını kontrol etmek için kullanılır.
#include <limits.h>          // PATH_MAX gibi sistem sınırlarını kullanmak için eklenir.
#include <stdio.h>           // FILE, fopen, fread, fwrite, fprintf, perror gibi giriş/çıkış işlemleri için gereklidir.
#include <stdlib.h>          // malloc, free, strtoll, strtoull gibi bellek ve dönüşüm fonksiyonları için kullanılır.
#define _GNU_SOURCE          // strtok_r gibi GNU/POSIX tarafında gelen bazı fonksiyonların görünür olması için tanımlanır.
#include <string.h>          // strlen, strcmp, strncpy, strchr, memset gibi string işlemleri için kullanılır.
#include <sys/stat.h>        // stat, chmod ve dosya izin bilgileri için kullanılır.
#include <unistd.h>          // POSIX sistem çağrıları ve chmod gibi işlemler için eklenir.

// Arşivlenecek dosyaların bilgilerini tutan yapı.
// Bu struct, -b modunda her giriş dosyası için gerekli olan adı, yolu, izni ve boyutu saklar.
typedef struct {
    const char *input_path;                      // Dosyanın kullanıcı tarafından verilen gerçek yolu.
    char name[TARSAU_MAX_FILE_NAME + 1];         // Arşiv içine yazılacak güvenli dosya adı.
    mode_t mode;                                 // Dosyanın okuma, yazma ve çalıştırma izinleri.
    off_t size;                                  // Dosyanın byte cinsinden boyutu.
} ArchiveInput;

// Arşivden çıkarılacak dosyaların bilgilerini tutan yapı.
// Bu struct, .sau dosyasındaki metadata okununca her kayıt için doldurulur.
typedef struct {
    char name[TARSAU_MAX_FILE_NAME + 1];         // Arşivden çıkarılacak dosyanın adı.
    mode_t mode;                                 // Dosya çıkarıldıktan sonra geri verilecek izin bilgisi.
    off_t size;                                  // Arşivden okunacak içerik miktarı.
} ArchiveEntry;

// Veriyi bir dosyadan (source) diğerine (destination) chunk'lar (parçalar) halinde kopyalar.
// Büyük dosyaları tek seferde belleğe almak yerine 8192 byte'lık parçalarla okumak daha güvenlidir.
static int copy_exact_bytes(FILE *source, FILE *destination, off_t byte_count)
{
    unsigned char buffer[8192];                  // Dosya içeriğini parça parça taşımak için geçici tampon.
    off_t remaining = byte_count;                // Kopyalanması gereken kalan byte sayısı.

    while (remaining > 0) {                      // İstenen byte sayısı bitene kadar okumaya devam edilir.
        size_t chunk = remaining > (off_t)sizeof(buffer) ? sizeof(buffer) : (size_t)remaining; // Son parçada dosya boyutu bufferdan küçük olabilir, bu yüzden en küçük değer seçilir.
        size_t bytes_read = fread(buffer, 1, chunk, source); // Kaynak dosyadan belirlenen miktarda veri okunur.

        if (bytes_read == 0) {                   // Hiç veri okunamadıysa hata veya beklenmeyen dosya sonu vardır.
            if (ferror(source)) {                // Okuma sırasında gerçek bir dosya hatası oluşmuş mu diye bakılır.
                perror("Okuma hatasi");          // Sistem hata mesajı ekrana yazdırılır.
            } else {
                fprintf(stderr, "Arsiv dosyasi beklenenden once bitti.\n"); // Dosya erken bittiyse arşiv formatı bozuk kabul edilir.
            }
            return -1;                           // Kopyalama başarısız olduğu için hata kodu döndürülür.
        }

        if (fwrite(buffer, 1, bytes_read, destination) != bytes_read) { // Okunan verinin tamamı hedef dosyaya yazılamadıysa hata oluşmuştur.
            perror("Yazma hatasi");              // Yazma hatasının sistem açıklaması yazdırılır.
            return -1;                           // İşlem başarısız olarak sonlandırılır.
        }

        remaining -= (off_t)bytes_read;          // Başarıyla kopyalanan byte sayısı kalan miktardan düşülür.
    }

    return 0;                                    // Tüm byte'lar eksiksiz kopyalandıysa başarı döndürülür.
}

// Dosya bilgilerini (isim, izin, boyut) organizasyon bölümüne string olarak ekler.
// Metadata formatı ödevde istendiği gibi "dosya_adı,izin,boyut" ve kayıtlar arasında "|" olacak şekildedir.
static int append_metadata(char *metadata, size_t metadata_size, const ArchiveInput *info, int first)
{
    size_t used = strlen(metadata);              // Metadata içinde şu ana kadar kullanılan karakter sayısı hesaplanır.
    // |Dosya adi,izinler,boyut formatında string oluşturulur.
    int written = snprintf(
        metadata + used,                         // Yeni kayıt, mevcut metnin bittiği yerden itibaren yazılır.
        metadata_size - used,                    // Taşma olmaması için kalan buffer boyutu verilir.
        "%s%s,%04o,%lld",                        // Kayıt formatı: ayraç, dosya adı, izin, boyut.
        first ? "" : "|",                        // İlk kayıttan önce ayraç konmaz, sonraki kayıtların başına "|" eklenir.
        info->name,                              // Dosyanın arşive yazılacak adı.
        (unsigned int)(info->mode & 0777),       // İzin bilgisi sadece son 3 octal basamak olarak yazılır.
        (long long)info->size);                  // Dosya boyutu metin olarak metadata içine eklenir.

    if (written < 0 || (size_t)written >= metadata_size - used) { // snprintf başarısız olduysa veya buffer yetmediyse metadata çok büyüktür.
        fprintf(stderr, "Organizasyon bolumu cok buyuk.\n");
        return -1;                               // Metadata oluşturma işlemi başarısız kabul edilir.
    }

    return 0;                                    // Kayıt başarıyla metadata sonuna eklenmiştir.
}

// Arşivlenecek dosyalar arasında aynı isme sahip dosya olup olmadığını kontrol eder.
// Aynı isimli iki dosya arşivden çıkarılırken birbirinin üstüne yazabileceği için buna izin verilmez.
static int duplicate_name(const ArchiveInput *files, size_t count, const char *name)
{
    for (size_t i = 0; i < count; i++) {         // Daha önce kaydedilmiş dosya adları tek tek kontrol edilir.
        if (strcmp(files[i].name, name) == 0) {  // Yeni dosya adı önceki dosyalardan biriyle aynıysa çakışma vardır.
            return 1;                            // Aynı isim bulunduğunu belirtir.
        }
    }
    return 0;                                    // Aynı isim yoksa güvenli şekilde devam edilebilir.
}

// Girdi dosyalarının boyut, izin, isim ve format (ASCII) kontrollerini yapar.
// Bu fonksiyon -b modunun en önemli doğrulama adımıdır; sorun varsa arşiv oluşturulmadan çıkılır.
static int collect_input_info(const char **input_paths, size_t file_count, ArchiveInput *files)
{
    long long total_size = 0;                    // Tüm giriş dosyalarının toplam boyutunu takip eder.

    for (size_t i = 0; i < file_count; i++) {    // Kullanıcının verdiği her giriş dosyası sırayla kontrol edilir.
        const char *name = base_name(input_paths[i]);  // Yolun klasör kısmı atılır, sadece dosya adı alınır.
        struct stat st;                          // Dosyanın sistemdeki izin ve tür bilgilerini tutmak için kullanılır.
        off_t file_size = 0;                     // ASCII doğrulama fonksiyonu dosya boyutunu buraya yazar.

        // Dosya adı geçerli mi?
        if (!safe_file_name(name)) {             // Dosya adında '/', '\\', ',' veya '|' gibi tehlikeli karakterler olmamalıdır.
            fprintf(stderr, "%s giris dosyasinin formati uyumsuzdur!\n", name);
            return -1;                           // Geçersiz isim varsa arşivleme durdurulur.
        }

        // Aynı isimde dosya var mı?
        if (duplicate_name(files, i, name)) {    // Daha önce aynı base name ile dosya eklenmiş mi diye bakılır.
            fprintf(stderr, "%s dosya adi birden fazla kullanilamaz.\n", name);
            return -1;                           // Aynı isimli dosyalar arşive alınmaz.
        }

        // Dosya metin (ASCII) dosyası mı?
        int validation = validate_ascii_text_file(input_paths[i], name, &file_size);  // Dosyanın normal dosya olup olmadığı ve ASCII karakter içerip içermediği kontrol edilir.
        if (validation <= 0) {                    // 0 format uyumsuzluğu, negatif değer ise okuma hatası anlamına gelir.
            return -1;                           // Uygun olmayan dosyada işlem sonlandırılır.
        }

        // Dosya bilgilerini (stat) al.
        if (stat(input_paths[i], &st) != 0) {    // Dosyanın izin bilgilerini almak için stat çağrılır.
            perror(input_paths[i]);              // stat başarısız olursa sistem hatası yazdırılır.
            return -1;                           // Dosya bilgisi alınamadan arşiv oluşturulamaz.
        }

        // Toplam boyut 200MB limitini aşıyor mu?
        total_size += (long long)file_size;      // Her dosyanın boyutu toplam boyuta eklenir.
        if (total_size > TARSAU_MAX_TOTAL_SIZE) { // Ödev şartındaki 200 MB toplam sınırı aşılmış mı diye bakılır.
            fprintf(stderr, "Giris dosyalarinin toplam boyutu 200 MB'i gecemez.\n");
            return -1;                           // Limit aşılırsa arşivleme yapılmaz.
        }

        // Bilgileri struct içerisine kaydet.
        files[i].input_path = input_paths[i];    // Dosyanın gerçek yolu, içerik kopyalama sırasında kullanılmak üzere saklanır.
        files[i].mode = st.st_mode & 0777;       // Sadece kullanıcı/grup/diğer izinleri alınır.
        files[i].size = file_size;               // Dosya boyutu metadata ve kopyalama işlemi için saklanır.
        strncpy(files[i].name, name, TARSAU_MAX_FILE_NAME); // Dosya adı güvenli uzunluk sınırına göre struct içine kopyalanır.
        files[i].name[TARSAU_MAX_FILE_NAME] = '\0'; // Stringin her durumda sonlandırılmış olması garanti edilir.
    }

    return 0;                                    // Bütün giriş dosyaları arşivlemeye uygun bulunmuştur.
}

// Birden fazla metin dosyasını tek bir .sau arşivinde birleştiren ana fonksiyon (-b).
// Bu fonksiyon ödevdeki "tarsau -b" modunun arşiv oluşturma kısmını gerçekleştirir.
int create_archive(const char **input_paths, size_t file_count, const char *archive_path)
{
    // Maksimum dosya sayısı kontrolü (En fazla 32).
    if (file_count == 0 || file_count > TARSAU_MAX_FILES) { // Hiç dosya verilmemesi veya 32'den fazla dosya verilmesi kabul edilmez.
        fprintf(stderr, "Giris dosyasi sayisi 1 ile %d arasinda olmalidir.\n", TARSAU_MAX_FILES);
        return -1;                               // Geçersiz dosya sayısı için hata döndürülür.
    }

    ArchiveInput files[TARSAU_MAX_FILES];        // En fazla 32 dosyanın bilgisi bu dizide tutulur.
    memset(files, 0, sizeof(files));             // Dizi temizlenerek önceki rastgele bellek değerleri sıfırlanır.

    // Girdi dosyalarını doğrula ve bilgilerini topla.
    if (collect_input_info(input_paths, file_count, files) != 0) { // Dosya adı, türü, ASCII içeriği, izinleri ve boyutu kontrol edilir.
        return -1;                               // Herhangi bir doğrulama hatasında arşiv dosyası oluşturulmaz.
    }

    // Organizasyon (metadata) verisini oluştur.
    char metadata[16384] = "";                   // Dosya kayıtlarının tutulacağı geçici metadata alanı.
    for (size_t i = 0; i < file_count; i++) {    // Her dosyanın bilgisi metadata stringine sırayla eklenir.
        if (append_metadata(metadata, sizeof(metadata), &files[i], i == 0) != 0) { // Metadata bufferı yetmezse veya yazım hatası olursa işlem durur.
            return -1;
        }
    }

    // Çıktı arşiv dosyasını binary yazma modunda aç.
    FILE *archive = fopen(archive_path, "wb");   // .sau dosyası ikili yazma modunda oluşturulur.
    if (archive == NULL) {                       // Dosya açılamazsa izin veya yol hatası olabilir.
        perror(archive_path);                    // Hatanın sistem açıklaması yazdırılır.
        return -1;                               // Arşiv oluşturma başarısız olur.
    }

    // 1. Kısım: İlk 10 byte'a metadata uzunluğunu yaz.
    size_t metadata_len = strlen(metadata);      // Metadata bölümünün karakter/byte uzunluğu hesaplanır.
    if (fprintf(archive, "%010zu", metadata_len) != TARSAU_HEADER_SIZE) { // Uzunluk 10 haneli, başı sıfırlarla doldurulmuş şekilde yazılır.
        perror(archive_path);                    // Header yazılamadıysa dosya yazma hatası gösterilir.
        fclose(archive);                         // Açılmış dosya kapatılır.
        return -1;                               // İşlem başarısız kabul edilir.
    }

    // 2. Kısım: Organizasyon (metadata) verisini dosyaya yaz.
    if (fwrite(metadata, 1, metadata_len, archive) != metadata_len) { // Metadata bloğunun tamamı arşive yazılmalıdır.
        perror(archive_path);                    // Yazma hatası oluşursa kullanıcıya gösterilir.
        fclose(archive);                         // Dosya kapatılarak kaynak sızıntısı önlenir.
        return -1;
    }

    // 3. Kısım: Tüm dosyaların içeriklerini arka arkaya arşive ekle.
    for (size_t i = 0; i < file_count; i++) {    // Giriş dosyaları metadata sırasıyla arşive yazılır.
        FILE *input = fopen(files[i].input_path, "rb"); // Her dosya binary okuma modunda açılır; byte'lar aynen korunur.
        if (input == NULL) {                     // Dosya bu aşamada açılamazsa işlem tamamlanamaz.
            perror(files[i].input_path);
            fclose(archive);                     // Arşiv dosyası kapatılır.
            return -1;
        }

        int copy_result = copy_exact_bytes(input, archive, files[i].size); // Dosyanın tam boyutu kadar içerik arşive kopyalanır.
        fclose(input);                            // Giriş dosyası ile iş bitince kapatılır.

        if (copy_result != 0) {                  // Kopyalama sırasında okuma/yazma hatası oluşmuşsa işlem durdurulur.
            fclose(archive);
            return -1;
        }
    }

    if (fclose(archive) != 0) {                  // Arşiv dosyası kapatılırken de yazma hatası olup olmadığı kontrol edilir.
        perror(archive_path);
        return -1;
    }

    return 0;                                    // Arşiv başarıyla oluşturulmuştur.
}

// Arşiv metadatasındaki boyut stringini long integer'a dönüştürür.
// Boyut negatif olamaz; bu yüzden dönüşümden sonra ayrıca kontrol edilir.
static int parse_non_negative_size(const char *text, off_t *size_out)
{
    if (text == NULL || text[0] == '\0') {       // Boş veya NULL boyut alanı geçersizdir.
        return -1;
    }

    char *end = NULL;                            // strtoll dönüşümünün nerede bittiğini göstermek için kullanılır.
    errno = 0;                                   // Önceki hatalardan kalan errno değeri temizlenir.
    long long value = strtoll(text, &end, 10);   // Boyut alanı onluk tabanda sayıya çevrilir.

    if (errno != 0 || end == text || *end != '\0' || value < 0) { // Taşma, hiç sayı okuyamama, fazladan karakter veya negatif değer hatadır.
        return -1;
    }

    *size_out = (off_t)value;                    // Geçerli boyut çıktı parametresine yazılır.
    return 0;                                    // Dönüşüm başarılıdır.
}

// Arşiv metadatasındaki izin (mode) stringini sekizlik (octal) değere dönüştürür.
// Örneğin "0644" metni, dosya izni olarak 0644 değerine çevrilir.
static int parse_permissions(const char *text, mode_t *mode_out)
{
    if (text == NULL || text[0] == '\0') {       // İzin alanı boş olamaz.
        return -1;
    }

    for (const char *p = text; *p != '\0'; p++) { // İzin metnindeki her karakter kontrol edilir.
        if (*p < '0' || *p > '7') {              // Octal sayı yalnızca 0 ile 7 arasındaki rakamlardan oluşur.
            return -1;
        }
    }

    char *end = NULL;                            // strtol dönüşümünün bittiği konumu tutar.
    long value = strtol(text, &end, 8);          // İzin metni 8 tabanında sayıya çevrilir.
    if (end == text || *end != '\0' || value < 0 || value > 0777) { // Boş dönüşüm, fazladan karakter veya 0777 üstü değer geçersizdir.
        return -1;
    }

    *mode_out = (mode_t)value;                   // Geçerli izin değeri çıktı parametresine aktarılır.
    return 0;                                    // İzin alanı başarıyla ayrıştırılmıştır.
}

// Metadata içerisindeki tek bir dosya kaydını (isim,izin,boyut) ayrıştırır.
// Örnek kayıt: "t1.txt,0644,120" biçimindedir.
static int parse_record(char *record, ArchiveEntry *entry)
{
    char *first_comma = strchr(record, ',');     // Dosya adı ile izin alanını ayıran ilk virgül aranır.
    if (first_comma == NULL) {                   // İlk virgül yoksa kayıt formatı bozuktur.
        return -1;
    }

    char *second_comma = strchr(first_comma + 1, ','); // İzin ile boyut alanını ayıran ikinci virgül aranır.
    if (second_comma == NULL || strchr(second_comma + 1, ',') != NULL) { // İkinci virgül yoksa veya fazladan virgül varsa kayıt geçersizdir.
        return -1;
    }

    *first_comma = '\0';                         // İlk virgül string sonu yapılarak dosya adı ayrı bir metne çevrilir.
    *second_comma = '\0';                        // İkinci virgül de izin alanını sonlandırmak için kesilir.

    const char *name = record;                   // Kayıt başı artık dosya adını gösterir.
    const char *permissions = first_comma + 1;   // İlk virgülden sonraki bölüm izin metnidir.
    const char *size_text = second_comma + 1;    // İkinci virgülden sonraki bölüm boyut metnidir.

    if (!safe_file_name(name)) {                 // Arşivden çıkacak dosya adının güvenli olması gerekir.
        return -1;
    }

    if (parse_permissions(permissions, &entry->mode) != 0) { // İzin alanı octal ve 0777 sınırında olmalıdır.
        return -1;
    }

    if (parse_non_negative_size(size_text, &entry->size) != 0) { // Boyut alanı negatif olmayan geçerli bir sayı olmalıdır.
        return -1;
    }

    strncpy(entry->name, name, TARSAU_MAX_FILE_NAME); // Geçerli dosya adı ArchiveEntry içine kopyalanır.
    entry->name[TARSAU_MAX_FILE_NAME] = '\0';    // String sonlandırması garanti edilir.
    return 0;                                    // Kayıt başarıyla ayrıştırılmıştır.
}

// Tüm metadata bloğunu | ayracına göre parçalayarak dosya kayıtlarını oluşturur.
// Bu işlem sonucunda arşivin içinde kaç dosya olduğu ve her dosyanın bilgisi elde edilir.
static int parse_metadata(char *metadata, ArchiveEntry *entries, size_t *entry_count)
{
    size_t count = 0;                            // Şu ana kadar okunan geçerli kayıt sayısı.
    char *saveptr = NULL;                        // strtok_r için parçalama durumunu saklayan yardımcı işaretçi.
    char *record = strtok_r(metadata, "|", &saveptr); // Metadata ilk "|" ayracına göre parçalanmaya başlanır.

    while (record != NULL) {                     // Ayrıştırılacak kayıt kaldığı sürece döngü devam eder.
        if (count >= TARSAU_MAX_FILES) {         // Ödev şartına göre arşivde en fazla 32 dosya olabilir.
            return -1;
        }

        if (parse_record(record, &entries[count]) != 0) { // Her kayıt dosya adı, izin ve boyut olarak ayrıştırılır.
            return -1;                           // Hatalı kayıt varsa arşiv bozuk kabul edilir.
        }

        count++;                                 // Başarıyla okunan kayıt sayısı artırılır.
        record = strtok_r(NULL, "|", &saveptr);  // Sonraki metadata kaydına geçilir.
    }

    if (count == 0) {                            // Hiç kayıt yoksa geçerli bir arşivden söz edilemez.
        return -1;
    }

    *entry_count = count;                        // Toplam dosya sayısı çağıran fonksiyona aktarılır.
    return 0;                                    // Metadata başarıyla ayrıştırılmıştır.
}

// Arşivin ilk 10 bytelık kısmını okur ve organizasyon bölümünün boyutunu hesaplar.
// .sau formatında ilk 10 byte, metadata bölümünün kaç byte olduğunu ASCII sayı olarak tutar.
static int read_metadata_header(FILE *archive, size_t *metadata_len)
{
    char header[TARSAU_HEADER_SIZE + 1];         // 10 byte header ve sonlandırıcı '\0' için alan ayrılır.

    if (fread(header, 1, TARSAU_HEADER_SIZE, archive) != TARSAU_HEADER_SIZE) { // Header tam okunamazsa arşiv eksik veya bozuktur.
        fprintf(stderr, "Arsiv dosyasi formati hatali.\n");
        return -1;
    }

    header[TARSAU_HEADER_SIZE] = '\0';           // Okunan 10 byte C string olarak kullanılabilsin diye sonlandırılır.
    for (int i = 0; i < TARSAU_HEADER_SIZE; i++) { // Header içindeki her karakterin rakam olması gerekir.
        if (!isdigit((unsigned char)header[i])) {
            fprintf(stderr, "Arsiv dosyasi formati hatali.\n");
            return -1;
        }
    }

    *metadata_len = (size_t)strtoull(header, NULL, 10); // Headerdaki ASCII sayı size_t türüne çevrilir.
    if (*metadata_len == 0) {                    // Metadata uzunluğu sıfır olamaz; arşivde en az bir kayıt bulunmalıdır.
        fprintf(stderr, "Arsiv dosyasi formati hatali.\n");
        return -1;
    }

    return 0;                                    // Header geçerli şekilde okunmuştur.
}

// Başarılı çıkarma işlemi sonrası ekrana basılacak bilgi mesajını üretir.
// Mesaj, ödevde istenen şekilde hangi dizinde hangi dosyaların açıldığını bildirir.
static void print_extraction_message(const char *destination_dir, const ArchiveEntry *entries, size_t entry_count)
{
    printf("%s dizininde ", destination_dir);    // Mesajın başında hedef dizin yazdırılır.

    for (size_t i = 0; i < entry_count; i++) {   // Açılan dosya adları sırayla ekrana basılır.
        if (i > 0) {                             // İlk dosyadan önce ayraç yazılmaz.
            if (i + 1 == entry_count) {          // Son dosyadan önce Türkçe cümle yapısı için "ve" kullanılır.
                printf(" ve ");
            } else {
                printf(", ");                    // Ortadaki dosyalar virgül ile ayrılır.
            }
        }
        printf("%s", entries[i].name);           // Dosya adı ekrana yazdırılır.
    }

    printf(" dosyalari acildi.\n");              // Başarı mesajı tamamlanır.
}

// (Dahili extract fonksiyonu alternatifi)
// Bu fonksiyon .sau arşivini açar, metadata bölümünü okur ve dosyaları hedef dizine çıkarır.
int extract_archive(const char *archive_path, const char *destination_dir)
{
    FILE *archive = fopen(archive_path, "rb");   // Arşiv dosyası binary okuma modunda açılır.
    if (archive == NULL) {                       // Dosya açılamazsa yol hatalı veya dosya erişilemez olabilir.
        perror(archive_path);
        return 1;                                // Hata durumunda 1 döndürülür.
    }

    size_t metadata_len = 0;                     // Metadata uzunluğu headerdan okunduktan sonra burada tutulur.
    if (read_metadata_header(archive, &metadata_len) != 0) {// İlk 10 byte okunup geçerli sayı olup olmadığı kontrol edilir.
        fclose(archive);
        return 1;
    }

    char *metadata = malloc(metadata_len + 1);   // Metadata metni ve '\0' karakteri için bellek ayrılır.
    if (metadata == NULL) {                      // malloc başarısız olursa bellek yetersizdir.
        fprintf(stderr, "Bellek ayrilamadi.\n");
        fclose(archive);
        return 1;
    }

    if (fread(metadata, 1, metadata_len, archive) != metadata_len) {   // Headerda belirtilen kadar metadata okunamazsa arşiv bozuktur.
        fprintf(stderr, "Arsiv dosyasi formati hatali.\n");
        free(metadata);                          // Ayrılan bellek geri verilir.
        fclose(archive);
        return 1;
    }
    metadata[metadata_len] = '\0';               // Metadata string fonksiyonlarında kullanılabilsin diye sonlandırılır.

    ArchiveEntry entries[TARSAU_MAX_FILES];      // Metadata içindeki dosya kayıtları bu diziye aktarılır.
    size_t entry_count = 0;                      // Arşivdeki dosya sayısını tutar.
    if (parse_metadata(metadata, entries, &entry_count) != 0) {   // Metadata "ad,izin,boyut" kayıtlarına ayrıştırılır.
        fprintf(stderr, "Arsiv dosyasi formati hatali.\n");
        free(metadata);
        fclose(archive);
        return 1;
    }

    free(metadata);                              // Metadata artık entries dizisine aktarıldığı için bellek serbest bırakılır.

    if (ensure_directory(destination_dir) != 0) {    // Hedef dizin yoksa oluşturulur, varsa gerçekten dizin olduğu doğrulanır.
        fclose(archive);
        return 1;
    }

    for (size_t i = 0; i < entry_count; i++) {   // Arşivdeki her dosya sırayla dışarı çıkarılır.
        char output_path[PATH_MAX];              // Hedef dosyanın tam yolunu tutmak için buffer.
        if (join_path(output_path, sizeof(output_path), destination_dir, entries[i].name) != 0) {   // Hedef dizin ve dosya adı güvenli şekilde birleştirilir.
            fclose(archive);
            return 1;
        }

        FILE *output = fopen(output_path, "wb"); // Çıkarılacak dosya binary yazma modunda oluşturulur.
        if (output == NULL) {                    // Dosya oluşturulamazsa izin veya yol hatası olabilir.
            perror(output_path);
            fclose(archive);
            return 1;
        }

        int copy_result = copy_exact_bytes(archive, output, entries[i].size);// Arşivden dosyanın boyutu kadar veri okunup hedef dosyaya yazılır.
        if (fclose(output) != 0) {               // Çıktı dosyası kapatılırken hata olup olmadığı da kontrol edilir.
            perror(output_path);
            copy_result = -1;                    // Kapatma hatası da dosya yazma hatası sayılır.
        }

        if (copy_result != 0) {                  // İçerik eksik veya hatalı kopyalandıysa işlem durdurulur.
            fclose(archive);
            return 1;
        }

        if (chmod(output_path, entries[i].mode) != 0) { // Dosyanın eski izinleri, çıkarma işleminden sonra tekrar uygulanır.
            perror(output_path);
            fclose(archive);
            return 1;
        }
    }

    int trailing = fgetc(archive);               // Tüm dosyalar okunduktan sonra arşivde fazladan veri kalmış mı bakılır.
    if (trailing != EOF) {                       // EOF gelmiyorsa beklenmeyen fazladan byte vardır.
        fprintf(stderr, "Arsiv dosyasi formati hatali.\n");
        fclose(archive);
        return 1;
    }

    if (fclose(archive) != 0) {                  // Arşiv dosyası kapatılırken oluşabilecek hatalar kontrol edilir.
        perror(archive_path);
        return 1;
    }

    print_extraction_message(destination_dir, entries, entry_count);// Kullanıcıya hangi dosyaların açıldığı bilgisi verilir.
    return 0;                                                      // Çıkarma işlemi başarıyla tamamlanmıştır.
}

