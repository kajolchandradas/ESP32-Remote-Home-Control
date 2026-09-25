#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <EEPROM.h>

// ============================================================================
// WIFI SETTINGS
// ============================================================================

#define WIFI_SSID       "😜 Hae_Krishna 😈"
#define WIFI_PASSWORD   "01774271450@@"

// ============================================================================
// FIREBASE SETTINGS
// ============================================================================

#define API_KEY         "AIzaSyD_pw2tqB_whEsddwJ_RmSidMSMbCWcQfM"
#define DATABASE_URL    "https://esp32-remote-home-control-default-rtdb.asia-southeast1.firebasedatabase.app/"

#define USER_EMAIL      "kajolchandradas3@gmail.com"
#define USER_PASSWORD   "Kajol01568453178@@"

// ============================================================================
// SYSTEM SETTINGS
// ============================================================================

#define USE_EEPROM            1
#define USE_LATCHED_SWITCH    1
#define RELAY_ACTIVE_LOW      1

// ============================================================================
// TIMING
// ============================================================================

#define FIREBASE_READ_INTERVAL       1000UL
#define REMAINING_UPDATE_INTERVAL    1000UL
#define TIMER_SAVE_INTERVAL          10000UL
#define WIFI_RECONNECT_INTERVAL      10000UL
#define SWITCH_DEBOUNCE_TIME         50UL
#define RELAY_BOOT_DELAY             500UL

// ============================================================================
// ESP32 DEVKIT V1 GPIO
// ============================================================================

#define RELAY1      18
#define RELAY2      19

#define SW1         25
#define SW2         26

#define LEDPIN      2

// ============================================================================
// EEPROM
// ============================================================================

#define EEPROM_SIZE          512

#define ADDR_R1              0
#define ADDR_R2              1
#define ADDR_TIMER           10

#define EEPROM_MAGIC_ADDR    100
#define EEPROM_MAGIC_VAL     55

// ============================================================================
// FIREBASE
// ============================================================================

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// ============================================================================
// RELAY STATES
// ============================================================================

bool relay1State = false;
bool relay2State = false;

// ============================================================================
// SWITCH STATES
// ============================================================================

bool lastSW1 = HIGH;
bool lastSW2 = HIGH;

unsigned long lastSW1Time = 0;
unsigned long lastSW2Time = 0;

// ============================================================================
// TIMER
// ============================================================================

// Timer running state
bool timerRunning = false;

// IMPORTANT:
// Do NOT call this variable timerStart.
// ESP32 Core already has a function named timerStart().
//
// Therefore we use:
unsigned long timerStartedAt = 0;

// Timer duration in MILLISECONDS
unsigned long timerDuration = 0;

// ============================================================================
// GENERAL TIMERS
// ============================================================================

unsigned long lastFirebaseRead = 0;
unsigned long lastTimerSave = 0;
unsigned long lastRemainingPush = 0;

// ============================================================================
// RELAY OUTPUT
// ============================================================================

void setRelay(uint8_t pin, bool state)
{
#if RELAY_ACTIVE_LOW

  digitalWrite(pin, state ? LOW : HIGH);

#else

  digitalWrite(pin, state ? HIGH : LOW);

#endif
}

// ============================================================================
// EEPROM SAVE RELAY
// ============================================================================

void saveRelayState()
{
#if USE_EEPROM

  bool changed = false;

  if (EEPROM.read(ADDR_R1) != (uint8_t)relay1State)
  {
    EEPROM.write(ADDR_R1, relay1State);
    changed = true;
  }

  if (EEPROM.read(ADDR_R2) != (uint8_t)relay2State)
  {
    EEPROM.write(ADDR_R2, relay2State);
    changed = true;
  }

  if (EEPROM.read(EEPROM_MAGIC_ADDR) != EEPROM_MAGIC_VAL)
  {
    EEPROM.write(
      EEPROM_MAGIC_ADDR,
      EEPROM_MAGIC_VAL
    );

    changed = true;
  }

  if (changed)
  {
    EEPROM.commit();
  }

#endif
}

// ============================================================================
// EEPROM LOAD RELAY
// ============================================================================

