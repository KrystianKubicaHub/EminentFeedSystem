// Test stabilności numer 2 uruchamiamy w wariancie synchronicznym na warstwie fizycznej w pamięci.
// Ten plik zawiera kompletną symulację wielourządzeniowej sieci SDK, dlatego całą logikę dokumentujemy
// bardzo dokładnie w komentarzach – krok po kroku, linijka po linijce.

#include "EminentSdk.hpp"          // Główna klasa SDK – zarządza sesjami, transportem i kodowaniem ramek.
#include "PhysicalLayerInMemory.hpp" // Warstwa fizyczna oparta na wspólnej pamięci (bez UDP), używana w teście.
#include "ValidationConfig.hpp"    // Zestaw reguł weryfikujących poprawność identyfikatorów i parametrów.

// Standardowe nagłówki C++ wykorzystywane przez test (omówione przy pierwszym użyciu w kodzie).
#include <atomic>          // std::atomic – bezpieczne współdzielenie flag pomiędzy wątkami.
#include <chrono>          // std::chrono – pomiar czasu i limity oczekiwania.
#include <cctype>          // std::isdigit / std::isspace – parsowanie indeksów z payloadów JSON.
#include <iostream>        // std::cout / std::cerr – raportowanie postępu testu.
#include <memory>          // std::unique_ptr / std::make_unique – zarządzanie obiektami SDK i warstwy fizycznej.
#include <mutex>           // std::mutex / std::lock_guard – ochrona map połączeń.
#include <optional>        // std::optional – bezpieczne zwracanie "może istnieć" / "nie istnieje".
#include <random>          // std::random_device / std::mt19937 – losowanie nadawców, odbiorców i treści.
#include <string>          // std::string – przechowywanie payloadów oraz logów.
#include <thread>          // std::this_thread::sleep_for – aktywne oczekiwanie na zdarzenia.
#include <unordered_map>   // std::unordered_map – skojarzenie remoteId -> connectionId.
#include <vector>          // std::vector – przechowywanie kontekstów urządzeń oraz rekordów wiadomości.

using namespace std;

