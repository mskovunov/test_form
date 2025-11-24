#include <WiFi.h>
#include <PZEM004Tv30.h>
#include <Firebase_ESP_Client.h>

// Вспомогательные файлы для генерации токенов
#include <addons/TokenHelper.h>

// ---------------- 1. НАСТРОЙКИ WIFI ----------------
#define WIFI_SSID "Xiaomi_9D58"
#define WIFI_PASSWORD "Am5084ak"

// ---------------- 2. НАСТРОЙКИ FIREBASE ----------------
#define API_KEY "AIzaSyAsr-JbArcAu-7_csBxM6VDsC7hyPz7qB0"
#define FIREBASE_PROJECT_ID "meter-form" // Например: my-home-project-123

// Данные пользователя, которого вы создали в Auth
#define USER_EMAIL "esp32@test.com"
#define USER_PASSWORD "123456"

// ---------------- 3. НАСТРОЙКИ ВРЕМЕНИ (NTP) ----------------
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7200;  // Смещение в секундах (3600 * 3 часа = UTC+3).
                                  // Если у вас UTC+2, ставьте 7200.
const int daylightOffset_sec = 0; // Летнее время (0 - выключено)

// ---------------- 4. НАСТРОЙКИ PZEM (26/27) ----------------
#define PZEM_RX_PIN 26
#define PZEM_TX_PIN 27
#define PZEM_SERIAL Serial2

PZEM004Tv30 pzem(PZEM_SERIAL, PZEM_RX_PIN, PZEM_TX_PIN);
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

unsigned long sendDataPrevMillis = 0;
unsigned long lastConnectionTime = 0; // Время последней успешной связи

// Функция получения текущего времени в формате строки
String getCurrentTime()
{
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo))
    {
        Serial.println("Не удалось получить время");
        return "Error";
    }
    char timeStringBuff[50];
    // Формат: Год-Месяц-День Час:Минута:Секунда
    strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(timeStringBuff);
}

void setup()
{
    Serial.begin(115200);

    // 1. Подключение WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Подключение к Wi-Fi");
    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.print(".");
        delay(300);
    }
    Serial.println("\nWiFi подключен!");

    // 2. Настройка времени (NTP)
    Serial.print("Настройка времени...");
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

    // Ждем, пока время синхронизируется
    struct tm timeinfo;
    while (!getLocalTime(&timeinfo))
    {
        Serial.print(".");
        delay(100);
    }
    Serial.println("\nВремя синхронизировано: " + getCurrentTime());

    // 3. Настройка Firebase
    config.api_key = API_KEY;
    auth.user.email = USER_EMAIL;
    auth.user.password = USER_PASSWORD;
    config.token_status_callback = tokenStatusCallback;

    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);

    lastConnectionTime = millis(); // Инициализируем таймер при старте
}

void loop()
{
    if (Firebase.ready() && (millis() - sendDataPrevMillis > 15000 || sendDataPrevMillis == 0))
    {
        sendDataPrevMillis = millis();

        // Считываем данные
        float voltage = pzem.voltage();
        float current = pzem.current();
        float power = pzem.power();
        float energy = pzem.energy();

        // === ФИЛЬТР АДЕКВАТНОСТИ ===
        // 1. Проверка на NaN (ошибка чтения)
        // 2. Проверка на бред (напряжение > 280В или < 0)
        // 3. Проверка на бред (мощность > 25000Вт - это 25 кВт, в квартире так не бывает)

        bool isError = false;

        if (isnan(voltage) || voltage < 0 || voltage > 280)
            isError = true;
        if (isnan(power) || power < 0 || power > 25000)
            isError = true;
        if (isnan(energy) || energy < 0)
            isError = true;

        if (isError)
        {
            Serial.println("Обнаружены аномальные данные (глюк при старте). Отправляем 0.");
            voltage = 0.0;
            current = 0.0;
            power = 0.0;
            // energy не трогаем или ставим 0, по желанию
        }

        // Округляем
        double cleanVoltage = round(voltage * 10) / 10.0;
        double cleanCurrent = round(current * 1000) / 1000.0;
        double cleanPower = round(power * 10) / 10.0;
        double cleanEnergy = round(energy * 1000) / 1000.0;

        // === ПРОВЕРКА СВЯЗИ И АВТОРЕБУТ ===

        // Если Wi-Fi подключен ИЛИ Firebase готов к работе
        if (WiFi.status() == WL_CONNECTED && Firebase.ready())
        {
            // Сбрасываем таймер (все хорошо)
            lastConnectionTime = millis();
        }
        else
        {
            // Если связи нет, проверяем, как долго
            if (millis() - lastConnectionTime > 180000)
            { // 180000 мс = 3 минуты
                Serial.println("Связь потеряна более 3 минут. Выполняю перезагрузку...");
                ESP.restart(); // <--- ЭТА КОМАНДА ПЕРЕЗАГРУЗИТ ПЛАТУ
            }
        }

        // Получаем текущее время
        String timeStr = getCurrentTime();

        Serial.printf("[%s] V: %.1f, P: %.1f\n", timeStr.c_str(), cleanVoltage, cleanPower);

        // Формируем JSON
        FirebaseJson content;
        content.set("fields/voltage/doubleValue", cleanVoltage);
        content.set("fields/current/doubleValue", cleanCurrent);
        content.set("fields/power/doubleValue", cleanPower);
        content.set("fields/energy/doubleValue", cleanEnergy);

        // ДОБАВЛЯЕМ ВРЕМЯ КАК СТРОКУ
        content.set("fields/created_at/stringValue", timeStr);
        // Также полезно добавить timestamp как число (для сортировки в будущем)
        content.set("fields/timestamp_unix/integerValue", time(nullptr));

        // Отправка
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