void loadRelayState()
{
#if USE_EEPROM

  if (
    EEPROM.read(EEPROM_MAGIC_ADDR)
    == EEPROM_MAGIC_VAL
  )
  {
    relay1State =
      EEPROM.read(ADDR_R1);

    relay2State =
      EEPROM.read(ADDR_R2);
  }
  else
  {
    relay1State = false;
    relay2State = false;
  }

#else

  relay1State = false;
  relay2State = false;

#endif
}

// ============================================================================
// EEPROM SAVE TIMER
// TIMER IS STORED IN MILLISECONDS
// ============================================================================

void saveTimerRemaining(
  unsigned long milliseconds
)
{
#if USE_EEPROM

  EEPROM.put(
    ADDR_TIMER,
    milliseconds
  );

  EEPROM.write(
    EEPROM_MAGIC_ADDR,
    EEPROM_MAGIC_VAL
  );

  EEPROM.commit();

#endif
}

// ============================================================================
// EEPROM LOAD TIMER
// ============================================================================

unsigned long loadTimerRemaining()
{
  unsigned long remaining = 0;

#if USE_EEPROM

  if (
    EEPROM.read(EEPROM_MAGIC_ADDR)
    == EEPROM_MAGIC_VAL
  )
  {
    EEPROM.get(
      ADDR_TIMER,
      remaining
    );
  }

#endif

  return remaining;
}

// ============================================================================
// WIFI CONNECT
// ============================================================================

void connectWiFi()
{
  Serial.println();
  Serial.println(
    "================================="
  );
  Serial.println(
    "Connecting to WiFi..."
  );
  Serial.println(
    "================================="
  );

  WiFi.mode(WIFI_STA);

  WiFi.setAutoReconnect(true);

  WiFi.persistent(false);

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );

  unsigned long startAttempt =
    millis();

  while (
    WiFi.status()
    != WL_CONNECTED
  )
  {
    delay(500);

    Serial.print(".");

    if (
      millis() - startAttempt
      >= 15000UL
    )
    {
      Serial.println();

      Serial.println(
        "WiFi connection timeout."
      );

      break;
    }
  }

  if (
    WiFi.status()
    == WL_CONNECTED
  )
  {
    Serial.println();

    Serial.println(
      "WiFi Connected!"
    );

    Serial.print(
      "IP Address: "
    );

    Serial.println(
      WiFi.localIP()
    );

    Serial.print(
      "RSSI: "
    );

    Serial.println(
      WiFi.RSSI()
    );
  }
  else
  {
    Serial.println();

    Serial.println(
      "WiFi not connected."
    );
  }
}

// ============================================================================
// WIFI MAINTENANCE
// ============================================================================

void maintainWiFi()
{
  static unsigned long lastRetry = 0;

  if (
    WiFi.status()
    == WL_CONNECTED
  )
  {
    return;
  }

  if (
    millis() - lastRetry
    >= WIFI_RECONNECT_INTERVAL
  )
  {
    lastRetry =
      millis();

    Serial.println(
      "WiFi disconnected."
    );

    Serial.println(
      "Retrying WiFi..."
    );

    WiFi.disconnect();

    delay(100);

    WiFi.begin(
      WIFI_SSID,
      WIFI_PASSWORD
    );
  }
}

// ============================================================================
// FIREBASE INITIALIZE
// ============================================================================

void connectFirebase()
{
  Serial.println();

  Serial.println(
    "Starting Firebase..."
  );

  config.api_key =
    API_KEY;

  config.database_url =
    DATABASE_URL;

  auth.user.email =
    USER_EMAIL;

  auth.user.password =
    USER_PASSWORD;

  Firebase.begin(
    &config,
    &auth
  );

  Firebase.reconnectWiFi(true);

  Serial.println(
    "Firebase initialized."
  );
}

// ============================================================================
// SETUP
// ============================================================================

