// ============================================================
// NEWS
// ============================================================

String getTopNews(
  String category = "general"
) {

  if (
    strlen(
      NEWS_API_KEY
    ) ==
    0
  ) {

    return
      "News API key is not configured.";
  }


  WiFiClientSecure client;

  client.setInsecure();


  HTTPClient https;


  String url =
    "https://newsapi.org/v2/top-headlines?category=" +
    category +
    "&language=en&pageSize=3&apiKey=" +
    String(NEWS_API_KEY);


  if (
    !https.begin(
      client,
      url
    )
  ) {

    return
      "Unable to connect to News API.";
  }


  https.addHeader(
    "User-Agent",
    "VIPER-SmartGlasses/1.0"
  );


  int httpCode =
    https.GET();


  String result =
    "No headlines found right now.";


  if (
    httpCode ==
    HTTP_CODE_OK
  ) {

    String payload =
      https.getString();


    JsonDocument doc;


    DeserializationError err =
      deserializeJson(
        doc,
        payload
      );


    if (!err) {

      JsonArray articles =
        doc["articles"]
        .as<JsonArray>();


      if (
        articles.size() >
        0
      ) {

        result =
          "Here are the top headlines: ";


        for (
          int i = 0;
          i < articles.size() &&
          i < 3;
          i++
        ) {

          const char* title =
            articles[i]["title"] |
            "Untitled";


          result +=
            String(i + 1) +
            ". " +
            String(title) +
            ". ";
        }
      }

    } else {

      LOG_PRINTF(
        "[Error]: News JSON parse: %s\n",
        err.c_str()
      );
    }

  } else {

    LOG_PRINTF(
      "[Error]: NewsAPI HTTP %d\n",
      httpCode
    );


    LOG_PRINTLN(
      https.getString()
    );


    result =
      "Error fetching news.";
  }


  https.end();


  return result;
}


// ============================================================
// GOOGLE DRIVE
// ============================================================

String searchAndReadDriveFile(
  String query
) {

  String token =
    getValidGoogleAccessToken();


  if (
    token.length() ==
    0
  ) {

    return
      "Authentication error.";
  }


  // Basic safe replacements for Drive query URL.
  query.replace(
    " ",
    "%20"
  );


  query.replace(
    "'",
    "%27"
  );


  WiFiClientSecure client;

  client.setInsecure();


  HTTPClient https;


  String url =
    "https://www.googleapis.com/drive/v3/files?q=name%20contains%20%27" +
    query +
    "%27&pageSize=1&fields=files(id,name,mimeType)";


  if (
    !https.begin(
      client,
      url
    )
  ) {

    return
      "Unable to connect to Google Drive.";
  }


  https.addHeader(
    "Authorization",
    "Bearer " +
    token
  );


  int httpCode =
    https.GET();


  String fileContent =
    "No matching file found in Google Drive.";


  if (
    httpCode ==
    HTTP_CODE_OK
  ) {

    JsonDocument doc;


    DeserializationError err =
      deserializeJson(
        doc,
        https.getString()
      );


    if (!err) {

      JsonArray files =
        doc["files"]
        .as<JsonArray>();


      if (
        files.size() >
        0
      ) {

        String fileId =
          String(
            files[0]["id"] |
            ""
          );


        String fileName =
          String(
            files[0]["name"] |
            ""
          );


        String mimeType =
          String(
            files[0]["mimeType"] |
            ""
          );


        https.end();


        String fetchUrl =
          "";


        if (
          mimeType ==
          "application/vnd.google-apps.document"
        ) {

          fetchUrl =
            "https://www.googleapis.com/drive/v3/files/" +
            fileId +
            "/export?mimeType=text/plain";

        } else if (
          mimeType ==
          "text/plain"
        ) {

          fetchUrl =
            "https://www.googleapis.com/drive/v3/files/" +
            fileId +
            "?alt=media";
        }


        if (
          fetchUrl.length() >
          0
        ) {

          if (
            https.begin(
              client,
              fetchUrl
            )
          ) {

            https.addHeader(
              "Authorization",
              "Bearer " +
              token
            );


            if (
              https.GET() ==
              HTTP_CODE_OK
            ) {

              String text =
                https.getString();


              if (
                text.length() >
                1500
              ) {

                text =
                  text.substring(
                    0,
                    1500
                  ) +
                  "... [Truncated]";
              }


              fileContent =
                "Content of '" +
                fileName +
                "':\n" +
                text;
            }
          }

        } else {

          fileContent =
            "Found '" +
            fileName +
            "', but its file type cannot be read directly.";
        }
      }
    }
  }


  https.end();


  return fileContent;
}


// ============================================================
// TELEGRAM PHOTO
// ============================================================

String takePhotoAndSendToPhone() {

  if (
    strlen(
      TELEGRAM_BOT_TOKEN
    ) ==
    0
  ) {

    return
      "Telegram bot token is not configured.";
  }


  if (
    strlen(
      TELEGRAM_CHAT_ID
    ) ==
    0
  ) {

    return
      "Telegram chat ID is not configured.";
  }


  LOG_PRINTLN(
    "[System]: Capturing photo for Telegram..."
  );


  camera_fb_t* fb =
    esp_camera_fb_get();


  if (!fb) {

    return
      "Hardware error: Failed to capture the image.";
  }


  WiFiClientSecure client;

  client.setInsecure();


  HTTPClient https;


  String url =
    "https://api.telegram.org/bot" +
    String(TELEGRAM_BOT_TOKEN) +
    "/sendPhoto";


  if (
    !https.begin(
      client,
      url
    )
  ) {

    esp_camera_fb_return(
      fb
    );


    return
      "Unable to connect to Telegram.";
  }


  String boundary =
    "ViperTelegramBoundary";


  String head =
    "--" +
    boundary +
    "\r\n"
    "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n" +
    String(
      TELEGRAM_CHAT_ID
    ) +
    "\r\n"
    "--" +
    boundary +
    "\r\n"
    "Content-Disposition: form-data; name=\"photo\"; filename=\"viper.jpg\"\r\n"
    "Content-Type: image/jpeg\r\n\r\n";


  String tail =
    "\r\n--" +
    boundary +
    "--\r\n";


  size_t totalLen =
    head.length() +
    fb->len +
    tail.length();


  uint8_t* body =
    (uint8_t*)ps_malloc(
      totalLen
    );


  if (!body) {

    https.end();


    esp_camera_fb_return(
      fb
    );


    return
      "Memory error while preparing photo.";
  }


  size_t offset =
    0;


  memcpy(
    body + offset,
    head.c_str(),
    head.length()
  );


  offset +=
    head.length();


  memcpy(
    body + offset,
    fb->buf,
    fb->len
  );


  offset +=
    fb->len;


  memcpy(
    body + offset,
    tail.c_str(),
    tail.length()
  );


  https.addHeader(
    "Content-Type",
    "multipart/form-data; boundary=" +
    boundary
  );


  int httpCode =
    https.POST(
      body,
      totalLen
    );


  String result;


  if (
    httpCode ==
    HTTP_CODE_OK
  ) {

    result =
      "I took the photo and successfully sent it to your phone.";

  } else {

    LOG_PRINTF(
      "[Error]: Telegram HTTP %d\n",
      httpCode
    );


    LOG_PRINTLN(
      https.getString()
    );


    result =
      "I took the picture, but there was a network error sending it.";
  }


  free(body);


  https.end();


  esp_camera_fb_return(
    fb
  );


  return result;
}
