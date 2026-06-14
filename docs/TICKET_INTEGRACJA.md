# 🎫 Ticket: Integracja EminentFeedSystem — test komunikacji ESP32 ↔ Mac 

---

## Cel

Przetestować bibliotekę `EminentFeedSystem` w realnym scenariuszu: **ESP32 z czujnikiem** wysyła telemetrię przez WiFi do **aplikacji na Macu**, a Mac odsyła komendy sterujące silnikiem. Komunikacja zaszyfrowana (ChaCha20).

---

## Wymagania sprzętowe

- ESP32 (dowolny wariant: ESP32, ESP32-S3, itp.)
- Czujnik (np. MPU6050, BME280, enkoder — cokolwiek z I2C/SPI)
- Silnik DC + sterownik (np. L298N, lub serwomechanizm z PWM)
- Mac z WiFi
- Wspólna sieć WiFi (ESP32 i Mac w tym samym LAN)

---

## Co macie zrobić

### 1. Sklonować repo i przeczytać README

```bash
git clone git@github.com:KrystianKubicaHub/EminentFeedSystem.git
```

W `README.md` macie kompletny opis:
- Jak zintegrować bibliotekę z projektem ESP-IDF
- Jak zbudować aplikację na Maca
- API reference (connect, send, encrypt)
- Schemat architektury

**Nie powtarzam tutaj setupu — jest w README.**

---

### 2. Firmware na ESP32 (czujnik + silnik)

Wasz program na ESP32 powinien:

1. Połączyć się z WiFi (STA mode)
2. Stworzyć instancję `EminentSdk` z `PhysicalLayerEsp32Wifi`
3. Włączyć szyfrowanie (pre-shared key, 32 bajty — ustalcie wspólny klucz z apką na Macu)
4. Nawiązać połączenie z Macem (`sdk.connect(...)`)
5. **W pętli co 100ms** — odczytywać czujnik i wysyłać JSON:
   ```json
   {"sensor_x": 1.23, "sensor_y": -0.5, "motor_speed": 128}
   ```
6. **Odbierać komendy** z Maca (callback `onMessage`) i ustawiać PWM silnika:
   ```json
   {"speed": 200}
   ```

**Gotowy przykład** jest w `examples/esp32_gyro_motor/` — dostosujcie piny i czujnik do tego co macie.

Integracja z ESP-IDF:
```bash
cd wasz_projekt/components
ln -s /ścieżka/do/EminentFeedSystem/esp_idf_component eminent_sdk
idf.py build
```

---

### 3. Aplikacja na Maca (konsola)

Wasz program na Macu powinien:

1. Stworzyć `EminentSdk` z `PhysicalLayerUdp` (port 5000, IP ESP32, port ESP32)
2. Ten sam klucz szyfrowania co ESP32
3. Nawiązać połączenie
4. **Wyświetlać w konsoli** telemetrię z czujnika w czasie rzeczywistym
5. **Przyjmować input z klawiatury** (np. wpisanie `150` → wysyła `{"speed":150}` do ESP32)

**Gotowy przykład** jest w `examples/mac_console/`.

Build:
```bash
cd EminentFeedSystem/build
cmake -DBUILD_EXAMPLES=ON ..
make mac_console
./mac_console
```

---

### 4. Kryteria akceptacji (Definition of Done)

- [ ] ESP32 łączy się z Macem przez WiFi (handshake przechodzi)
- [ ] Telemetria z czujnika wyświetla się na Macu w konsoli (min. 5 Hz)
- [ ] Komenda prędkości wysłana z Maca faktycznie zmienia obroty silnika
- [ ] Komunikacja jest zaszyfrowana (ten sam klucz po obu stronach)
- [ ] Odłączenie ESP32 od zasilania → Mac dostaje callback `onDisconnected` lub `onHeartbeatMissed`
- [ ] Ponowne włączenie ESP32 → ponowne połączenie działa bez restartu Maca

---

## Wskazówki

- **Porty UDP:** ESP32 nasłuchuje na jednym porcie (np. 5001), Mac na innym (np. 5000). Muszą na siebie wskazywać.
- **IP:** Po połączeniu ESP32 do WiFi sprawdźcie jakie dostał IP (log w monitorze serialnym). To IP podajecie w aplikacji na Macu.
- **Klucz szyfrowania:** 32 bajty, identyczne po obu stronach. Hardcode na razie wystarczy.
- **Heartbeat:** Ustawcie interwał ~1s. Jeśli ESP32 zginie, Mac powinien to wykryć w ciągu 3-5s.
- **Logi:** `LogLevel::INFO` na początku, potem `WARN` jak już działa.

---

## Problemy? Kontakt

Jeśli coś nie działa:
1. Sprawdźcie czy ESP32 i Mac są w tym samym subnecie (ping)
2. Sprawdźcie czy porty UDP nie są blokowane przez firewall na Macu (System Settings → Firewall)
3. Logi SDK (`LogLevel::DEBUG`) pokażą co się dzieje wewnątrz

Pytania techniczne o API → `README.md` + `Sdk/include/EminentSdk.hpp` (tam jest pełna lista metod).

---

*Powodzenia!* 🚀