void setup()
{
  Serial.begin(115200);

  delay(1000);

  Serial.println();

  Serial.println(
    "=========================================="
  );

  Serial.println(
    "ESP32 Firebase Relay Controller"
  );

  Serial.println(
    "ESP32 DevKit V1"
  );

  Serial.println(
    "=========================================="
  );

  // --------------------------------------------------------------------------
  // EEPROM
  // --------------------------------------------------------------------------

#if USE_EEPROM

  if (
    !EEPROM.begin(EEPROM_SIZE)
  )
  {
    Serial.println(
      "EEPROM initialization failed!"
    );
  }
  else
  {
    Serial.println(
      "EEPROM initialized."
    );
  }

#endif

  // --------------------------------------------------------------------------
  // RELAY
  // --------------------------------------------------------------------------

  pinMode(
    RELAY1,
    OUTPUT
  );

  pinMode(
    RELAY2,
    OUTPUT
  );

  // Start OFF

  setRelay(
    RELAY1,
    false
  );

  setRelay(
    RELAY2,
    false
  );

  // --------------------------------------------------------------------------
  // SWITCH
  // --------------------------------------------------------------------------

  pinMode(
    SW1,
    INPUT_PULLUP
  );

  pinMode(
    SW2,
    INPUT_PULLUP
  );

  // --------------------------------------------------------------------------
  // LED
  // --------------------------------------------------------------------------

  pinMode(
    LEDPIN,
    OUTPUT
  );

  digitalWrite(
    LEDPIN,
    LOW
  );

  // --------------------------------------------------------------------------
  // LOAD SAVED RELAY STATE
  // --------------------------------------------------------------------------

  loadRelayState();

  delay(
    RELAY_BOOT_DELAY
  );

  setRelay(
    RELAY1,
    relay1State
  );

  setRelay(
    RELAY2,
    relay2State
  );

  // --------------------------------------------------------------------------
  // WIFI
  // --------------------------------------------------------------------------

  connectWiFi();

  // --------------------------------------------------------------------------
  // FIREBASE
  // --------------------------------------------------------------------------

  connectFirebase();

  // --------------------------------------------------------------------------
  // RESTORE TIMER
  // --------------------------------------------------------------------------

  unsigned long remaining =
    loadTimerRemaining();

  if (
    remaining > 0
  )
  {
    Serial.println();

    Serial.println(
      "Restoring timer..."
    );

    Serial.print(
      "Remaining milliseconds: "
    );

    Serial.println(
      remaining
    );

    timerRunning = true;

    // remaining is already milliseconds

    timerDuration =
      remaining;

    // FIXED VARIABLE NAME
    timerStartedAt =
      millis();

    // Relay 2 ON

    relay2State =
      true;

    setRelay(
      RELAY2,
      true
    );

    digitalWrite(
      LEDPIN,
      HIGH
    );

    if (
      Firebase.ready()
    )
    {
      Firebase.RTDB.setBool(
        &fbdo,
        "/relay2",
        true
      );

      Firebase.RTDB.setBool(
        &fbdo,
        "/timerActive",
        true
      );

      Firebase.RTDB.setInt(
        &fbdo,
        "/remaining",
        (int)remaining
      );
    }
  }
  else
  {
    digitalWrite(
      LEDPIN,
      LOW
    );
  }

  Serial.println();

  Serial.println(
    "=========================================="
  );

  Serial.println(
    "System Ready"
  );

  Serial.println(
    "=========================================="
  );
}

// ============================================================================
// PHYSICAL SWITCH
// ============================================================================

