# EminentFeedSystem — Dokumentacja Architektury

## Spis treści

1. [Przegląd systemu](#1-przegląd-systemu)
2. [Architektura warstwowa](#2-architektura-warstwowa)
3. [Szczegóły warstw](#3-szczegóły-warstw)
4. [Protokoły](#4-protokoły)
5. [Format binarny ramki](#5-format-binarny-ramki)
6. [Typy danych](#6-typy-danych)

---

## 1. Przegląd systemu

**EminentFeedSystem** to niestandardowy stos komunikacyjny implementujący niezawodną,
warstwową transmisję danych peer-to-peer. System umożliwia nawiązywanie połączeń,
wymianę wiadomości (JSON, VIDEO) z gwarancją dostarczenia (ACK + retransmisja)
oraz fragmentację dużych payloadów.

### Diagram warstw

```
┌─────────────────────────────────────────────────────────┐
│                      APLIKACJA                          │
│               (app/main.cpp, użytkownik)                │
└────────────────────────┬────────────────────────────────┘
                         │ API publiczne
┌────────────────────────▼────────────────────────────────┐
│                    EminentSdk                            │
│     Zarządzanie połączeniami, handshake, API wysyłania  │
│                                                         │
│  Wejście:  metody publiczne (initialize, connect, send) │
│  Wyjście:  outgoingQueue_ (queue<Message>) ────────┐    │
│  Odbiór:   onMessageReceived(Message) ◄────────────│──  │
└────────────────────────┬───────────────────────────│────┘
                         │ referencja do kolejki     │
┌────────────────────────▼───────────────────────────│────┐
│                  SessionManager                    │    │
│     Fragmentacja, ACK, retransmisja, składanie    │    │
│                                                   │    │
│  Wejście:  sdkQueue_ (referencja na outgoingQueue_)    │
│  Wyjście:  outgoingPackages_ (queue<Package>) ───┐│    │
│  Odbiór:   receivePackage(Package) ◄─────────────││──  │
│  Do SDK:   sdk_.onMessageReceived(Message) ───────│──▶ │
└────────────────────────┬─────────────────────────││────┘
                         │ referencja do kolejki   ││
┌────────────────────────▼─────────────────────────││────┐
│                 TransportLayer                    ││    │
│     Serializacja/deserializacja pakietów         ││    │
│                                                  ││    │
│  Wejście:  outgoingPackages_ (ref na SM queue)   ││    │
│  Wyjście:  outgoingFrames_ (queue<Frame>) ──────┐││    │
│  Odbiór:   receiveFrame(Frame) ◄────────────────│││──  │
│  Do SM:    sessionManager_.receivePackage() ─────││──▶ │
└────────────────────────┬────────────────────────│││────┘
                         │ referencja do kolejki  │││
┌────────────────────────▼────────────────────────│││────┐
│                  CodingModule                   │││    │
│     Dodanie/weryfikacja CRC32                  │││    │
│                                                │││    │
│  Wejście:  inputFrames_ (ref na TL outFrames)  │││    │
│  Wyjście:  outgoingFrames_ (queue<Frame>) ────┐││││    │
│  Odbiór:   receiveFrameWithCrc(Frame) ◄───────│││││──  │
│  Do TL:    transportLayer_.receiveFrame() ─────│││──▶ │
└────────────────────────┬───────────────────────│││││────┘
                         │ referencja do kolejki │││││
┌────────────────────────▼───────────────────────│││││────┐
│                Physical Layer                  │││││    │
│     Transmisja UDP / InMemory                  │││││    │
│                                                │││││    │
│  Wejście:  outgoingFramesFromCodingModule_     │││││    │
│            (wskaźnik na CM outgoingFrames_)     │││││    │
│  Wyjście:  sieć UDP / medium InMemory          │││││    │
│  Odbiór:   sieć/medium → codingModule_->       │││││    │
│            receiveFrameWithCrc(frame) ──────────││││──▶ │
└─────────────────────────────────────────────────────────┘
```

---

## 2. Architektura warstwowa

### Kierunek wysyłania (↓ w dół stosu)

```
Aplikacja → EminentSdk.send()
         → Message trafia do outgoingQueue_
         → SessionManager pobiera z sdkQueue_, fragmentuje na Package[]
         → Package trafia do outgoingPackages_
         → TransportLayer pobiera, serializuje do Frame (binarne bajty)
         → Frame trafia do outgoingFrames_
         → CodingModule pobiera, dodaje CRC32
         → Frame+CRC trafia do outgoingFrames_
         → PhysicalLayer pobiera, wysyła przez UDP / medium
```

### Kierunek odbioru (↑ w górę stosu)

```
Sieć/Medium → PhysicalLayer.workerLoop() odbiera dane
            → codingModule_->receiveFrameWithCrc(frame)
            → CodingModule weryfikuje CRC32, odcina CRC
            → transportLayer_.receiveFrame(frame)
            → TransportLayer deserializuje Frame → Package
            → sessionManager_.receivePackage(pkg)
            → SessionManager składa fragmenty, generuje ACK
            → sdk_.onMessageReceived(message)
            → EminentSdk routuje do odpowiedniego handlera
            → connection.onMessage(msg) → callback aplikacji
```

---

## 3. Szczegóły warstw

### 3.1. EminentSdk

**Plik:** `Sdk/src/EminentSdk.cpp` | **Header:** `Sdk/include/EminentSdk.hpp`

**Odpowiedzialność:**
- Publiczne API systemu (jedyny punkt kontaktu dla aplikacji)
- Zarządzanie połączeniami (mapa `connections_`)
- Protokół handshake (3-way)
- Routing przychodzących wiadomości do callbacków

**Jak dane wchodzą (wysyłanie):**
- Aplikacja wywołuje `sdk.send(connId, payload, format, priority, requireAck, onDelivered)`
- Metoda tworzy obiekt `Message` i wrzuca go do `outgoingQueue_`

```cpp
// EminentSdk::send()
outgoingQueue_.push(msg);  // queue<Message> outgoingQueue_
```

**Jak dane wychodzą (do następnej warstwy):**
- `outgoingQueue_` jest przekazana przez **referencję** do `SessionManager` w konstruktorze:
  ```cpp
  SessionManager(outgoingQueue_, *this, validationConfig_, ...)
  ```
- SessionManager sam pobiera wiadomości z tej kolejki w swoim wątku roboczym

**Jak dane przychodzą (odbiór):**
- SessionManager wywołuje `sdk_.onMessageReceived(message)` po złożeniu wszystkich fragmentów
- `onMessageReceived()` sprawdza `msg.format` i routuje do:
  - `handleHandshakeRequest/Response/FinalConfirmation` → HANDSHAKE
  - `handleJsonMessage` → JSON
  - `handleVideoMessage` → VIDEO

**Kluczowe pola:**
| Pole | Typ | Opis |
|------|-----|------|
| `outgoingQueue_` | `queue<Message>` | Kolejka wyjściowa do SessionManagera |
| `connections_` | `unordered_map<int, Connection>` | Mapa aktywnych połączeń |
| `deviceId_` | `DeviceId` | Identyfikator tego urządzenia |
| `nextConnectionId_` | `ConnectionId` | Następny wolny ID (generowane jako liczby pierwsze) |

---

### 3.2. SessionManager

**Plik:** `Session_Manager/src/SessionManager.cpp` | **Header:** `Session_Manager/include/SessionManager.hpp`

**Odpowiedzialność:**
- Fragmentacja dużych wiadomości na pakiety (`Package`)
- Śledzenie potwierdzeń (ACK) — per pakiet
- Retransmisja niepotwierdzonych pakietów (co 500ms, max 5 prób)
- Składanie fragmentów przychodzących w kompletne wiadomości
- Generowanie pakietów ACK (format `CONFIRMATION`)

**Jak dane wchodzą (wysyłanie):**
- Wątek roboczy (`workerLoop()`) co 20ms sprawdza `sdkQueue_` (referencja na `EminentSdk::outgoingQueue_`)
- Pobiera `Message` z kolejki i przetwarza w `processSdkQueueLocked()`

```cpp
// workerLoop() → processSdkQueueLocked()
while (!sdkQueue_.empty()) {
    Message msg = sdkQueue_.front();
    sdkQueue_.pop();
    // fragmentacja...
}
```

**Fragmentacja:**
- Payload dzielony na fragmenty o rozmiarze `maxPacketSize_` (= `validationConfig_.maxPayloadLengthBytes()`)
- Każdy fragment staje się obiektem `Package` z polami `fragmentId` i `fragmentsCount`

**Jak dane wychodzą (do TransportLayer):**
- Każdy `Package` trafia do `outgoingPackages_` (`queue<Package>`)
- TransportLayer ma referencję na tę kolejkę (przekazaną w konstruktorze)

```cpp
// sendPackageLocked()
outgoingPackages_.push(info.pkg);
```

**Jak dane przychodzą (odbiór):**
- TransportLayer wywołuje `sessionManager_.receivePackage(pkg)` (wywołanie metody)
- Jeśli `pkg.format == CONFIRMATION` → `handleAckPackage()` — usuwa pakiet z pending
- W przeciwnym razie:
  1. Jeśli `requireAck` → generuje ACK via `sendAckForPackageLocked()`
  2. Buforuje fragment w `receivedPackages_[messageId]`
  3. Gdy wszystkie fragmenty zebrane → składa payload → `sdk_.onMessageReceived(message)`

**Mechanizm retransmisji:**
```
┌─ workerLoop (co 20ms) ─────────────────────────────┐
│  1. Pobierz wiadomości z sdkQueue_ → fragmentuj    │
│  2. Sprawdź pendingMessages_:                       │
│     - Jeśli minęło 500ms od ostatniego wysłania    │
│       i attempts < 5 → retransmituj                 │
│     - Jeśli attempts >= 5 → porzuć pakiet          │
└─────────────────────────────────────────────────────┘
```

**Kluczowe pola:**
| Pole | Typ | Opis |
|------|-----|------|
| `sdkQueue_` | `queue<Message>&` | Ref na kolejkę SDK |
| `outgoingPackages_` | `queue<Package>` | Kolejka wyjściowa do TransportLayer |
| `pendingMessages_` | `unordered_map<MessageId, PendingMessageInfo>` | Pakiety czekające na ACK |
| `receivedPackages_` | `unordered_map<MessageId, vector<Package>>` | Bufor fragmentów przychodzących |
| `retransmitInterval_` | `500ms` | Czas między retransmisjami |
| `maxRetransmitAttempts_` | `5` | Maksymalna liczba prób |

---

### 3.3. TransportLayer

**Plik:** `Transport_Layer/src/TransportLayer.cpp` | **Header:** `Transport_Layer/include/TransportLayer.hpp`

**Odpowiedzialność:**
- Serializacja `Package` → `Frame` (binary big-endian)
- Deserializacja `Frame` → `Package`
- Walidacja rozmiaru pól zgodnie z `ValidationConfig`

**Jak dane wchodzą (wysyłanie):**
- Wątek roboczy (`workerLoop()`) co 10ms sprawdza `outgoingPackages_` (referencja na kolejkę SessionManagera)
- Pobiera `Package` i wywołuje `serialize(pkg)`

```cpp
// workerLoop()
while (!outgoingPackages_.empty()) {
    Package pkg = outgoingPackages_.front();
    outgoingPackages_.pop();
    Frame frame = serialize(pkg);
    outgoingFrames_.push(frame);
}
```

**Jak dane wychodzą (do CodingModule):**
- Zserializowany `Frame` trafia do `outgoingFrames_` (`queue<Frame>`)
- CodingModule dostaje referencję na tę kolejkę w swoim konstruktorze

**Jak dane przychodzą (odbiór):**
- CodingModule wywołuje `transportLayer_.receiveFrame(frame)` (wywołanie metody)
- Frame jest deserializowany → `Package`
- Pakiet przekazywany do `sessionManager_.receivePackage(pkg)`

**Format serializacji (big-endian):**
```
[packageId][messageId][connId][fragmentId][fragmentsCount][format][priority][requireAck][payloadLength][payload...]
```

Rozmiar każdego pola zależy od `ValidationConfig` (np. 16-bit connectionId = 2 bajty).

**Kluczowe pola:**
| Pole | Typ | Opis |
|------|-----|------|
| `outgoingPackages_` | `queue<Package>&` | Ref na kolejkę SM |
| `outgoingFrames_` | `queue<Frame>` | Kolejka wyjściowa do CodingModule |
| `packageIdBytes_` | `uint8_t` | Liczba bajtów na pole packageId |
| `payloadLengthBytes_` | `uint8_t` | Zawsze 2 (max payload = 65535B) |

---

### 3.4. CodingModule

**Plik:** `Coding_Module/src/CodingModule.cpp` | **Header:** `Coding_Module/include/CodingModule.hpp`

**Odpowiedzialność:**
- Dodawanie sumy kontrolnej CRC32 do ramek wychodzących
- Weryfikacja CRC32 ramek przychodzących (odrzucenie uszkodzonych)
- Walidacja rozmiarów ramek

**Jak dane wchodzą (wysyłanie):**
- Wątek roboczy co 10ms sprawdza `inputFrames_` (referencja na `TransportLayer::outgoingFrames_`)
- Pobiera `Frame`, oblicza CRC32, dołącza 4 bajty CRC na końcu

```cpp
// Worker w konstruktorze:
while (!inputFrames_.empty()) {
    Frame frame = inputFrames_.front();
    inputFrames_.pop();
    uint32_t crc = crc32(frame.data);
    Frame frameWithCrc = frame;
    // dołącz 4 bajty CRC (big-endian)
    for (int i = 0; i < 4; ++i)
        frameWithCrc.data.push_back((crc >> (8*(3-i))) & 0xFF);
    outgoingFrames_.push(frameWithCrc);
}
```

**Jak dane wychodzą (do PhysicalLayer):**
- `Frame` z CRC trafia do `outgoingFrames_` (`queue<Frame>`)
- PhysicalLayer podczas `configure()` otrzymuje wskaźnik na tę kolejkę

**Jak dane przychodzą (odbiór):**
- PhysicalLayer wywołuje `codingModule_->receiveFrameWithCrc(frame)` (wywołanie metody)
- CodingModule:
  1. Odcina ostatnie 4 bajty (received CRC)
  2. Oblicza CRC32 z reszty danych
  3. Porównuje — jeśli mismatch → `throw runtime_error` (ramka odrzucona)
  4. Jeśli OK → `transportLayer_.receiveFrame(decodedFrame)`

**Algorytm CRC32:**
- Standard CRC-32 (polynomial `0xEDB88320`, init `0xFFFFFFFF`, final XOR)

**Kluczowe pola:**
| Pole | Typ | Opis |
|------|-----|------|
| `inputFrames_` | `queue<Frame>&` | Ref na kolejkę TL |
| `outgoingFrames_` | `queue<Frame>` | Kolejka wyjściowa do PhysicalLayer |
| `CRC_BYTES` | `4` | Stały rozmiar sumy kontrolnej |

---

### 3.5. PhysicalLayer (Abstract + UDP + InMemory)

**Pliki:** `Physical_Layer/src/` | **Headers:** `Physical_Layer/include/`

**Odpowiedzialność:**
- Fizyczna transmisja ramek binarnych
- Implementacja UDP (prawdziwa sieć) lub InMemory (testy)

#### 3.5.1. PhysicalLayerUdp

**Jak dane wchodzą (wysyłanie):**
- Wątek roboczy co 10ms sprawdza `outgoingFramesFromCodingModule_` (wskaźnik na `CodingModule::outgoingFrames_`)
- Pobiera `Frame` i wysyła przez UDP (`sendto()`)

```cpp
// workerLoop()
while (outgoingFramesFromCodingModule_ && !outgoingFramesFromCodingModule_->empty()) {
    Frame frame = outgoingFramesFromCodingModule_->front();
    outgoingFramesFromCodingModule_->pop();
    sendto(sock_, frame.data.data(), frame.data.size(), 0, ...);
}
```

**Jak dane wychodzą (do CodingModule — odbiór):**
- Ten sam wątek roboczy wykonuje `recvfrom()` na non-blocking socket
- Odebrane dane → `codingModule_->receiveFrameWithCrc(frame)`

```cpp
ssize_t received = recvfrom(sock_, recvBuffer_.data(), recvBuffer_.size(), 0, ...);
while (received > 0) {
    Frame frame;
    frame.data.assign(recvBuffer_.begin(), recvBuffer_.begin() + received);
    codingModule_->receiveFrameWithCrc(frame);
    received = recvfrom(...);
}
```

#### 3.5.2. PhysicalLayerInMemory

- Używa współdzielonego `InMemoryMedium` (wektor ramek + mutex)
- Symuluje broadcast — każde urządzenie widzi ramki wszystkich innych
- Używana w testach (nie wymaga sieci)

**Schemat medium:**
```
┌────────────────── InMemoryMedium ──────────────────┐
│  entries: vector<{senderId, frame, deliveredTo}>   │
│  participants: unordered_set<DeviceId>             │
│  mutex: std::mutex                                 │
└────────────────────────────────────────────────────┘
       ↑ push               ↓ read (filtered)
   Device A              Device B, C, D...
```

---

### 3.6. ValidationModule (ValidationConfig)

**Plik:** `Validation_Module/src/ValidationConfig.cpp` | **Header:** `Validation_Module/include/ValidationConfig.hpp`

**Odpowiedzialność:**
- Definiowanie szerokości bitowych pól protokołu
- Walidacja wartości identyfikatorów i pól pakietów
- Obliczanie rozmiarów nagłówka transportowego

**Nie jest warstwą przepływu danych** — jest modułem konfiguracyjnym współdzielonym przez wszystkie warstwy.

**Domyślna konfiguracja:**
| Pole | Bity | Max wartość | Bajty w ramce |
|------|------|-------------|---------------|
| deviceId | 16 | 65535 | 2 |
| connectionId | 16 | 65535 | 2 |
| messageId | 24 | 16777215 | 3 |
| packageId | 24 | 16777215 | 3 |
| fragmentId | 8 | 255 | 1 |
| fragmentsCount | 8 | 255 | 1 |
| priority | 4 | 15 | 1 |
| specialCode | 16 | 65535 | 2 |
| format | (stałe) | — | 1 |
| requireAck | (stałe) | — | 1 |
| payloadLength | (stałe) | 65535 | 2 |
| CRC32 | (stałe) | — | 4 |

**Nagłówek transportowy:** 17 bajtów (z domyślną konfiguracją)
**Max payload:** 65535 bajtów
**Max ramka z CRC:** 65556 bajtów

---

## 4. Protokoły

### 4.1. Nawiązanie połączenia (3-Way Handshake)

Połączenie ustanawiane jest protokołem trójfazowym inspirowanym TCP.
Używa `MessageFormat::HANDSHAKE` i specjalnego mechanizmu generowania ConnectionId (iloczyn liczb pierwszych).

```
    Urządzenie A (initiator)              Urządzenie B (acceptor)
         │                                        │
         │  ① HANDSHAKE REQUEST                   │
         │  connId = prime_A (np. 2)              │
         │  payload: {"deviceId": A,              │
         │            "specialCode": random}      │
         │───────────────────────────────────────▶│
         │                                        │
         │         onIncomingConnectionDecision()  │
         │         → true (akceptacja)            │
         │                                        │
         │  ② HANDSHAKE RESPONSE                  │
         │  connId = prime_A (2)                  │
         │  payload: {"deviceId": B,              │
         │            "specialCode": same,        │
         │            "newId": prime_B (np. 2)}   │
         │◀───────────────────────────────────────│
         │                                        │
         │  B tworzy Connection z id =            │
         │  prime_A * prime_B = 4                  │
         │  status: ACCEPTED                      │
         │                                        │
         │  A odbiera, oblicza combined =          │
         │  prime_A * prime_B = 4                  │
         │  status: ACTIVE                        │
         │  wywołuje onConnected(4)               │
         │                                        │
         │  ③ FINAL CONFIRMATION                  │
         │  connId = combined (4)                 │
         │  payload: {"deviceId": A,              │
         │            "specialCode": same,        │
         │            "finalConfirmation": true}  │
         │───────────────────────────────────────▶│
         │                                        │
         │  B status ACCEPTED → ACTIVE            │
         │  wywołuje onConnectionEstablished(4,A) │
         │                                        │
    ═══════════ POŁĄCZENIE AKTYWNE (id=4) ════════════
```

**Mechanizm ConnectionId:**
- Każda strona generuje liczbę pierwszą (`nextPrime()`)
- Finalne ConnectionId = iloczyn obu liczb pierwszych
- Gwarantuje unikalność (każda para ma unikalne id)

**SpecialCode:**
- Losowo generowany kod (16-bit domyślnie)
- Służy do identyfikacji sesji handshake
- Obie strony muszą potwierdzić ten sam kod

---

### 4.2. Wysyłanie wiadomości

```
    Aplikacja                    EminentSdk               SessionManager
       │                             │                          │
       │  sdk.send(connId,           │                          │
       │    payload, JSON, 5,        │                          │
       │    true, onDelivered)       │                          │
       │────────────────────────────▶│                          │
       │                             │                          │
       │                             │  Message → outgoingQueue_│
       │                             │─────────────────────────▶│
       │                             │                          │
       │                             │     workerLoop() pobiera │
       │                             │     fragmentacja:        │
       │                             │     payload / maxPacketSize│
       │                             │                          │
       │                             │     Package[0..N-1]      │
       │                             │     → outgoingPackages_  │
       │                             │                          │
```

```
    SessionManager           TransportLayer          CodingModule         PhysicalLayer
       │                          │                       │                     │
       │  outgoingPackages_       │                       │                     │
       │─ ─ ─ ─(queue ref)─ ─ ─ ▶│                       │                     │
       │                          │                       │                     │
       │                          │  serialize(pkg)       │                     │
       │                          │  → Frame (binary)     │                     │
       │                          │  → outgoingFrames_    │                     │
       │                          │─ ─ ─(queue ref)─ ─ ─▶│                     │
       │                          │                       │                     │
       │                          │                       │  CRC32 + append    │
       │                          │                       │  → outgoingFrames_  │
       │                          │                       │─ ─(ptr na queue)─ ─▶│
       │                          │                       │                     │
       │                          │                       │                     │ sendto(UDP)
       │                          │                       │                     │────▶ SIEĆ
```

**Fragmentacja (Session Manager):**
- `maxPacketSize_` = max payload z ValidationConfig (domyślnie 65535B)
- Wiadomość dzielona na `ceil(payload.size() / maxPacketSize_)` pakietów
- Każdy pakiet: `fragmentId` = 0..N-1, `fragmentsCount` = N

**Potwierdzenie dostarczenia (ACK):**
- Gdy `requireAck = true`:
  - Pakiety trafiają do `pendingMessages_` z timestampem
  - Odbiorca generuje `CONFIRMATION` z payload `{"ackPackageId": X}`
  - Po otrzymaniu ACK dla wszystkich fragmentów → `onDelivered()` callback

**Retransmisja:**
- Co 20ms sprawdzane są pakiety w `pendingMessages_`
- Jeśli od ostatniego wysłania minęło > 500ms → retransmisja
- Po 5 nieudanych próbach → pakiet porzucony

---

### 4.3. Odbiór wiadomości

```
  SIEĆ ────▶ PhysicalLayer       CodingModule        TransportLayer      SessionManager
                  │                    │                    │                    │
                  │  recvfrom()        │                    │                    │
                  │  frame.data = raw  │                    │                    │
                  │                    │                    │                    │
                  │  codingModule_->   │                    │                    │
                  │  receiveFrameWithCrc│                   │                    │
                  │───────────────────▶│                    │                    │
                  │                    │                    │                    │
                  │                    │  verify CRC32     │                    │
                  │                    │  strip 4 bytes    │                    │
                  │                    │                    │                    │
                  │                    │  transportLayer_->│                    │
                  │                    │  receiveFrame()   │                    │
                  │                    │───────────────────▶│                    │
                  │                    │                    │                    │
                  │                    │                    │  deserialize()    │
                  │                    │                    │  Frame → Package  │
                  │                    │                    │                    │
                  │                    │                    │  sessionManager_->│
                  │                    │                    │  receivePackage() │
                  │                    │                    │───────────────────▶│
                  │                    │                    │                    │
                  │                    │                    │                    │ if CONFIRMATION
                  │                    │                    │                    │ → handleAckPackage()
                  │                    │                    │                    │
                  │                    │                    │                    │ else:
                  │                    │                    │                    │ → buforuj fragment
                  │                    │                    │                    │ → if all received:
                  │                    │                    │                    │   złóż Message
                  │                    │                    │                    │   sdk_.onMessageReceived()
```

**Składanie wiadomości:**
1. Fragmenty buforowane w `receivedPackages_[messageId]`
2. Gdy `vec.size() == fragmentsCount`:
   - Sortowanie po `fragmentId`
   - Konkatenacja payloadów
   - Przekazanie do SDK

---

### 4.4. Zamknięcie połączenia (Disconnect Protocol)

Protokół rozłączenia jest **dwustronny** — obie strony otrzymują powiadomienie.

```
    Urządzenie A (rozłącza)                Urządzenie B
         │                                      │
         │  sdk.disconnect(connId)              │
         │                                      │
         │  ① DISCONNECT message               │
         │  format: MessageFormat::DISCONNECT   │
         │  connId: shared connection id        │
         │  payload: {"deviceId": A,            │
         │            "connId": id}             │
         │────────────────────────────────────▶│
         │                                      │
         │  A: onDisconnected() callback ✓      │
         │  A: usuwa connection z mapy          │
         │  A: usuwa heartbeat state            │
         │                                      │
         │                                      │ B: handleDisconnectMessage()
         │                                      │ B: onDisconnected() callback ✓
         │                                      │ B: usuwa connection z mapy
         │                                      │ B: usuwa heartbeat state
         │                                      │
    ═══════════ POŁĄCZENIE ZAMKNIĘTE ═══════════
```

**API:**
```cpp
// Rozłącz się (obie strony dostaną callback)
sdk.disconnect(connId);

// Zmiana callbacka po fakcie:
sdk.setOnDisconnected(connId, []() { cout << "Disconnected!" << endl; });
```

---

### 4.5. Heartbeat (Utrzymanie połączenia)

Heartbeat służy do wykrywania "martwych" połączeń. Konfigurowalny interwał
(domyślnie 2000ms). Użytkownik dostaje callback przy braku odpowiedzi i
sam decyduje kiedy rozłączyć.

```
    Urządzenie A                             Urządzenie B
         │                                        │
         │  (co heartbeatInterval ms)             │
         │                                        │
         │  ① HEARTBEAT                           │
         │  format: MessageFormat::HEARTBEAT      │
         │  payload: {"deviceId": A, "ts": ...}   │
         │───────────────────────────────────────▶│
         │                                        │
         │                                        │ handleHeartbeat()
         │                                        │
         │  ② HEARTBEAT_ACK                       │
         │  format: MessageFormat::HEARTBEAT_ACK  │
         │  payload: {"deviceId": B, "ts": ...}   │
         │◀───────────────────────────────────────│
         │                                        │
         │  handleHeartbeatAck()                  │
         │  → lastReceived = now                  │
         │  → waitingForResponse = false          │
         │                                        │
```

**Gdy odpowiedź nie nadchodzi:**
```
    Urządzenie A
         │
         │  (heartbeatInterval elapsed, no ACK)
         │
         │  onHeartbeatMissed(connId) → callback do usera
         │
         │  User decyduje:
         │  - Ignorować (może sieć jest wolna)
         │  - Po N missach: sdk.disconnect(connId)
```

**API:**
```cpp
sdk.connect(
    targetId, priority,
    onSuccess, onFailure, onTrouble, onDisconnected, onConnected, onMessage,
    chrono::milliseconds{2000},  // heartbeat co 2 sekundy
    [](ConnectionId cid) {
        cout << "Heartbeat missed on " << cid << endl;
        // user liczy ile razy i decyduje o disconnect
    }
);
```

**Uwaga:** Heartbeat NIE używa `requireAck=true` na poziomie transportu — to
nie duplikuje mechanizmu ACK niższych warstw. Heartbeat działa na poziomie SDK
jako ping-pong: A wysyła `HEARTBEAT`, B odpowiada `HEARTBEAT_ACK`.

---

## 5. Format binarny ramki

### Struktura ramki na warstwie transportowej (przed CRC):

```
┌──────────────────────────────────────────────────────────────────────┐
│ packageId │ messageId │ connId │ fragId │ fragCount │ format │ prio │
│  (3B)     │  (3B)     │ (2B)   │ (1B)   │  (1B)     │ (1B)   │ (1B)│
├──────────────────────────────────────────────────────────────────────┤
│ requireAck │ payloadLength │         payload (variable)              │
│   (1B)     │    (2B)       │         (0..65535 B)                    │
└──────────────────────────────────────────────────────────────────────┘
```

### Po przejściu przez CodingModule (+CRC32):

```
┌──────────────────────────────────────────────────────────────┬──────┐
│              Transport Frame (jak wyżej)                     │ CRC  │
│              (17 + payload bajtów)                            │ (4B) │
└──────────────────────────────────────────────────────────────┴──────┘
```

**Kolejność bajtów:** Big-Endian (MSB first)

---

## 6. Typy danych

### Message (SDK ↔ SessionManager)

```cpp
struct Message {
    MessageId id;           // Unikalny identyfikator wiadomości
    ConnectionId connId;    // ID połączenia
    string payload;         // Treść (JSON string, binary, handshake)
    MessageFormat format;   // JSON, VIDEO, HANDSHAKE, CONFIRMATION
    Priority priority;      // Priorytet (0..15 domyślnie)
    bool requireAck;        // Czy wymagane potwierdzenie
    function<void()> onDelivered;  // Callback po dostarczeniu
};
```

### Package (SessionManager ↔ TransportLayer)

```cpp
struct Package {
    PackageId packageId;      // Unikalny ID pakietu (do ACK)
    MessageId messageId;      // ID wiadomości-rodzica
    ConnectionId connId;      // ID połączenia
    int fragmentId;           // Numer fragmentu (0-based)
    int fragmentsCount;       // Łączna liczba fragmentów
    string payload;           // Fragment payloadu
    MessageFormat format;     // Format odziedziczony z Message
    Priority priority;        // Priorytet
    bool requireAck;          // Czy wymagane ACK
    PackageStatus status;     // QUEUED, SENT, ACKED, FAILED
};
```

### Frame (TransportLayer ↔ CodingModule ↔ PhysicalLayer)

```cpp
struct Frame {
    vector<uint8_t> data;   // Surowe bajty (header + payload [+ CRC])
};
```

### Connection (wewnątrz EminentSdk)

```cpp
struct Connection {
    ConnectionId id;                       // Finalne ID (iloczyn prime)
    DeviceId remoteId;                     // ID zdalnego urządzenia
    Priority defaultPriority;              // Domyślny priorytet
    function<void(const Message&)> onMessage;     // Callback odbioru
    function<void(const string&)> onTrouble;      // Callback problemów
    function<void()> onDisconnected;              // Callback rozłączenia
    function<void(ConnectionId)> onConnected;     // Callback połączenia
    ConnectionStatus status;               // PENDING/ACCEPTED/ACTIVE/FAILED
    int specialCode;                       // Kod sesji handshake
};
```

---

## Podsumowanie przepływu międzywarstwowego

| Z warstwy | Do warstwy | Mechanizm przekazania | Typ danych |
|-----------|------------|----------------------|------------|
| EminentSdk | SessionManager | `outgoingQueue_` (wspólna `queue<Message>` przez referencję) | `Message` |
| SessionManager | TransportLayer | `outgoingPackages_` (własna kolejka, TL ma referencję) | `Package` |
| TransportLayer | CodingModule | `outgoingFrames_` (własna kolejka, CM ma referencję) | `Frame` |
| CodingModule | PhysicalLayer | `outgoingFrames_` (własna kolejka, PL ma wskaźnik) | `Frame+CRC` |
| PhysicalLayer | CodingModule | wywołanie metody `codingModule_->receiveFrameWithCrc()` | `Frame+CRC` |
| CodingModule | TransportLayer | wywołanie metody `transportLayer_.receiveFrame()` | `Frame` |
| TransportLayer | SessionManager | wywołanie metody `sessionManager_.receivePackage()` | `Package` |
| SessionManager | EminentSdk | wywołanie metody `sdk_.onMessageReceived()` | `Message` |

**Wzorzec:** Wysyłanie odbywa się przez **publiczne kolejki** (polling w wątku roboczym).
Odbiór odbywa się przez **bezpośrednie wywołania metod** (push w górę stosu).