namespace {
// ---- Stałe konfiguracyjne testu ----
constexpr int kDeviceCount = 4;                  // Liczba instancji SDK, które symulujemy jednocześnie.
constexpr int kMessageCount = 20;               // Łączna liczba wiadomości wysyłanych w losowych parach.
constexpr int kMaxPayloadLength = 2100;           // Maksymalna długość losowanego tekstu w polu "text" JSON-a.
constexpr Priority kDefaultPriority = 5;         // Wspólny priorytet domyślny wiadomości i połączeń.
constexpr chrono::milliseconds kPollingInterval{200}; // Odstęp pomiędzy kolejnymi próbami sprawdzania warunków.
constexpr chrono::seconds kInitTimeout{50};       // Limit czasu na inicjalizację wszystkich urządzeń.
constexpr chrono::seconds kConnectTimeout{300};   // Limit czasu na zestawienie pojedynczego połączenia on-demand.
constexpr chrono::seconds kDeliveryTimeout{200};  // Limit czasu na dostarczenie wszystkich wiadomości.

struct MessageRecord {
    DeviceId senderId = -1;   // Identyfikator urządzenia nadającego.
    DeviceId receiverId = -1; // Identyfikator urządzenia odbierającego.
    string payload;           // Treść wiadomości JSON (zawiera index + tekst).
    atomic<bool> delivered{false}; // Callback dostarczenia (warstwa transportowa) ustawia na true.
    atomic<bool> received{false};  // Handler odbiorcy ustawia na true, gdy wiadomość dotrze do SDK.
};

struct DeviceContext {
    DeviceId id;                                 // Własny identyfikator urządzenia (3000 + indeks).
    unique_ptr<EminentSdk> sdk;                  // Wskaźnik na instancję SDK obsługującą urządzenie.
    unordered_map<DeviceId, ConnectionId> connections; // Odwzorowanie: remoteId -> connectionId (aktywny kanał).
    mutable mutex connectionsMutex;              // Blokada chroniąca mapę połączeń przed wyścigami.
    atomic<bool> initialized{false};             // Flaga informująca, że initialize() zakończyło się sukcesem.
};

string generateRandomString(mt19937& engine,
                            uniform_int_distribution<int>& lengthDist,
                            const string& alphabet,
                            uniform_int_distribution<int>& charDist) {
    const int len = lengthDist(engine);              // Losujemy docelową długość tekstu.
    string result;                                   // Bufor na wynik.
    result.reserve(static_cast<size_t>(len));        // Minimalizujemy realokacje rezerwując miejsce.
    for (int i = 0; i < len; ++i) {                  // Generujemy każdy znak osobno...
        result.push_back(alphabet[static_cast<size_t>(charDist(engine))]); // ...korzystając z rozkładu jednolitego.
    }
    return result;                                   // Zwracamy wygenerowany ciąg.
}

string buildJsonPayload(DeviceId from, int index, const string& text) {
    string payload = "{\"from\":\"";          // Składamy JSON-a ręcznie, aby uniknąć dodatkowych zależności.
    payload += to_string(from);                     // Pole "from" – identyfikator nadawcy.
    payload += "\",\"text\":\"";             // Zakładka dla pola tekstowego.
    payload += text;                                // Właściwa losowa treść.
    payload += "\",\"index\":";              // Pole "index" – numer wiadomości.
    payload += to_string(index);                    // Wstawiamy indeks.
    payload += "}";                                // Domykamy obiekt JSON.
    return payload;                                 // Zwracamy gotowy string.
}

optional<int> extractIndex(const string& payload) {
    const string token = "\"index\":";                // Szukamy substringa identyfikującego pole "index".
    size_t pos = payload.find(token);                      // Pozycja startowa pola.
    if (pos == string::npos) {                             // Jeśli brak pola – zwracamy brak wartości.
        return nullopt;
    }
    pos += token.size();                                   // Przeskakujemy za nazwę pola.
    while (pos < payload.size() && isspace(static_cast<unsigned char>(payload[pos]))) {
        ++pos;                                             // Pomijamy spacje / tabulatory.
    }
    size_t end = pos;                                      // Koniec fragmentu liczbowego.
    while (end < payload.size() && isdigit(static_cast<unsigned char>(payload[end]))) {
        ++end;                                             // Zbieramy kolejne cyfry.
    }
    if (end == pos) {                                     // Jeżeli brak cyfr – niepoprawny format.
        return nullopt;
    }
    try {
        return stoi(payload.substr(pos, end - pos));       // Konwertujemy substring na liczbę całkowitą.
    } catch (...) {
        return nullopt;                                    // W razie błędu konwersji – brak wartości.
    }
}

void noteConnection(DeviceContext& ctx, DeviceId remoteId, ConnectionId connectionId) {
    lock_guard<mutex> lock(ctx.connectionsMutex);          // Chronimy mapę połączeń.
    ctx.connections[remoteId] = connectionId;              // Zapamiętujemy identyfikator aktywnego kanału.
}

optional<ConnectionId> getConnection(const DeviceContext& ctx, DeviceId remoteId) {
    lock_guard<mutex> lock(ctx.connectionsMutex);          // Dostęp do mapy wymaga blokady (mutable pozwala na lock_guard).
    auto it = ctx.connections.find(remoteId);              // Szukamy połączenia do danego remoteId.
    if (it == ctx.connections.end()) {
        return nullopt;                                    // Jeśli nie istnieje – zwracamy brak.
    }
    return it->second;                                     // Jeżeli istnieje – zwracamy connectionId.
}

} // namespace