void checkSwitches()
{
  bool sw1 =
    digitalRead(SW1);

  bool sw2 =
    digitalRead(SW2);

#if USE_LATCHED_SWITCH

  // --------------------------------------------------------------------------
  // SWITCH 1 -> RELAY 1
  // --------------------------------------------------------------------------

  if (
    sw1 != lastSW1 &&
    millis() - lastSW1Time
    >= SWITCH_DEBOUNCE_TIME
  )
  {
    lastSW1Time =
      millis();

    relay1State =
      (sw1 == LOW);

    setRelay(
      RELAY1,
      relay1State
    );

    if (
      Firebase.ready()
    )
    {
      Firebase.RTDB.setBool(
        &fbdo,
        "/relay1",
        relay1State
      );
    }

    saveRelayState();
  }

  // --------------------------------------------------------------------------
  // SWITCH 2 -> RELAY 2
  // --------------------------------------------------------------------------

  if (
    sw2 != lastSW2 &&
    !timerRunning &&
    millis() - lastSW2Time
    >= SWITCH_DEBOUNCE_TIME
  )
  {
    lastSW2Time =
      millis();

    relay2State =
      (sw2 == LOW);

    setRelay(
      RELAY2,
      relay2State
    );

    if (
      Firebase.ready()
    )
    {
      Firebase.RTDB.setBool(
        &fbdo,
        "/relay2",
        relay2State
      );
    }

    saveRelayState();
  }

#else

  // --------------------------------------------------------------------------
  // PUSH BUTTON SWITCH 1
  // --------------------------------------------------------------------------

  if (
    sw1 == LOW &&
    lastSW1 == HIGH &&
    millis() - lastSW1Time
    >= SWITCH_DEBOUNCE_TIME
  )
  {
    lastSW1Time =
      millis();

    relay1State =
      !relay1State;

    setRelay(
      RELAY1,
      relay1State
    );

    if (
      Firebase.ready()
    )
    {
      Firebase.RTDB.setBool(
        &fbdo,
        "/relay1",
        relay1State
      );
    }

    saveRelayState();
  }

  // --------------------------------------------------------------------------
  // PUSH BUTTON SWITCH 2
  // --------------------------------------------------------------------------

  if (
    sw2 == LOW &&
    lastSW2 == HIGH &&
    !timerRunning &&
    millis() - lastSW2Time
    >= SWITCH_DEBOUNCE_TIME
  )
  {
    lastSW2Time =
      millis();

    relay2State =
      !relay2State;

    setRelay(
      RELAY2,
      relay2State
    );

    if (
      Firebase.ready()
    )
    {
      Firebase.RTDB.setBool(
        &fbdo,
        "/relay2",
        relay2State
      );
    }

    saveRelayState();
  }

#endif

  lastSW1 =
    sw1;

  lastSW2 =
    sw2;
}

// ============================================================================
// READ FIREBASE
// ============================================================================

void readFirebase()
{
  if (
    !Firebase.ready()
  )
  {
    return;
  }

  // ==========================================================================
  // RELAY 1
  // ==========================================================================

  if (
    Firebase.RTDB.getBool(
      &fbdo,
      "/relay1"
    )
  )
  {
    bool value =
      fbdo.boolData();

    if (
      value != relay1State
    )
    {
      relay1State =
        value;

      setRelay(
        RELAY1,
        relay1State
      );

      saveRelayState();

      Serial.print(
        "Relay 1 changed: "
      );

      Serial.println(
        relay1State
        ? "ON"
        : "OFF"
      );
    }
  }

  // ==========================================================================
  // RELAY 2
  // ==========================================================================

  if (
    !timerRunning
  )
  {
    if (
      Firebase.RTDB.getBool(
        &fbdo,
        "/relay2"
      )
    )
    {
      bool value =
        fbdo.boolData();

      if (
        value != relay2State
      )
      {
        relay2State =
          value;

        setRelay(
          RELAY2,
          relay2State
        );

        saveRelayState();

        Serial.print(
          "Relay 2 changed: "
        );

        Serial.println(
          relay2State
          ? "ON"
          : "OFF"
        );
      }
    }
  }

  // ==========================================================================
  // TIMER
  // ==========================================================================
  //
  // Firebase /remaining = MILLISECONDS
  //
  // 1 Second = 1000
  // 1 Minute = 60000
  // 1 Hour   = 3600000
  //
  // ==========================================================================

  if (
    Firebase.RTDB.getBool(
      &fbdo,
      "/timerActive"
    )
  )
  {
    bool command =
      fbdo.boolData();

    // ------------------------------------------------------------------------
    // START TIMER
    // ------------------------------------------------------------------------

    if (
      command &&
      !timerRunning
    )
    {
      if (
        Firebase.RTDB.getInt(
          &fbdo,
          "/remaining"
        )
      )
      {
        long remainingValue =
          fbdo.intData();

        if (
          remainingValue > 0
        )
        {
          unsigned long milliseconds =
            (unsigned long)remainingValue;

          Serial.println();

          Serial.println(
            "Starting timer from Firebase."
          );

          Serial.print(
            "Timer milliseconds: "
          );

          Serial.println(
            milliseconds
          );

          timerRunning =
            true;

          timerDuration =
            milliseconds;

          // FIXED:
          // timerStart -> timerStartedAt

          timerStartedAt =
            millis();

          // Relay 2 ON

          relay2State =
            true;

          setRelay(
            RELAY2,
            true
          );

          digitalWrite(
            LEDPIN,
            HIGH
          );

          lastRemainingPush =
            millis();

          lastTimerSave =
            millis();

          saveRelayState();

          saveTimerRemaining(
            milliseconds
          );

          Firebase.RTDB.setBool(
            &fbdo,
            "/relay2",
            true
          );
        }
      }
    }

    // ------------------------------------------------------------------------
    // STOP TIMER
    // ------------------------------------------------------------------------

    if (
      !command &&
      timerRunning
    )
    {
      Serial.println(
        "Timer stopped from Firebase."
      );

      timerRunning =
        false;

      relay2State =
        false;

      setRelay(
        RELAY2,
        false
      );

      digitalWrite(
        LEDPIN,
        LOW
      );

      saveTimerRemaining(
        0
      );

      saveRelayState();

      Firebase.RTDB.setBool(
        &fbdo,
        "/relay2",
        false
      );

      Firebase.RTDB.setInt(
        &fbdo,
        "/remaining",
        0
      );
    }
  }

  // ==========================================================================
  // DEVICE STATUS
  // ==========================================================================

  if (
    Firebase.RTDB.getBool(
      &fbdo,
      "/statusRequest"
    )
  )
  {
    bool request =
      fbdo.boolData();

    if (
      request
    )
    {
      Firebase.RTDB.setString(
        &fbdo,
        "/deviceStatus",
        "Connected"
      );

      Firebase.RTDB.setBool(
        &fbdo,
        "/statusReply",
        true
      );

      Firebase.RTDB.setBool(
        &fbdo,
        "/statusRequest",
        false
      );

      Serial.println(
        "Status reply sent."
      );
    }
  }
}

