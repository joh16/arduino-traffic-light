#include <Stepper.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

// Wi-Fi
const char *ssid = "yourssid";  // FIXME: edit this and below
const char *password = "yourpassword";

AsyncWebServer server(80);

// pins
const uint8_t VCC = 15;

const uint8_t PED_GREEN = 12;
const uint8_t PED_RED = 13;

const uint8_t CAR_GREEN = 27;
const uint8_t CAR_YELLOW = 26;
const uint8_t CAR_RED = 25;

const uint8_t BUZZER = 2;

uint8_t outPins[] = {
  VCC, PED_GREEN, PED_RED, CAR_GREEN,
  CAR_YELLOW, CAR_RED, BUZZER
};

const int BUTTON = 34;
const int CDS = 35;

const int STEP[4] = { 5, 19, 18, 23 };

// state variables
volatile uint8_t pedRedCountdown = 0;
uint8_t pedRed = HIGH;
uint8_t pedGreenCountdown = 0;
uint8_t darkChangeCountdown = 0;
bool isDark = true;
unsigned long carGreenUntil = millis();
char *text[2] = { "Initializing...", "" };
char buf[1000];

// times
const uint8_t MIN_CAR_GREEN = 15;
const uint8_t CDS_DEBOUNCE = 5;
const uint8_t UNTIL_PED_GREEN = 5;
const uint8_t PED_GREEN_HURRY = 5;
const uint8_t PED_GREEN_TOTAL = 8;
const uint8_t CAR_TO_YELLOW = 2;

// I2C (LCD) - SCL: 22, SDA: 21
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Stepper
const int MPR = 32;
const int GEAR = 64;
const int QUARTER_REV = MPR * GEAR / 4;
Stepper stepper(MPR, STEP[0], STEP[1], STEP[2], STEP[3]);

// Server
const char html[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>

<head>
  <meta charset="utf-8">
  <title>ESP32 WiFi Test Page</title>
  <style>
    .noFill {
      background-color: transparent !important;
    }
    .green {
      border-color: green !important;
      background-color: green;
    }
    .red {
      border-color: red !important;
      background-color: red;
    }
    .circle {
      border: 3px solid;
      border-radius: 100%;
      width: 16px;
      height: 16px;
      margin: 0 0 0 4px;
    }
  </style>
  <script>
    let crossNode, isDarkNode, redNode, greenNode, countdownNode;
    let isCrossable = true, pedRed = true, count, refreshTimerId, countTimerId;

    const renderCount = (count) => countdownNode.innerHTML = `${
      count + 1
    }초 뒤 ${pedRed ? '초록' : '빨간'}불`;
    const handleResponse = ({ isDark, ped, countdown }) => {
      if (countTimerId) clearInterval(countTimerId);
      refreshTimerId = clearTimeout(refreshTimerId);
      isDarkNode.innerHTML = `모드: ${isDark ? '야간' : '주간'}`;
      pedRed = ped === 'red';
      redNode.classList[pedRed ? 'remove' : 'add']('noFill');
      greenNode.classList[ped === 'green' ? 'remove' : 'add']('noFill');
      crossNode.disabled = (count = countdown) || !pedRed || isDark;
      if (count) {
        renderCount(count);
        countTimerId = setInterval(() => {
          if (!count--) {
            pedRed = !pedRed;
            clearInterval(countTimerId);
            return setTimeout(refresh, 500);
          }
          renderCount(count);
        }, 1000);
      } else {
        countdownNode.innerHTML = '';
        if (!refreshTimerId) refreshTimerId = setTimeout(() => {          
          refresh();
          return refreshTimerId = undefined;
        }, 5000);
      }
    };

    const refresh = () => fetch(`http://${window.location.host}/api/status`)
      .then((res) => res.json())
      .then(handleResponse)
      .catch((e) => alert(`요청에 실패했습니다: ${e.message}`));

    const cross = () => fetch(`http://${window.location.host}/api/cross`, { method: 'POST' })
      .then((res) => {
        if (!res.ok) alert('건너기 요청을 할 수 없는 상태입니다.');
        return res.json().then(handleResponse);
      })
      .catch((e) => alert(`요청에 실패했습니다: ${e.message}`));

    const setup = setTimeout(() => {
      try {
        crossNode = document.getElementById('cross');
        isDarkNode = document.getElementById('isDark');
        redNode = document.getElementById('red');
        greenNode = document.getElementById('green');
        countdownNode = document.getElementById('countdown');
        refresh();
      } catch (e) {
        console.error(e);
        setup();
      }
    }, 100);
    setup();
  </script>
</head>

<body>
  <button onclick="refresh()">새로고침</button>
  <button id="cross" onclick="cross()">건너기</button>
  <br>
  <p id="isDark">loading...</p>
  <div style="display:flex;">
    <div>신호: </div>
    <div id="red" class="red circle"></div>
    <div id="green" class="green circle noFill"></div>
  </div>
  <p id="countdown"></p>
</body>

</html>
)=====";