int main() {
    cout << "[StabilityTest] Start - in-memory multicast scenario" << endl; // Nagłówek testu w logach.

    ValidationConfig validationConfig;             // Wspólna konfiguracja walidacji dla wszystkich instancji SDK.
    auto medium = make_shared<InMemoryMedium>();   // Współdzielone medium pamięciowe – symuluje warstwę fizyczną.

    vector<unique_ptr<DeviceContext>> devices;     // Kontener przechowujący konteksty wszystkich urządzeń.
    devices.reserve(kDeviceCount);                 // Rezerwujemy pamięć, aby uniknąć dodatkowych alokacji.

    for (int i = 0; i < kDeviceCount; ++i) {
        auto context = make_unique<DeviceContext>();                            // Tworzymy nowy kontekst urządzenia.
        context->id = 3000 + i;                                                // Nadajemy unikalny identyfikator (z zakresu 3000+).
        auto physicalLayer = make_unique<PhysicalLayerInMemory>(context->id, medium); // Każde urządzenie dostaje własną warstwę fizyczną.
        context->sdk = make_unique<EminentSdk>(std::move(physicalLayer), validationConfig, LogLevel::ERROR);
        // Powyżej: przenosimy warstwę fizyczną do konstruktora SDK + ustawiamy niski poziom logowania (ERROR).
        devices.push_back(std::move(context));                                   // Zachowujemy kontekst w wektorze.
    }

    vector<MessageRecord> records(static_cast<size_t>(kMessageCount)); // Tablica rekordów – po jednym na każdą wiadomość.

    auto registerHandler = [&](DeviceContext& ctx) {
        return [&](ConnectionId connectionId) {                                  // Zwracamy lambdę konfigurującą handler.
            ctx.sdk->setOnMessageHandler(connectionId, [&](const Message& msg) { // Przypisujemy callback odbioru wiadomości.
                auto maybeIndex = extractIndex(msg.payload);                     // Próbujemy wydobyć "index" z JSON-a.
                if (!maybeIndex) {                                               // Jeśli nie ma indeksu...
                    cerr << "[WARN] Unable to extract index from payload: " << msg.payload << endl;
                    return;                                                     // ...kończymy bez oznaczenia odbioru.
                }
                int idx = *maybeIndex;                                          // Parsowanie powiodło się – mamy numer wiadomości.
                if (idx < 0 || idx >= kMessageCount) {                          // Sprawdzamy, czy indeks mieści się w 0..399.
                    cerr << "[WARN] Received index out of range: " << idx << endl;
                    return;                                                     // Błędny indeks -> ignorujemy.
                }
                records[static_cast<size_t>(idx)].received.store(true, memory_order_relaxed); // Oznaczamy wiadomość jako odebraną.
            });
        };
    };

    auto ensureConnection = [&](DeviceContext& senderCtx, DeviceContext& receiverCtx) -> optional<ConnectionId> {
        if (auto existing = getConnection(senderCtx, receiverCtx.id)) {         // Najpierw sprawdzamy, czy połączenie już istnieje.
            return existing;                                                    // Jeśli tak – natychmiast zwracamy jego ID.
        }

        atomic<bool> connected{false};                                          // Flaga ustawiana, gdy callback onConnected się wykona.
        atomic<bool> failed{false};                                             // Flaga błędu ustawiana przez onFailure.
        string lastError;                                                       // Wiadomość błędu do diagnostyki.

        auto attachHandler = registerHandler(senderCtx);                        // Przygotowujemy handler wiadomości aktywowany po zestawieniu połączenia.

        senderCtx.sdk->connect(
            receiverCtx.id,
            kDefaultPriority,
            nullptr,                                                            // Nie potrzebujemy osobnego onSuccess (ACK przychodzi asynchronicznie).
            [&](const string& err) {                                             // onFailure – gdy handshake się nie powiedzie.
                lastError = err;
                failed.store(true, memory_order_relaxed);
                cerr << "  [ERROR] connect failure from " << senderCtx.id << " to "
                     << receiverCtx.id << ": " << err << endl;
            },
            [&](const string& trouble) {                                         // onTrouble – ostrzegamy o problemach podczas połączenia.
                cerr << "  [WARN] connect trouble on device " << senderCtx.id << ": " << trouble << endl;
            },
            [&]() {                                                             // onDisconnected – logujemy zerwanie, aby było widoczne w testach.
                cerr << "  [WARN] connection dropped on device " << senderCtx.id
                     << " to " << receiverCtx.id << endl;
            },
            [&](ConnectionId cid) {                                              // onConnected – wywoływane, gdy połączenie przechodzi w stan ACTIVE.
                noteConnection(senderCtx, receiverCtx.id, cid);                  // Zapamiętujemy kanał w mapie.
                attachHandler(cid);                                              // Dołączamy handler odbioru wiadomości.
                connected.store(true, memory_order_relaxed);                     // Sygnalizujemy powodzenie.
                cout << "  [Connect] Device " << senderCtx.id << " active with "
                     << receiverCtx.id << " (cid=" << cid << ")" << endl;
            },
            {}                                                                  // onMessage – niepotrzebny tutaj, handler ustawiamy wyżej.
        );

        const auto start = chrono::steady_clock::now();                         // Zapamiętujemy chwilę startu oczekiwania.
        while (chrono::steady_clock::now() - start < kConnectTimeout) {         // Pętla aktywnego oczekiwania z limitem czasu.
            if (connected.load(memory_order_relaxed)) {                         // Jeśli połączenie się zestawiło...
                if (auto existing = getConnection(senderCtx, receiverCtx.id)) { // ...pobieramy jego ID i kończymy.
                    return existing;
                }
            }
            if (failed.load(memory_order_relaxed)) {                            // Jeśli wystąpił błąd – przerywamy.
                break;
            }
            this_thread::sleep_for(kPollingInterval);                           // Krótki sen, aby nie zająć 100% CPU.
        }

        if (!lastError.empty()) {                                               // Po przekroczeniu limitu czasu raportujemy przyczynę.
            cerr << "  [ERROR] ensureConnection failed: " << lastError << endl;
        } else {
            cerr << "  [ERROR] ensureConnection timeout between " << senderCtx.id
                 << " and " << receiverCtx.id << endl;
        }
        return nullopt;                                                         // Brak połączenia – sygnalizujemy niepowodzenie.
    };

    auto acceptAllConnections = [](DeviceId remoteId, const string& payload) {
        (void)payload;                                                          // Payload handshaku nas nie interesuje – akceptujemy wszystko.
        cout << "  [Init] Incoming handshake from device " << remoteId << " -> ACCEPT" << endl;
        return true;                                                            // Zwracamy true – handshake zostaje zaakceptowany.
    };

    for (auto& devicePtr : devices) {
        DeviceContext* ctx = devicePtr.get();                                   // Pobieramy surowy wskaźnik na kontekst urządzenia.
        auto attachHandler = registerHandler(*ctx);                             // Przygotowujemy funkcję do ustawiania handlerów.
        ctx->sdk->initialize(
            ctx->id,                                                            // Identyfikator własny.
            [ctx]() {                                                           // onSuccess – oznaczamy urządzenie jako gotowe.
                ctx->initialized.store(true, memory_order_relaxed);
                cout << "  [Init] Device " << ctx->id << " initialized" << endl;
            },
            [ctx](const string& err) {                                          // onFailure – wypisujemy błąd inicjalizacji.
                cerr << "  [ERROR] Initialization failed for device " << ctx->id << ": " << err << endl;
            },
            acceptAllConnections,                                               // Decyzja o przychodzących handshake'ach – zawsze true.
            [ctx, attachHandler](ConnectionId connectionId, DeviceId remoteId) { // onConnectionEstablished – handshake przychodzący.
                noteConnection(*ctx, remoteId, connectionId);                   // Dodajemy nowe połączenie do mapy.
                attachHandler(connectionId);                                    // Podpinamy handler odbiorczy.
                cout << "  [Init] Device " << ctx->id << " established connection with " << remoteId
                     << " (cid=" << connectionId << ")" << endl;
            }
        );
    }

    const auto initStart = chrono::steady_clock::now();                         // Start odliczania czasu inicjalizacji.
    while (true) {
        bool allInitialized = true;                                             // Zakładamy sukces, dopóki nie wykryjemy inaczej.
        for (const auto& devicePtr : devices) {
            if (!devicePtr->initialized.load(memory_order_relaxed)) {
                allInitialized = false;                                         // Jakiekolwiek urządzenie wciąż niegotowe -> kontynuujemy.
                break;
            }
        }
        if (allInitialized) {                                                   // Wszystkie urządzenia gotowe – wychodzimy z pętli.
            break;
        }
        if (chrono::steady_clock::now() - initStart > kInitTimeout) {           // Zbyt długie oczekiwanie -> błąd testu.
            cerr << "[ERROR] Initialization timeout" << endl;
            return 1;
        }
        this_thread::sleep_for(kPollingInterval);                              // Czekamy chwilę przed kolejnym sprawdzeniem.
    }

    cout << "[StabilityTest] All devices initialized" << endl; // Meldunek – można zaczynać test właściwy.

    random_device rd;                                                           // Źródło entropii (seed dla generatora).
    mt19937 engine(rd());                                                       // Generator liczb pseudolosowych (Mersenne Twister).
    uniform_int_distribution<int> deviceDist(0, kDeviceCount - 1);              // Równomierny rozkład nad indeksem urządzeń.
    uniform_int_distribution<int> lengthDist(1, kMaxPayloadLength);             // Rozkład dla długości tekstu.
    const string alphabet = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"; // Dostępny alfabet.
    uniform_int_distribution<int> charDist(0, static_cast<int>(alphabet.size()) - 1); // Rozkład dla pojedynczych znaków.

    size_t sendFailures = 0;                                                    // Licznik nieudanych prób wysłania.

    for (int i = 0; i < kMessageCount; ++i) {
        int senderIndex = deviceDist(engine);                                   // Losujemy nadawcę.
        int receiverIndex;                                                     // Oddzielna zmienna na odbiorcę.
        do {
            receiverIndex = deviceDist(engine);                                // Losujemy, aż trafi się ktoś inny niż nadawca.
        } while (receiverIndex == senderIndex);

        DeviceContext& senderCtx = *devices[static_cast<size_t>(senderIndex)];  // Referencje upraszczają dostęp do kontekstu.
        DeviceContext& receiverCtx = *devices[static_cast<size_t>(receiverIndex)];

        auto maybeConnection = getConnection(senderCtx, receiverCtx.id);        // Sprawdzamy, czy mamy gotowy kanał.
        if (!maybeConnection) {                                                 // Jeśli nie...
            maybeConnection = ensureConnection(senderCtx, receiverCtx);         // ...próbujemy zestawić połączenie na żądanie.
        }
        if (!maybeConnection) {                                                 // Połączenie się nie udało – raportujemy i pomijamy wysyłkę.
            cerr << "[ERROR] Missing connection from " << senderCtx.id << " to " << receiverCtx.id << endl;
            ++sendFailures;
            continue;
        }

        const string randomText = generateRandomString(engine, lengthDist, alphabet, charDist); // Tworzymy losowy tekst.
        string payload = buildJsonPayload(senderCtx.id, i, randomText);          // Szykujemy JSON z numerem i treścią.

        MessageRecord& record = records[static_cast<size_t>(i)];                 // Bierzemy rekord odpowiadający bieżącej wiadomości.
        record.senderId = senderCtx.id;                                         // Zapamiętujemy nadawcę...
        record.receiverId = receiverCtx.id;                                     // ...i odbiorcę.
        record.payload = payload;                                               // Archiwizujemy wysłaną treść.
        record.delivered.store(false, memory_order_relaxed);                    // Zerujemy flagę dostarczenia (nowa wiadomość).
        record.received.store(false, memory_order_relaxed);                     // Zerujemy flagę odbioru.

        try {
            senderCtx.sdk->send(
                maybeConnection.value(),
                payload,
                MessageFormat::JSON,
                kDefaultPriority,
                false,
                [index = i, &records]() {                                       // onDelivered – callback ustawia flagę delivered.
                    records[static_cast<size_t>(index)].delivered.store(true, memory_order_relaxed);
                }
            );
        } catch (const exception& ex) {
            ++sendFailures;                                                     // Jeśli send rzuci wyjątkiem – inkrementujemy licznik...
            cerr << "[ERROR] send failed from " << senderCtx.id << " to " << receiverCtx.id
                 << ": " << ex.what() << endl;                                 // ...i wypisujemy powód.
        }
    }

    if (sendFailures > 0) {
        cerr << "[WARN] Send failures encountered: " << sendFailures << endl;   // Po pętli informujemy o liczbie nieudanych wysyłek.
    }

    const auto deliveryStart = chrono::steady_clock::now();                     // Chwila startu odliczania dostaw.
    while (true) {
        bool allDelivered = true;                                               // Zakładamy sukces.
        for (const auto& record : records) {
            if (!record.delivered.load(memory_order_relaxed) ||
                !record.received.load(memory_order_relaxed)) {                   // Wystarczy, że jedna wiadomość ma false -> czekamy dalej.
                allDelivered = false;
                break;
            }
        }
        if (allDelivered) {                                                     // Wszystkie flagi ustawione -> kończymy oczekiwanie.
            break;
        }
        if (chrono::steady_clock::now() - deliveryStart > kDeliveryTimeout) {   // Upłynął limit czasu -> raportujemy błąd.
            cerr << "[ERROR] Delivery timeout" << endl;
            break;
        }
        this_thread::sleep_for(kPollingInterval);                              // Krótka pauza przed następnym sprawdzeniem.
    }

    size_t deliveredCount = 0;                                                  // Licznik wiadomości z potwierdzonym dostarczeniem.
    size_t receivedCount = 0;                                                   // Licznik wiadomości odebranych przez handler.
    vector<int> missingDelivery;                                                // Indeksy wiadomości bez potwierdzenia dostarczenia.
    vector<int> missingReception;                                               // Indeksy wiadomości bez potwierdzenia odbioru.

    for (int i = 0; i < kMessageCount; ++i) {
        const auto& record = records[static_cast<size_t>(i)];                   // Odczytujemy bieżący rekord.
        if (record.delivered.load(memory_order_relaxed)) {                      // Sprawdzamy flagę delivered...
            ++deliveredCount;                                                   // ...i zliczamy sukcesy.
        } else {
            missingDelivery.push_back(i);                                       // W przeciwnym razie zapamiętujemy indeks.
        }
        if (record.received.load(memory_order_relaxed)) {                       // Analogicznie – flaga received.
            ++receivedCount;
        } else {
            missingReception.push_back(i);
        }
    }

    cout << "[StabilityTest] Messages delivered: " << deliveredCount << " / " << kMessageCount << endl;
    cout << "[StabilityTest] Messages received : " << receivedCount << " / " << kMessageCount << endl;

    if (!missingDelivery.empty() || !missingReception.empty()) {
        cerr << "[ERROR] Test failed" << endl;                                 // Jeśli którekolwiek listy są niepuste – test nie zdał.
        if (!missingDelivery.empty()) {
            cerr << "  Missing deliveries (indices): ";                        // Wypisujemy indeksy bez potwierdzenia dostarczenia.
            for (int idx : missingDelivery) {
                cerr << idx << ' ';
            }
            cerr << endl;
        }
        if (!missingReception.empty()) {
            cerr << "  Missing receptions (indices): ";                       // Analogicznie dla nieodebranych wiadomości.
            for (int idx : missingReception) {
                cerr << idx << ' ';
            }
            cerr << endl;
        }
        return 3;                                                               // Kończymy z kodem błędu.
    }

    cout << "[StabilityTest] Completed successfully" << endl;                  // Jeśli doszliśmy tutaj – test zakończył się sukcesem.
    return 0;                                                                   // Zwracamy kod powodzenia.
}
