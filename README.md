# ESP32 & HomeSpan ile Akıllı Panjur Kontrol Sistemi

Bu proje, standart bir 220V AC motorlu panjuru Apple HomeKit (Siri) entegrasyonu ile akıllı ve hassas hale getirmek için geliştirilmiştir. Projede mekanik ve elektriksel güvenliği artırmak amacıyla donanımsal/yazılımsal interlock, sıralı anahtarlama (sequencing) ve endüktif yük koruma teknikleri kullanılmıştır.

## 🛠️ Özellikler & Mühendislik Yaklaşımları

* **Doğrusal Olmayan Zaman Haritalama (Non-Linear Mapping):** Panjurun ilk kalkıştaki katlanma (ölü bölge) süreleri ile ray üzerindeki aktif hareket süreleri ayrı ayrı milisaniye mertebesinde ölçülmüştür. Bu sayede HomeKit üzerindeki `%50` gibi ara değerlerin fiziksel yükseklikle milimetrik örtüşmesi sağlanmıştır.
* **Sıralı Anahtarlama (Sequencing):** Yüksek endüktif yüklerde kontak yapışmasını ve ark oluşumunu engellemek için hareket bitiminde önce ana güç rölesi (Faz) kapatılmakta, `150ms` sonra yön rölesi akımsız durumdayken bırakılmaktadır.
* **Kalıcı Hafıza Entegrasyonu:** ESP32'nin `Preferences` kütüphanesi kullanılarak panjurun son konumu NVS (Non-Volatile Storage) üzerinde saklanır; böylece elektrik kesintilerinde konum senkronizasyonu kaybolmaz.

## 📊 Kalibrasyon Verileri (Milisaniye)

* **Yukarı Hareket:** 4100 ms Katlanma + 19400 ms Aktif Hareket (Toplam: 23500 ms)
* **Aşağı Hareket:** 18530 ms Aktif Hareket + 4370 ms Kapanma/Sıkıştırma (Toplam: 22900 ms)

## 📌 Pin Diyagramı (ESP32)

* `GPIO 25` -> Ana Faz / Güç Kontrol Rölesi (Röle 1)
* `GPIO 32` -> Yön Seçim Rölesi (Röle 2 - NC: Aşağı, NO: Yukarı)
