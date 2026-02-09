#define TINY_GSM_MODEM_SIM800
#define TINY_GSM_RX_BUFFER 1024

#include <WiFi.h>
#include <HTTPClient.h> // Библиотека для прямого HTTPS (Wi-Fi)
#include <TinyGsmClient.h>
#include <ArduinoHttpClient.h> // Библиотека для PushingBox (GSM)

// ================= НАСТРОЙКИ =================
const bool ENABLE_WIFI = true;
const bool ENABLE_GSM = true;

const char *wifi_ssid = "Xiaomi_9D58";
const char *wifi_pass = "Am5084ak";

// --- НАСТРОЙКИ ДЛЯ БЕЗЛИМИТНОГО WI-FI (Google) ---
// Вставьте сюда ID вашего скрипта (Длинная строка из URL между /s/ и /exec)
String GAS_SCRIPT_ID = "AKfycbyvn_NXzYXWankzZan3efMbUaQZNpjjNCBcmJNrGPHHkUoAfASHqMpmiy_Sm_Yuzt4d";

// --- НАСТРОЙКИ ДЛЯ РЕЗЕРВНОГО GSM (PushingBox) ---
const char apn[] = "internet";
const char user[] = "";
const char pass[] = "";
const char *DEVID = "vA48E437DF1936FA"; // Ваш ID из PushingBox

// ================= ПИНЫ =================
#define MODEM_RST 23
#define MODEM_TX 17
#define MODEM_RX 16
#define MAINS_PIN 4
#define BAT_PIN 34

#define SerialMon Serial
#define SerialAT Serial2

// ================= КЛИЕНТЫ =================
WiFiClient clientWifi;
TinyGsm modem(SerialAT);
TinyGsmClient clientGsm(modem);
HttpClient httpGsm(clientGsm, "api.pushingbox.com", 80);

bool isPowerOn = true;
unsigned long lastKeepAlive = 0;

// Предварительное объявление функций, чтобы не было ошибок компиляции
int getBatteryPercentage(bool charging, float &outVoltage);
void sendSmart(int val, String deviceStatus, int batLevel, float voltage);

void setup()
{
    SerialMon.begin(115200);
    // ПРАВКА 1: Даем питанию стабилизироваться при включении
    delay(2000);

    pinMode(MAINS_PIN, INPUT);
    pinMode(BAT_PIN, INPUT);

    // 1. Инициализация GSM (Только если разрешено)
    if (ENABLE_GSM)
    {
        SerialAT.begin(57600, SERIAL_8N1, MODEM_RX, MODEM_TX);
        delay(3000);
        SerialMon.println("System Start. Init Modem...");
        modem.restart();
        httpGsm.setHttpResponseTimeout(90000);
    }

    // ПРАВКА 2: Пауза между GSM и Wi-Fi, чтобы не было скачка тока
    delay(2000);
    // 2. Инициализация Wi-Fi (Только если разрешено)
    if (ENABLE_WIFI)
    {
        SerialMon.println("Init Wi-Fi...");
        WiFi.mode(WIFI_STA);
        WiFi.begin(wifi_ssid, wifi_pass);
        // Тут тайм-аут не ставим, используем встроенный в HTTPClient
    }
    // Проверка статуса при старте
    if (digitalRead(MAINS_PIN) == HIGH)
    {
        isPowerOn = true;
        SerialMon.println("STARTUP: Power ON");
    }
    else
    {
        isPowerOn = false;
        SerialMon.println("STARTUP: Power OFF");
    }

    // ПРАВКА 3: Отправляем приветственное сообщение, чтобы убедиться, что система не зависла
    SerialMon.println("Setup Done. Sending Boot Info...");

    // ИЗМЕНЕНИЕ: Добавили переменную для вольтажа
    float currentVolt = 0.0;
    int bat = getBatteryPercentage(isPowerOn, currentVolt);

    // ИЗМЕНЕНИЕ: Передаем вольтаж в sendSmart
    sendSmart(1, "SystemStart", bat, currentVolt);
}

void loop()
{
    int sensorVal = digitalRead(MAINS_PIN);
    bool currentReading = (sensorVal == HIGH);

    if (currentReading != isPowerOn)
    {
        delay(500);
        if (digitalRead(MAINS_PIN) == sensorVal)
        {
            isPowerOn = currentReading;

            // ИЗМЕНЕНИЕ: Получаем вольтаж
            float currentVolt = 0.0;
            int bat = getBatteryPercentage(isPowerOn, currentVolt);

            if (isPowerOn)
            {
                SerialMon.println("EVENT: Power Restored");
                sendSmart(1, "PowerRestored", bat, currentVolt);
            }
            else
            {
                SerialMon.println("EVENT: Power Lost");
                sendSmart(0, "PowerLost", bat, currentVolt);
            }
        }
    }

    // ТЕПЕРЬ МОЖНО ЧАЩЕ! Например, раз в 5 минут (300000 мс)
    // Wi-Fi "бесплатный", а GSM будет тратить лимит PushingBox только при аварии
    if (millis() - lastKeepAlive > 600000)
    {
        // ИЗМЕНЕНИЕ: Получаем вольтаж
        float currentVolt = 0.0;
        int bat = getBatteryPercentage(isPowerOn, currentVolt);

        SerialMon.println("Routine Check...");
        sendSmart(isPowerOn ? 1 : 0, "RoutineCheck", bat, currentVolt);
        lastKeepAlive = millis();
    }
    delay(100);
}

