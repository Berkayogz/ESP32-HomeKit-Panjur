#include "HomeSpan.h"
#include <Preferences.h>

// ==========================================
// PİN TANIMLAMALARI (ESP32)
// ==========================================
#define PANJUR_ROLE1_PIN 25  // Ana Faz / Güç Kontrolü (Röle 1)
#define PANJUR_ROLE2_PIN 32  // Yön Seçimi (NC: Aşağı, NO: Yukarı) (Röle 2)

// ==========================================
// HOMEKIT PANJUR SERVİSİ
// ==========================================
struct Panjur : Service::WindowCovering {
  
  Characteristic::CurrentPosition *mevcutPozisyon;
  Characteristic::TargetPosition *hedefPozisyon;
  Characteristic::PositionState *durum;

  // --- Senin Gerçekleştirdiğin Hassas Ölçümler (ms) ---
  const uint32_t UP_DEAD_ZONE = 4100;    // Kumaşın katlanma/boşluk alma süresi
  const uint32_t UP_ACTIVE_MOVE = 19400; // Ray üzerindeki gerçek yükselme süresi
  const uint32_t DOWN_ACTIVE_MOVE = 18530; // Ray üzerindeki gerçek iniş süresi
  const uint32_t DOWN_DEAD_ZONE = 4370;  // Yere değdikten sonraki kapanma süresi
  
  // Kontak koruması ve akım sönümleme için sıralı geçiş gecikmesi
  const uint32_t SEQUENCING_DELAY = 150; 

  // Hareket Kontrol Değişkenleri
  uint32_t hareketBaslangic = 0;
  uint32_t gerekenToplamSure = 0;
  int baslangicYuzdesi = 0;
  int hedefYuzdesi = 0;
  int sonYon = 0; // 1: Yukarı, -1: Aşağı, 0: Durdu
  bool hareketHalinde = false;
  
  Preferences preferences;

  // ==========================================
  // CONSTRUCTOR (BAŞLATICI)
  // ==========================================
  Panjur() : Service::WindowCovering() {
    // Non-volatile storage (NVS) üzerinden son pozisyonu oku
    preferences.begin("panjur", false);
    int kaydedilmis = preferences.getInt("pozisyon", 0);
    preferences.end();

    mevcutPozisyon = new Characteristic::CurrentPosition(kaydedilmis);
    hedefPozisyon = new Characteristic::TargetPosition(kaydedilmis);
    durum = new Characteristic::PositionState(2); // 2: STOPPED

    // GPIO Ayarları
    pinMode(PANJUR_ROLE1_PIN, OUTPUT);
    pinMode(PANJUR_ROLE2_PIN, OUTPUT);
    
    // İlk enerjilenmede güvenli durum: Tüm röleler kapalı (Güç kesik)
    digitalWrite(PANJUR_ROLE1_PIN, HIGH); 
    digitalWrite(PANJUR_ROLE2_PIN, HIGH);

    Serial.printf("[PANJUR] Donanım Başlatıldı. Hafızadaki Konum: %%%d\n", kaydedilmis);
  }

  // ==========================================
  // HOMEKIT GÜNCELLEME TETİKLEYİCİSİ (Siri/App)
  // ==========================================
  boolean update() override {
    hedefYuzdesi = hedefPozisyon->getNewVal<int>();
    baslangicYuzdesi = mevcutPozisyon->getVal<int>();

    // Eğer zaten hedefteyse hiçbir şey yapma
    if (hedefYuzdesi == baslangicYuzdesi) {
      panjurDurdur();
      return true;
    }

    // Yön tayini
    sonYon = (hedefYuzdesi > baslangicYuzdesi) ? 1 : -1;
    float mesafeOrani = abs(hedefYuzdesi - baslangicYuzdesi) / 100.0;

    // --- DOĞRUSAL OLMAYAN (NON-LINEAR) SÜRE MODELLEMESİ ---
    if (sonYon == 1) { // YUKARI HAREKET
      gerekenToplamSure = (uint32_t)(mesafeOrani * UP_ACTIVE_MOVE);
      // Panjur dipten kalkıyorsa ölü bölge (katlanma) süresini ekle
      if (baslangicYuzdesi == 0) gerekenToplamSure += UP_DEAD_ZONE;
    } 
    else { // AŞAĞI HAREKET
      gerekenToplamSure = (uint32_t)(mesafeOrani * DOWN_ACTIVE_MOVE);
      // Panjur tam kapanacaksa yere değdikten sonraki sıkıştırma süresini ekle
      if (hedefYuzdesi == 0) gerekenToplamSure += DOWN_DEAD_ZONE;
    }

    Serial.printf("[PANJUR] Komut Alındı: %%%d -> %%%d | Hesaplanan Süre: %d ms\n", baslangicYuzdesi, hedefYuzdesi, gerekenToplamSure);
    
    panjurHareketBaslat();
    return true;
  }

