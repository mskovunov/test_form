#include <WiFi.h>
#include <PZEM004Tv30.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>

// 1. ПОДКЛЮЧАЕМ СТОРОЖЕВОЙ ТАЙМЕР
#include <esp_task_wdt.h>

// ---------------- 1. НАСТРОЙКИ WIFI ----------------
#define WIFI_SSID "Xiaomi_9D58"
#define WIFI_PASSWORD "Am5084ak"

// ---------------- 2. НАСТРОЙКИ FIREBASE ----------------
#define API_KEY "AIzaSyAsr-JbArcAu-7_csBxM6VDsC7hyPz7qB0"
#define FIREBASE_PROJECT_ID "meter-form"

// Данные пользователя Auth
#define USER_EMAIL "esp32@test.com"
#define USER_PASSWORD "123456"

// ---------------- 3. НАСТРОЙКИ ВРЕМЕНИ (NTP) ----------------
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7200;     // UTC+2 (Киев/Зимнее время)
const int daylightOffset_sec = 3600; // Летнее время (3600 - включено)

// ---------------- 4. НАСТРОЙКИ PZEM (26/27) ----------------
#define PZEM_RX_PIN 26
#define PZEM_TX_PIN 27
#define PZEM_SERIAL Serial2

// 2. ТАЙМАУТ ЗАВИСАНИЯ (30 секунд)
#define WDT_TIMEOUT 30

PZEM004Tv30 pzem(PZEM_SERIAL, PZEM_RX_PIN, PZEM_TX_PIN);
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

unsigned long sendDataPrevMillis = 0;
unsigned long lastConnectionTime = 0; // Время последней успешной связи

// --- ФУНКЦИЯ ПОЛУЧЕНИЯ ВРЕМЕНИ ---
String getCurrentTime()
{
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo))
    {
        Serial.println("Не удалось получить время");
        return "Error";
    }
    char timeStringBuff[50];
    strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(timeStringBuff);
}

// --- ФУНКЦИЯ ОТПРАВКИ ЛОГОВ ---
// --- ФУНКЦИЯ ОТПРАВКИ ЛОГОВ ---
void sendLog(String message)
{
    if (Firebase.ready())
    {
        FirebaseJson content;
        content.set("fields/message/stringValue", message);
        String timeStr = getCurrentTime();
        content.set("fields/created_at/stringValue", timeStr);

        String reason = "Unknown";
        esp_reset_reason_t r = esp_reset_reason();
        switch (r)
        {
        case ESP_RST_POWERON:
            reason = "Power ON (Живлення)";
            break;
        case ESP_RST_EXT:
            reason = "External Reset (Кнопка)";
            break; // <--- ДОБАВИЛ
        case ESP_RST_SW:
            reason = "Software Reset (Код)";
            break;
        case ESP_RST_PANIC:
            reason = "Crash/Panic (Помилка)";
            break;
        case ESP_RST_BROWNOUT:
            reason = "Brownout (Напруга)";
            break;
        case ESP_RST_INT_WDT:
            reason = "Int. Watchdog";
            break;
        case ESP_RST_TASK_WDT:
            reason = "Task Watchdog (Зависання)";
            break;
        case ESP_RST_WDT:
            reason = "Other Watchdog";
            break;
        default:
            reason = String(r);
            break;
        }
        content.set("fields/reason/stringValue", reason);

        String path = "system_logs";
        Serial.print("Попытка записи в system_logs... ");
        if (Firebase.Firestore.createDocument(&fbdo, FIREBASE_PROJECT_ID, "", path.c_str(), content.raw()))
        {
            Serial.printf("УСПЕХ! Лог записан. ID: %s\n", fbdo.payload().c_str());
        }
        else
        {
            Serial.printf("ОШИБКА записи лога: %s\n", fbdo.errorReason().c_str());
        }
    }
    else
    {
        Serial.println("ОШИБКА: Firebase не готов");
    }
}

