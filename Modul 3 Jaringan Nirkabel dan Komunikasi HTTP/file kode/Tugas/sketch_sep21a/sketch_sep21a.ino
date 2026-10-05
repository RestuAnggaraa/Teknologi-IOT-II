#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DHT.h>

const char* ssid = "cihuyyy";
const char* password = "12345678";

// ================= PIN =================
const byte dhtPin = 2;        // D4 (GPIO 2)
const byte buttonPin = 4;     // D2 (GPIO 4)
const byte ledPin = 12;       // D6 (GPIO 12)
const byte pinLED = 5;        // D1 (GPIO 5) - LED PWM

// ================= DHT =================
#define DHTTYPE DHT22
DHT dht(dhtPin, DHTTYPE);

// ================= VARIABEL =================
bool ledState = false;

String currentTemp = "--";
String currentHum = "--";

int pwmValue = 0;

int buttonState = LOW;
int lastButtonState = LOW;

unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

unsigned long lastTime = 0;

// ================= WEB SERVER =================
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ================= HTML =================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">

  <title>Smart Lighting IoT</title>

  <style>
    body {
      font-family: Arial;
      text-align: center;
      background: #eeeeee;
    }

    .card {
      background: white;
      margin: 20px auto;
      padding: 20px;
      max-width: 350px;
      border-radius: 10px;
      box-shadow: 0 2px 8px rgba(0,0,0,0.15);
    }

    button {
      padding: 15px 30px;
      font-size: 20px;
      border-radius: 5px;
      cursor: pointer;
      color: white;
      border: none;
    }

    .btn-on {
      background-color: #4CAF50;
    }

    .btn-off {
      background-color: #f44336;
    }

    input[type=range] {
      width: 90%;
    }

    .value {
      font-size: 25px;
      font-weight: bold;
    }
  </style>
</head>

<body>

  <h1>Smart Room</h1>

  <!-- ================= SUHU ================= -->
  <div class="card">
    <h2>Suhu</h2>
    <p class="value">
      <span id="tempValue">--</span> &deg;C
    </p>
  </div>

  <!-- ================= KELEMBAPAN ================= -->
  <div class="card">
    <h2>Kelembapan</h2>
    <p class="value">
      <span id="humValue">--</span> %
    </p>
  </div>

  <!-- ================= LED ON OFF ================= -->
  <div class="card">
    <h2>LED ON/OFF</h2>

    <h3>
      Status:
      <span id="ledStatus">OFF</span>
    </h3>

    <button
      id="toggleBtn"
      class="btn-off"
      onclick="toggleLed()">
      Turn ON
    </button>
  </div>

  <!-- ================= LED DIMMER ================= -->
  <div class="card">
    <h2>LED Dimmer</h2>

    <p>
      Intensitas:
      <span id="pwmValue">0</span>
    </p>

    <input
      type="range"
      min="0"
      max="1023"
      value="0"
      id="pwmSlider"
      oninput="sendPWM(this.value)"
    >

    <p>0 = Mati</p>
    <p>1023 = Terang Maksimal</p>
  </div>

  <script>

    // ================= WEBSOCKET =================

    var gateway = `ws://${window.location.hostname}/ws`;

    var websocket;

    window.addEventListener('load', onLoad);

    function onLoad(event) {
      initWebSocket();
    }

    function initWebSocket() {

      websocket = new WebSocket(gateway);

      websocket.onopen = onOpen;

      websocket.onclose = onClose;

      websocket.onmessage = onMessage;
    }

    function onOpen(event) {

      console.log("WebSocket Terkoneksi");
    }

    function onClose(event) {

      console.log("WebSocket Terputus");

      setTimeout(initWebSocket, 2000);
    }


    // ================= LED ON OFF =================

    function toggleLed() {

      websocket.send("toggle");
    }


    // ================= LED PWM =================

    function sendPWM(value) {

      document.getElementById("pwmValue").innerHTML = value;

      websocket.send("pwm," + value);
    }


    // ================= TERIMA DATA =================

    function onMessage(event) {

      var dataObj = JSON.parse(event.data);


      // ---------- SUHU ----------

      if (dataObj.suhu !== undefined) {

        document.getElementById("tempValue").innerHTML =
          dataObj.suhu;
      }


      // ---------- KELEMBAPAN ----------

      if (dataObj.hum !== undefined) {

        document.getElementById("humValue").innerHTML =
          dataObj.hum;
      }


      // ---------- LED ON OFF ----------

      if (dataObj.led !== undefined) {

        var btn =
          document.getElementById("toggleBtn");

        var status =
          document.getElementById("ledStatus");


        if (dataObj.led == "1") {

          status.innerHTML = "ON";

          btn.innerHTML = "Turn OFF";

          btn.className = "btn-on";

        } else {

          status.innerHTML = "OFF";

          btn.innerHTML = "Turn ON";

          btn.className = "btn-off";
        }
      }


      // ---------- PWM ----------

      if (dataObj.pwm !== undefined) {

        document.getElementById("pwmValue").innerHTML =
          dataObj.pwm;

        document.getElementById("pwmSlider").value =
          dataObj.pwm;
      }
    }

  </script>

</body>
</html>
)rawliteral";


// ======================================================
// NOTIFY CLIENT
// Mengirim data LED, suhu, kelembapan dan PWM
// ======================================================