// ============================================================================
// TIMER HANDLER
// ============================================================================

void handleTimer()
{
  if (
    !timerRunning
  )
  {
    return;
  }

  unsigned long now =
    millis();

  // FIXED:
  // timerStart -> timerStartedAt

  unsigned long elapsed =
    now - timerStartedAt;

  // ==========================================================================
  // TIMER FINISHED
  // ==========================================================================

  if (
    elapsed >= timerDuration
  )
  {
    Serial.println();

    Serial.println(
      "================================="
    );

    Serial.println(
      "TIMER FINISHED"
    );

    Serial.println(
      "================================="
    );

    timerRunning =
      false;

    // Relay 2 OFF

    relay2State =
      false;

    setRelay(
      RELAY2,
      false
    );

    digitalWrite(
      LEDPIN,
      LOW
    );

    if (
      Firebase.ready()
    )
    {
      Firebase.RTDB.setBool(
        &fbdo,
        "/relay2",
        false
      );

      Firebase.RTDB.setBool(
        &fbdo,
        "/timerActive",
        false
      );

      Firebase.RTDB.setInt(
        &fbdo,
        "/remaining",
        0
      );
    }

    saveRelayState();

    saveTimerRemaining(
      0
    );

    return;
  }

  // ==========================================================================
  // REMAINING MILLISECONDS
  // ==========================================================================

  unsigned long remainingMilliseconds =
    timerDuration - elapsed;

  // ==========================================================================
  // UPDATE FIREBASE
  // ==========================================================================

  if (
    now - lastRemainingPush
    >= REMAINING_UPDATE_INTERVAL
  )
  {
    lastRemainingPush =
      now;

    if (
      Firebase.ready()
    )
    {
      Firebase.RTDB.setInt(
        &fbdo,
        "/remaining",
        (int)remainingMilliseconds
      );
    }

    Serial.print(
      "Timer remaining: "
    );

    Serial.print(
      remainingMilliseconds
    );

    Serial.println(
      " ms"
    );
  }

  // ==========================================================================
  // EEPROM TIMER SAVE
  // ==========================================================================

  if (
    now - lastTimerSave
    >= TIMER_SAVE_INTERVAL
  )
  {
    lastTimerSave =
      now;

    saveTimerRemaining(
      remainingMilliseconds
    );
  }
}

// ============================================================================
// LOOP
// ============================================================================

void loop()
{
  // WiFi
  maintainWiFi();

  // Physical switches
  checkSwitches();

  // Firebase
  if (
    millis() - lastFirebaseRead
    >= FIREBASE_READ_INTERVAL
  )
  {
    lastFirebaseRead =
      millis();

    readFirebase();
  }

  // Timer
  handleTimer();

  delay(1);
}