# TARSAU - Sıkıştırmasız Arşivleme Programı

## Proje Açıklaması
Tarsau, Linux/Unix ortamında C dili ile geliştirilmiş, tar/zip benzeri çalışan ancak sıkıştırma yapmayan bir arşivleme programıdır.

## Çalışma Modları

### -b (Arşivleme)
- ASCII metin dosyalarını birleştirir
- En fazla 32 dosya
- Toplam boyut 200 MB sınırı
- Hatalı dosyada program güvenli şekilde sonlanır

Örnek:
./bin/tarsau -b t1.txt t2.txt

Hata:
t7 giriş dosyasının formatı uyumsuzdur!

---

### -a (Arşiv Açma)
- .sau dosyalarını açar
- İsteğe bağlı hedef dizin
- Dizin yoksa oluşturulur
- Dosya izinleri korunur (rwx)

Örnek:
./bin/tarsau -a arsiv.sau output

Hata:
Arşiv dosyası uygunsuz veya bozuk!

---

## .SAU Dosya Formatı

### 1. Metadata Bölümü
- İlk 10 byte: metadata boyutu (ASCII sayı)
- Kayıt formatı:
|dosya_adi,izinler,boyut|

### 2. Veri Bölümü
- Dosya içerikleri ardışık yazılır
- Ayraç yoktur

---

## Hata Yönetimi
- Geçersiz dosya → format uyumsuz
- Bozuk arşiv → uygunsuz veya bozuk!
- Fazla dosya → maksimum 32
- Program tüm hatalarda güvenli şekilde sonlanır (no crash)

---

## Test Senaryoları

Normal:
echo "hello" > t1.txt
echo "world" > t2.txt
./bin/tarsau -b t1.txt t2.txt

Binary test:
dd if=/dev/urandom of=bad.bin bs=10 count=1
./bin/tarsau -b bad.bin

Extract:
./bin/tarsau -a arsiv.sau output

---

## Kullanılan Teknolojiler
- C (GNU11)
- Linux system calls
- File I/O (fopen, fread, fwrite)
- Directory operations (mkdir)
- Permission handling (chmod)

---

## Geliştirme Süreci
- GitHub üzerinde geliştirilmiştir
- tarsau-b ve tarsau-a branch’leri kullanılmıştır
- Git ile versiyon kontrolü yapılmıştır
- Makefile ile otomatik derleme sağlanmıştır

## Derleme
make

Temizleme
make clean