void setup() {
  // pins & interrupts
  for (int i = 0; i < 7; i++) {
    pinMode(outPins[i], OUTPUT);
  }
  digitalWrite(VCC, HIGH);
  pinMode(CDS, INPUT);
  pinMode(BUTTON, INPUT);
  attachInterrupt(BUTTON, onPed, FALLING);
  ledcAttachPin(BUZZER, 0);

  // Connections
  Serial.begin(9600);
  lcd.init();
  lcd.backlight();
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to ");
  Serial.println(ssid);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Connected.\nIP address:");
  Serial.println(WiFi.localIP());

  // Server
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", html);
  });

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    prepareResponse();
    request->send(200, "application/json", buf);
  });

  server.on("/api/cross", HTTP_POST, [](AsyncWebServerRequest *request) {
    uint16_t status = isDark || pedRed == LOW || pedRedCountdown > 0 ? 409 : 200;
    if (status == 200) onPed();
    prepareResponse();
    request->send(status, "application/json", buf);
  });

  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
  });

  server.begin();

  // Stepper
  stepper.setSpeed(800);
}

void loop() {
  if (pedRedCountdown > 0) handlePed();
  if (darkChangeCountdown == 0) {
    if (isDark = !isDark) onDark();
    else onBright();
    darkChangeCountdown = CDS_DEBOUNCE;
  } else darkChangeCountdown = isDark == analogRead(CDS) < 300
                                 ? CDS_DEBOUNCE
                                 : darkChangeCountdown - 1;
  for (uint8_t out = LOW; out <= HIGH; out++) {
    delay(440); // supposed to be 500 but considering delay
    if (isDark) digitalWrite(CAR_YELLOW, out);  // blink
  }
}

void prepareResponse() {
  sprintf(
    buf,
    "{\"isDark\":%s,\"ped\":%s,\"countdown\":%d}",
    isDark ? "true" : "false",
    pedRed ? "\"red\"" : isDark ? "null"
                                : "\"green\"",
    pedRed ? pedRedCountdown : pedGreenCountdown);
}

void onDark() {
  updateText("Night mode:", "Watch for cars!");
  digitalWrite(CAR_GREEN, LOW);
  digitalWrite(CAR_YELLOW, HIGH);
  digitalWrite(PED_RED, pedRed = LOW);
}

void onBright() {
  updateText("Day mode:", "Press to cross.");
  digitalWrite(PED_RED, pedRed = HIGH);
  digitalWrite(CAR_GREEN, HIGH);
  digitalWrite(CAR_YELLOW, LOW);
  digitalWrite(CAR_RED, LOW);
  carGreenUntil = millis() + MIN_CAR_GREEN * 1000;
}

void onPed() {
  if (isDark || pedRed == LOW || pedRedCountdown > 0) return;
  unsigned long now = millis();
  uint8_t left = now < carGreenUntil
                   ? (carGreenUntil - now) / 1000
                   : UNTIL_PED_GREEN;
  pedRedCountdown = left > UNTIL_PED_GREEN
                      ? left
                      : UNTIL_PED_GREEN;
}

void handlePed() {
  while (pedRedCountdown > 0) {
    delay(880);
    if (pedRedCountdown == CAR_TO_YELLOW) {
      digitalWrite(CAR_GREEN, LOW);
      digitalWrite(CAR_YELLOW, HIGH);
    }
    updateText("Will be green on", String(pedRedCountdown--).c_str());
  }
  digitalWrite(CAR_YELLOW, LOW);
  digitalWrite(CAR_RED, HIGH);
  stepper.step(-QUARTER_REV);
  pedRed = LOW;
  delay(220);

  digitalWrite(PED_RED, LOW);
  digitalWrite(PED_GREEN, HIGH);

  pedGreenCountdown = PED_GREEN_TOTAL;
  while (pedGreenCountdown > 0) {
    updateText("Will be red on", String(pedGreenCountdown).c_str());
    if (pedGreenCountdown-- <= PED_GREEN_HURRY) {
      toneDuration(880, 110);
      delay(110);
      toneDuration(880, 110);
      delay(110);
      // blink
      digitalWrite(PED_GREEN, LOW);
      delay(440);
      digitalWrite(PED_GREEN, HIGH);
    } else {
      toneDuration(440, 440);
      delay(440);
    }
  }
  digitalWrite(PED_GREEN, LOW);
  digitalWrite(PED_RED, pedRed = (isDark ? LOW : HIGH));
  stepper.step(QUARTER_REV);
  if (isDark) onDark();
  else onBright();
  delay(110);
}

void toneDuration(unsigned int freq, unsigned int duration) {
  ledcAttachPin(BUZZER, 0);
  ledcWriteTone(0, freq);
  delay(duration);
  ledcDetachPin(BUZZER);
}

void updateText(String s0, String s1) {
  lcd.clear();
  lcd.print(text[0] = (char *)s0.c_str());
  lcd.setCursor(0, 1);
  lcd.print(text[1] = (char *)s1.c_str());
  sprintf(buf, "%s\n%s\n", text[0], text[1]);
  Serial.print(buf);
}