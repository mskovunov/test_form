#define TINY_GSM_MODEM_SIM800
#define TINY_GSM_RX_BUFFER 1024

#include <WiFi.h>
#include <TinyGsmClient.h>
#include <ArduinoHttpClient.h>

// ================= НАСТРОЙКИ =================
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

    // GSM Старт
    SerialAT.begin(57600, SERIAL_8N1, MODEM_RX, MODEM_TX);
    delay(3000);
    SerialMon.println("System Start. Init Modem...");
    modem.restart();

    // Wi-Fi Старт
    SerialMon.println("Init Wi-Fi...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifi_ssid, wifi_pass);

    httpWifi.setHttpResponseTimeout(15000);
    httpGsm.setHttpResponseTimeout(90000);

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

            // Теперь эта функция снова пишет логи в монитор!
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

// === УМНАЯ ОТПРАВКА ===
void sendSmart(int val, String deviceStatus, int batLevel)
{

    String path = "/pushingbox?devid=" + String(DEVID) +
                  "&val=" + String(val) +
                  "&device=" + deviceStatus +
                  "&bat=" + String(batLevel);

    bool sent = false;

    // 1. WI-FI
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

        // Проверка успеха (Ошибок нет И статус 200 ОК)
        if (err == 0 && statusCode == 200)
        {
            SerialMon.println("Success!");
            httpWifi.responseBody();
            sent = true;
        }
        else
        {
            // ВОТ ТУТ МЫ ТЕПЕРЬ УВИДИМ ПРИЧИНУ
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

    // 2. GSM (РЕЗЕРВ)
    if (!sent)
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
}

// === ФУНКЦИЯ ЧТЕНИЯ БАТАРЕИ (С ЛОГАМИ) ===
int getBatteryPercentage(bool charging)
{
    long sum = 0;
    for (int i = 0; i < 20; i++)
    {
        sum += analogRead(BAT_PIN);
        delay(5);
    }
    float average = sum / 20.0;

    // Ваш калибровочный коэффициент 2.46
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

    // ВЕРНУЛ ЛОГИ СЮДА:
    SerialMon.print("Bat Voltage: ");
    SerialMon.print(voltage);
    SerialMon.print("V | Mode: ");
    SerialMon.print(charging ? "Charging" : "Discharging");
    SerialMon.print(" | Level: ");
    SerialMon.print(percentage);
    SerialMon.println("%");

    return percentage;
}