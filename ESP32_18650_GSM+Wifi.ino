#define TINY_GSM_MODEM_SIM800
#define TINY_GSM_RX_BUFFER 1024

#include <WiFi.h>
#include <TinyGsmClient.h>
#include <ArduinoHttpClient.h>

// ================= РЕЖИМЫ РАБОТЫ (ПАНЕЛЬ УПРАВЛЕНИЯ) =================
// Меняйте true на false, чтобы отключать каналы связи
const bool ENABLE_WIFI = true; // true = Использовать Wi-Fi (Основной)
const bool ENABLE_GSM = true;  // true = Использовать GSM (Резервный)

// ================= НАСТРОЙКИ СЕТИ =================
const char *wifi_ssid = "Xiaomi_9D58"; // <--- ВАШ WI-FI
const char *wifi_pass = "Am5084ak";    // <--- ВАШ ПАРОЛЬ

const char apn[] = "internet";
const char user[] = "";
const char pass[] = "";

const char *DEVID = "vA48E437DF1936FA";

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

HttpClient httpWifi(clientWifi, "api.pushingbox.com", 80);
HttpClient httpGsm(clientGsm, "api.pushingbox.com", 80);

bool isPowerOn = true;
unsigned long lastKeepAlive = 0;

void setup()
{
    SerialMon.begin(115200);
    delay(1000);

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
    else
    {
        SerialMon.println("GSM is DISABLED in settings.");
    }

    // 2. Инициализация Wi-Fi (Только если разрешено)
    if (ENABLE_WIFI)
    {
        SerialMon.println("Init Wi-Fi...");
        WiFi.mode(WIFI_STA);
        WiFi.begin(wifi_ssid, wifi_pass);
        httpWifi.setHttpResponseTimeout(15000);
    }
    else
    {
        SerialMon.println("Wi-Fi is DISABLED in settings.");
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

            int bat = getBatteryPercentage(isPowerOn);

            if (isPowerOn)
            {
                SerialMon.println("EVENT: Power Restored");
                sendSmart(1, "PowerRestored", bat);
            }
            else
            {
                SerialMon.println("EVENT: Power Lost");
                sendSmart(0, "PowerLost", bat);
            }
        }
    }

    if (millis() - lastKeepAlive > 60000)
    {
        int bat = getBatteryPercentage(isPowerOn);
        SerialMon.println("Routine Check...");
        sendSmart(isPowerOn ? 1 : 0, "RoutineCheck", bat);
        lastKeepAlive = millis();
    }

    delay(100);
}

// === УМНАЯ ОТПРАВКА С УЧЕТОМ НАСТРОЕК ===
void sendSmart(int val, String deviceStatus, int batLevel)
{

    String path = "/pushingbox?devid=" + String(DEVID) +
                  "&val=" + String(val) +
                  "&device=" + deviceStatus +
                  "&bat=" + String(batLevel);

    bool sent = false;

    // --- ШАГ 1: Wi-Fi ---
    // Пробуем только если: 1. В настройках включено.
    if (ENABLE_WIFI)
    {

        // Если отвалился - пробуем переподключить
        if (WiFi.status() != WL_CONNECTED)
        {
            SerialMon.print("Wi-Fi lost. Reconnecting");
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
            SerialMon.print("[Wi-Fi] Sending... ");
            int err = httpWifi.get(path);
            int statusCode = httpWifi.responseStatusCode();

            if (err == 0 && statusCode == 200)
            {
                SerialMon.println("Success!");
                httpWifi.responseBody();
                sent = true;
            }
            else
            {
                SerialMon.print("Error! Code: ");
                SerialMon.print(err); // -1...-4 это ошибки сети
                SerialMon.print(" | HTTP Status: ");
                SerialMon.println(statusCode); // Код ответа сервера
            }
        }
        else
        {
            SerialMon.println("[Wi-Fi] No connection.");
        }
    }

    // --- ШАГ 2: GSM ---
    // Пробуем если: 1. Wi-Fi не справился (или выключен). 2. GSM включен в настройках.
    if (!sent && ENABLE_GSM)
    {
        SerialMon.println("[GSM] Switching to Backup Channel...");

        if (!modem.isGprsConnected())
        {
            SerialMon.print("[GSM] Connecting GPRS... ");
            if (!modem.gprsConnect(apn, user, pass))
            {
                SerialMon.println("FAIL");
                return;
            }
            SerialMon.println("OK");
        }

        SerialMon.print("[GSM] Sending... ");
        int err = httpGsm.get(path);
        if (err == 0)
        {
            SerialMon.println("Success!");
            httpGsm.responseBody();
        }
        else
        {
            SerialMon.println("Error via GSM: " + String(err));
        }
    }
    else if (!sent && !ENABLE_GSM)
    {
        SerialMon.println("Message NOT sent: Wi-Fi failed and GSM is disabled.");
    }
}

// === БАТАРЕЯ (Коэфф 2.40) ===
int getBatteryPercentage(bool charging)
{
    long sum = 0;
    for (int i = 0; i < 20; i++)
    {
        sum += analogRead(BAT_PIN);
        delay(5);
    }
    float average = sum / 20.0;

    float voltage = (average / 4095.0) * 3.3 * 2.40;

    int percentage = 0;
    if (charging)
    {
        percentage = map(voltage * 100, 400, 420, 80, 100);
    }
    else
    {
        percentage = map(voltage * 100, 320, 380, 0, 100);
    }

    if (percentage > 100)
        percentage = 100;
    if (percentage < 0)
        percentage = 0;

    SerialMon.print("Bat Voltage: ");
    SerialMon.print(voltage);
    SerialMon.print("V | Mode: ");
    SerialMon.print(charging ? "Charging" : "Discharging");
    SerialMon.print(" | Level: ");
    SerialMon.print(percentage);
    SerialMon.println("%");

    return percentage;
}