void notifyClients() {

  String jsonString =
    "{\"led\":\"" +
    String(ledState ? 1 : 0) +
    "\", ";

  jsonString +=
    "\"suhu\":\"" +
    currentTemp +
    "\", ";

  jsonString +=
    "\"hum\":\"" +
    currentHum +
    "\", ";

  jsonString +=
    "\"pwm\":\"" +
    String(pwmValue) +
    "\"}";

  ws.textAll(jsonString);
}


// ======================================================
// HANDLE WEBSOCKET MESSAGE
// ======================================================

void handleWebSocketMessage(
  void *arg,
  uint8_t *data,
  size_t len
) {

  AwsFrameInfo *info =
    (AwsFrameInfo*)arg;


  if (
    info->final &&
    info->index == 0 &&
    info->len == len &&
    info->opcode == WS_TEXT
  ) {

    data[len] = 0;


    // Ubah data menjadi String
    String message =
      String((char*)data);


    // ==================================================
    // TOMBOL ON / OFF
    // ==================================================

    if (message == "toggle") {

      ledState = !ledState;

      notifyClients();
    }


    // ==================================================
    // PWM
    // Format: pwm,512
    // ==================================================

    else if (message.startsWith("pwm,")) {

      String nilai =
        message.substring(4);


      pwmValue =
        nilai.toInt();


      // Membatasi nilai PWM
      // 0 sampai 1023

      pwmValue =
        constrain(
          pwmValue,
          0,
          1023
        );


      // Mengatur brightness LED

      analogWrite(
        pinLED,
        pwmValue
      );


      // Kirim status ke browser

      notifyClients();
    }
  }
}


// ======================================================
// WEBSOCKET EVENT
// ======================================================

void onEvent(
  AsyncWebSocket *server,
  AsyncWebSocketClient *client,
  AwsEventType type,
  void *arg,
  uint8_t *data,
  size_t len
) {

  switch (type) {

    case WS_EVT_CONNECT:

      Serial.printf(
        "Client WebSocket #%u terhubung\n",
        client->id()
      );

      // Kirim status saat browser dibuka

      notifyClients();

      break;


    case WS_EVT_DISCONNECT:

      Serial.printf(
        "Client WebSocket #%u terputus\n",
        client->id()
      );

      break;


    case WS_EVT_DATA:

      handleWebSocketMessage(
        arg,
        data,
        len
      );

      break;
  }
}


// ======================================================
// SETUP
// ======================================================

void setup() {

  Serial.begin(115200);


  // ================= BUTTON =================

  pinMode(
    buttonPin,
    INPUT
  );


  // ================= LED ON/OFF =================

  pinMode(
    ledPin,
    OUTPUT
  );

  digitalWrite(
    ledPin,
    LOW
  );


  // ================= LED PWM =================

  pinMode(
    pinLED,
    OUTPUT
  );

  analogWrite(
    pinLED,
    0
  );


  // ================= DHT =================

  dht.begin();


  // ================= WIFI =================

  WiFi.mode(WIFI_STA);

  WiFi.begin(
    ssid,
    password
  );


  Serial.print(
    "Menghubungkan WiFi"
  );


  while (
    WiFi.status() != WL_CONNECTED
  ) {

    delay(500);

    Serial.print(".");
  }


  Serial.println();

  Serial.print(
    "IP Address: "
  );

  Serial.println(
    WiFi.localIP()
  );


  // ================= WEB =================

  server.on(
    "/",
    HTTP_GET,
    [](AsyncWebServerRequest *request) {

      request->send_P(
        200,
        "text/html",
        index_html
      );
    }
  );


  // ================= WEBSOCKET =================

  ws.onEvent(onEvent);

  server.addHandler(&ws);


  // ================= START SERVER =================

  server.begin();

  Serial.println(
    "Web Server Started"
  );
}


// ======================================================
// LOOP
// ======================================================

void loop() {

  ws.cleanupClients();


  // ====================================================
  // LED ON / OFF
  // ====================================================

  digitalWrite(
    ledPin,
    ledState ? HIGH : LOW
  );


  // ====================================================
  // BUTTON FISIK
  // ====================================================

  int reading =
    digitalRead(buttonPin);


  if (
    reading != lastButtonState
  ) {

    lastDebounceTime =
      millis();
  }


  if (
    (millis() - lastDebounceTime)
    > debounceDelay
  ) {

    if (
      reading != buttonState
    ) {

      buttonState =
        reading;


      // Tombol ditekan = HIGH

      if (
        buttonState == HIGH
      ) {

        ledState =
          !ledState;


        // Update browser

        notifyClients();
      }
    }
  }


  lastButtonState =
    reading;


  // ====================================================
  // DHT SETIAP 3 DETIK
  // ====================================================

  if (
    (millis() - lastTime)
    > 3000
  ) {

    // Baca suhu

    float t =
      dht.readTemperature();


    // Baca kelembapan

    float h =
      dht.readHumidity();


    // Jika pembacaan valid

    if (
      !isnan(t) &&
      !isnan(h)
    ) {

      currentTemp =
        String(t);

      currentHum =
        String(h);


      // Kirim data ke browser

      notifyClients();
    }


    lastTime =
      millis();
  }
}