void setup()
{
    Serial.begin(115200);

    Serial.println("Configuring WDT...");
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = WDT_TIMEOUT * 1000,
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
        .trigger_panic = true};
    esp_task_wdt_init(&wdt_config);
    esp_task_wdt_add(NULL);

    // 1. Подключение WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Подключение к Wi-Fi");

    // <--- ПРАВКА ЗДЕСЬ: Добавили сброс таймера в цикл ожидания
    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.print(".");
        delay(300);
        esp_task_wdt_reset(); // <--- КОРМИМ СОБАКУ, ПОКА ЖДЕМ WI-FI
    }
    Serial.println("\nWiFi подключен!");

    // 2. Настройка времени
    Serial.print("Настройка времени...");
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

    struct tm timeinfo;
    // <--- ПРАВКА ЗДЕСЬ: Добавили сброс таймера в цикл ожидания
    while (!getLocalTime(&timeinfo))
    {
        Serial.print(".");
        delay(100);
        esp_task_wdt_reset(); // <--- КОРМИМ СОБАКУ, ПОКА ЖДЕМ ВРЕМЯ
    }
    Serial.println("\nВремя синхронизировано: " + getCurrentTime());

    config.api_key = API_KEY;
    auth.user.email = USER_EMAIL;
    auth.user.password = USER_PASSWORD;
    config.token_status_callback = tokenStatusCallback;

    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);

    lastConnectionTime = millis();

    Serial.println("Инициализация Firebase...");
    unsigned long startWait = millis();
    while (!Firebase.ready() && millis() - startWait < 10000)
    {
        delay(500);
        Serial.print(".");
        esp_task_wdt_reset(); // Тут уже было, все ок
    }
    Serial.println();

    Serial.println("Отправка лога о запуске...");
    sendLog("Запуск системи");
}

void loop()
{
    // 5. КОРМИМ СОБАКУ (Если этот код не сработает 30 сек — будет ребут)
    esp_task_wdt_reset();

    // === 1. ПРОВЕРКА СВЯЗИ И АВТОРЕБУТ ===
    if (WiFi.status() == WL_CONNECTED && Firebase.ready())
    {
        lastConnectionTime = millis();
    }
    else
    {
        if (millis() - lastConnectionTime > 180000)
        { // 3 минуты
            Serial.println("Связь потеряна более 3 минут. Выполняю перезагрузку...");
            ESP.restart();
        }
    }

    // === 2. ОТПРАВКА ДАННЫХ (Раз в 15 сек) ===
    if (Firebase.ready() && (millis() - sendDataPrevMillis > 15000 || sendDataPrevMillis == 0))
    {
        sendDataPrevMillis = millis();

        float voltage = pzem.voltage();
        float current = pzem.current();
        float power = pzem.power();
        float energy = pzem.energy();

        // === ФИЛЬТР АДЕКВАТНОСТИ ===
        bool isError = false;
        if (isnan(voltage) || voltage < 0 || voltage > 280)
            isError = true;
        if (isnan(power) || power < 0 || power > 25000)
            isError = true;
        if (isnan(energy) || energy < 0)
            isError = true;

        if (isError)
        {
            Serial.println("Обнаружены аномальные данные или обрыв. Отправляем 0.");
            voltage = 0.0;
            current = 0.0;
            power = 0.0;
        }

        double cleanVoltage = round(voltage * 10) / 10.0;
        double cleanCurrent = round(current * 1000) / 1000.0;
        double cleanPower = round(power * 10) / 10.0;
        double cleanEnergy = round(energy * 1000) / 1000.0;

        String timeStr = getCurrentTime();

        Serial.printf("[%s] V: %.1f, P: %.1f\n", timeStr.c_str(), cleanVoltage, cleanPower);

        FirebaseJson content;
        content.set("fields/voltage/doubleValue", cleanVoltage);
        content.set("fields/current/doubleValue", cleanCurrent);
        content.set("fields/power/doubleValue", cleanPower);
        content.set("fields/energy/doubleValue", cleanEnergy);

        content.set("fields/created_at/stringValue", timeStr);
        content.set("fields/timestamp_unix/integerValue", time(nullptr));

        String documentPath = "meter_readings";
        if (Firebase.Firestore.createDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), content.raw()))
        {
            Serial.printf("Отправлено! ID: %s\n", fbdo.payload().c_str());
        }
        else
        {
            Serial.printf("Ошибка: %s\n", fbdo.errorReason().c_str());
        }
    }
}