  // ==========================================
  // SIRALI ANAHTARLAMA (SEQUENCING) MANTIĞI
  // ==========================================
  void panjurHareketBaslat() {
    // 1. ADIM: Önce akım geçmiyorken yön rölesini (Röle 2) konumlandır
    if (sonYon == 1) {
      digitalWrite(PANJUR_ROLE2_PIN, LOW); // YUKARI (NO)
      durum->setVal(0); // HomeKit Durumu: OPENING
    } else {
      digitalWrite(PANJUR_ROLE2_PIN, HIGH); // AŞAĞI (NC)
      durum->setVal(1); // HomeKit Durumu: CLOSING
    }

    // 2. ADIM: Kontak mekanik olarak yerine oturana kadar bekle
    delay(SEQUENCING_DELAY); 

    // 3. ADIM: Şimdi ana güç rölesinden (Röle 1) fazı ver
    digitalWrite(PANJUR_ROLE1_PIN, LOW); 
    
    hareketBaslangic = millis();
    hareketHalinde = true;
  }

  void panjurDurdur() {
    // 1. ADIM: Arkı önlemek için önce ana gücü (Röle 1 - Faz) kes
    digitalWrite(PANJUR_ROLE1_PIN, HIGH); 
    Serial.println("[PANJUR] Güç kesildi (Röle 1)");
    
    // 2. ADIM: Endüktif akımın sönümlenmesi ve arkın bitmesi için bekle
    delay(SEQUENCING_DELAY); 

    // 3. ADIM: Yön rölesini (Röle 2) akımsız durumdayken serbest bırak
    digitalWrite(PANJUR_ROLE2_PIN, HIGH); 
    Serial.println("[PANJUR] Yön rölesi serbest (Röle 2)");

    hareketHalinde = false;
    sonYon = 0;
    durum->setVal(2); // HomeKit Durumu: STOPPED
    
    // Güncel konumu NVS'e kaydet
    preferences.begin("panjur", false);
    preferences.putInt("pozisyon", mevcutPozisyon->getVal<int>());
    preferences.end();
  }

  // ==========================================
  // ARKA PLAN ZAMAN TAKİBİ (LOOP)
  // ==========================================
  void loop() override {
    if (!hareketHalinde) return;

    uint32_t gecenSure = millis() - hareketBaslangic;

    if (gecenSure < gerekenToplamSure) {
      // HomeKit arayüzünde kaydırıcının (slider) senkronize akması için ara yüzde hesabı
      float ilerleme = (float)gecenSure / (float)gerekenToplamSure;
      int anlik;

      if (sonYon == 1) {
        anlik = baslangicYuzdesi + (int)((hedefYuzdesi - baslangicYuzdesi) * ilerleme);
      } else {
        anlik = baslangicYuzdesi - (int)((baslangicYuzdesi - hedefYuzdesi) * ilerleme);
      }
      
      // Gereksiz ağ trafiğini engellemek için sadece değer değiştiğinde güncelle
      if(anlik != mevcutPozisyon->getVal<int>()) {
        mevcutPozisyon->setVal(anlik);
      }
    } 
    else {
      // Süre tam olarak doldu, nihai hedefi yaz ve akımı kes
      mevcutPozisyon->setVal(hedefYuzdesi);
      panjurDurdur();
    }
  }
};