// ИЗМЕНЕНИЕ: Добавлен аргумент float voltage
void sendSmart(int val, String deviceStatus, int batLevel, float voltage)
{
    bool sent = false;

    // --- 1. КАНАЛ WI-FI (ПРЯМОЙ В GOOGLE - БЕЗЛИМИТ) ---
    if (ENABLE_WIFI)
    {
        // Реконнект если нужно
        if (WiFi.status() != WL_CONNECTED)
        {
            SerialMon.print("Wi-Fi reconnecting...");
            WiFi.reconnect();
            for (int i = 0; i < 30; i++)
            {
                if (WiFi.status() == WL_CONNECTED)
                    break;
                SerialMon.print(".");
                delay(100);
            }
            SerialMon.println();
        }

        if (WiFi.status() == WL_CONNECTED)
        {
            HTTPClient http;

            // Формируем прямой URL для Google Script
            String url = "https://script.google.com/macros/s/" + GAS_SCRIPT_ID + "/exec";
            url += "?val=" + String(val);
            url += "&device=" + deviceStatus;
            url += "&bat=" + String(batLevel);
            url += "&volt=" + String(voltage, 2); // ИЗМЕНЕНИЕ: Добавлен вольтаж в URL

            // ВАЖНО: Разрешаем переадресацию (Google всегда делает Redirect 302)
            http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

            // Отключаем проверку сертификата (чтобы не менять его каждые 3 месяца)
            http.begin(url);

            SerialMon.print("[Wi-Fi Direct] Sending... ");
            int httpCode = http.GET();

            if (httpCode == 200)
            {
                SerialMon.println("Success! (Unlimited)");
                sent = true;
            }
            else
            {
                SerialMon.print("Error: ");
                SerialMon.println(httpCode);
            }
            http.end();
        }
    }

    // --- 2. КАНАЛ GSM (РЕЗЕРВ ЧЕРЕЗ PUSHINGBOX) ---
    // Используется только если Wi-Fi не смог отправить
    if (!sent && ENABLE_GSM)
    {
        SerialMon.println("[GSM] Switching to Backup (PushingBox)...");

        if (!modem.isGprsConnected())
        {
            SerialMon.print("[GSM] Connecting GPRS... ");
            if (!modem.gprsConnect(apn, user, pass))
            {
                SerialMon.println("FAIL. Resetting modem...");
                modem.restart();
                return;
            }
            SerialMon.println("OK");
        }

        // Старый добрый URL для PushingBox
        String path = "/pushingbox?devid=" + String(DEVID) +
                      "&val=" + String(val) +
                      "&device=" + deviceStatus +
                      "&bat=" + String(batLevel) +
                      "&volt=" + String(voltage, 2); // ИЗМЕНЕНИЕ: Добавлен вольтаж в URL

        SerialMon.print("[GSM] Sending... ");
        httpGsm.stop();
        int err = httpGsm.get(path);

        if (err == 0)
        {
            SerialMon.println("Success! (Backup used)");
            httpGsm.responseBody();
        }
        else
        {
            SerialMon.println("Error via GSM: " + String(err));
        }
    }
}

// === БАТАРЕЯ (ПЕРЕСЧИТАНО ПОД ХОРОШУЮ ПАЙКУ) ===
// ИЗМЕНЕНИЕ: Функция теперь принимает ссылку на переменную outVoltage, чтобы вернуть точное значение
int getBatteryPercentage(bool charging, float &outVoltage)
{
    long sum = 0;
    for (int i = 0; i < 20; i++)
    {
        sum += analogRead(BAT_PIN);
        delay(5);
    }
    float average = sum / 20.0;

    // ИЗМЕНЕНИЕ: Пишем результат сразу в переданную переменную outVoltage
    if (charging)
    {
        // Свет есть: коэффициент 2.14 (Оставляем, он точный)
        outVoltage = (average / 4095.0) * 3.3 * 2.14;
    }
    else
    {
        // Света нет: Снижаем с 2.33 до 2.25
        // Теперь напряжение не будет "подпрыгивать" при отключении света
        outVoltage = (average / 4095.0) * 3.3 * 2.25;
    }

    int percentage = 0;
    if (charging)
        percentage = map(outVoltage * 100, 400, 420, 80, 100);
    else
        percentage = map(outVoltage * 100, 320, 370, 0, 100);

    if (percentage > 100)
        percentage = 100;
    if (percentage < 0)
        percentage = 0;

    SerialMon.print("Bat: ");
    SerialMon.print(outVoltage);
    SerialMon.print("V | ");
    SerialMon.print(percentage);
    SerialMon.println("%");
    return percentage;
}