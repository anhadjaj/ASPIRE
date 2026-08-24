// ============================================================
// WIFI
// ============================================================

void connectWiFi() {

  LOG_PRINT(
    "[System]: Initializing WiFi..."
  );


  WiFi.persistent(
    false
  );


  WiFi.disconnect(
    true,
    true
  );


  delay(200);


  WiFi.mode(
    WIFI_STA
  );


  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );


  bool ledState =
    false;


  unsigned long startAttemptTime =
    millis();


  while (
    WiFi.status() !=
      WL_CONNECTED
    &&
    millis() -
      startAttemptTime <
      15000
  ) {

    digitalWrite(
      LED_PIN,
      ledState
        ? LOW
        : HIGH
    );


    ledState =
      !ledState;


    delay(300);


    LOG_PRINT(".");
  }


  if (
    WiFi.status() !=
    WL_CONNECTED
  ) {

    LOG_PRINTLN(
      "\n[Error]: WiFi connection timed out. Restarting board..."
    );


    digitalWrite(
      LED_PIN,
      HIGH
    );


    delay(1000);


    ESP.restart();
  }


  digitalWrite(
    LED_PIN,
    HIGH
  );


  LOG_PRINTLN(
    " connected!"
  );


  LOG_PRINT(
    "[WiFi] IP: "
  );


  LOG_PRINTLN(
    WiFi.localIP()
  );


  LOG_PRINTF(
    "[WiFi] RSSI: %d dBm\n",
    WiFi.RSSI()
  );


  printMemoryDebug(
    "AFTER_WIFI"
  );
}


// ============================================================
// TIME
// ============================================================

void syncTime() {

  delay(1000);


  configTzTime(
    TZ_STRING,
    "pool.ntp.org",
    "time.google.com",
    "time.windows.com"
  );


  LOG_PRINT(
    "[System]: Syncing time"
  );


  struct tm timeinfo;


  bool ledState =
    false;


  unsigned long startAttemptTime =
    millis();


  bool synced =
    false;


  while (
    millis() -
      startAttemptTime <
      10000
  ) {

    if (
      getLocalTime(
        &timeinfo,
        100
      )
    ) {

      synced =
        true;

      break;
    }


    digitalWrite(
      LED_PIN,
      ledState
        ? LOW
        : HIGH
    );


    ledState =
      !ledState;


    LOG_PRINT(".");


    delay(400);
  }


  digitalWrite(
    LED_PIN,
    HIGH
  );


  if (!synced) {

    LOG_PRINTLN(
      "\n[Warning]: Time sync timed out."
    );

  } else {

    LOG_PRINTLN(
      " synced!"
    );


    char timeString[80];


    strftime(
      timeString,
      sizeof(timeString),
      "%Y-%m-%d %H:%M:%S",
      &timeinfo
    );


    LOG_PRINT(
      "[TIME] Current time: "
    );


    LOG_PRINTLN(
      timeString
    );
  }
}
