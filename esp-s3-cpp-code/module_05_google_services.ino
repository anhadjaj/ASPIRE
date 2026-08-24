// ============================================================
// GOOGLE ACCESS TOKEN
// ============================================================

String getValidGoogleAccessToken() {

  if (
    googleAccessToken.length() >
      0
    &&
    tokenExpiryMillis >
      300000
    &&
    millis() <
      tokenExpiryMillis -
      300000
  ) {

    return googleAccessToken;
  }


  LOG_INFO_PRINTLN(
    "[System]: Google Access Token expired or missing. Fetching new token..."
  );


  WiFiClientSecure client;

  client.setInsecure();


  HTTPClient https;


  if (
    !https.begin(
      client,
      "https://oauth2.googleapis.com/token"
    )
  ) {

    LOG_ERROR_PRINTLN(
      "[Error]: Failed to connect to Google OAuth server."
    );


    return "";
  }


  https.addHeader(
    "Content-Type",
    "application/x-www-form-urlencoded"
  );


  String payload =
    "client_id=" +
    String(
      GOOGLE_CLIENT_ID
    ) +

    "&client_secret=" +
    String(
      GOOGLE_CLIENT_SECRET
    ) +

    "&refresh_token=" +
    String(
      GOOGLE_REFRESH_TOKEN
    ) +

    "&grant_type=refresh_token";


  int httpCode =
    https.POST(
      payload
    );


  String newToken =
    "";


  if (
    httpCode ==
    HTTP_CODE_OK
  ) {

    String response =
      https.getString();


    JsonDocument doc;


    DeserializationError err =
      deserializeJson(
        doc,
        response
      );


    if (!err) {

      newToken =
        String(
          doc["access_token"] |
          ""
        );


      int expiresIn =
        doc["expires_in"] |
        3600;


      googleAccessToken =
        newToken;


      tokenExpiryMillis =
        millis() +
        (
          (unsigned long)expiresIn *
          1000UL
        );


      LOG_INFO_PRINTLN(
        "[System]: Google Access Token refreshed successfully."
      );

    } else {

      LOG_ERROR_PRINTF(
        "[Error]: Token JSON parse failed: %s\n",
        err.c_str()
      );
    }

  } else {

    LOG_ERROR_PRINTF(
      "[Error]: Token fetch failed HTTP %d\n",
      httpCode
    );


    LOG_DEBUG_PRINTLN(
      https.getString()
    );
  }


  https.end();


  return newToken;
}


// ============================================================
// CALENDAR GET EVENTS
// ============================================================

String getUpcomingCalendarEvents(
  int maxResults = 5
) {

  String token =
    getValidGoogleAccessToken();


  if (
    token.length() ==
    0
  ) {

    return
      "Authentication error: Unable to get access token.";
  }


  time_t now;


  time(
    &now
  );


  struct tm timeinfo;


  gmtime_r(
    &now,
    &timeinfo
  );


  char timeBuf[30];


  strftime(
    timeBuf,
    sizeof(timeBuf),
    "%Y-%m-%dT%H:%M:%SZ",
    &timeinfo
  );


  String url =
    "https://www.googleapis.com/calendar/v3/calendars/primary/events?timeMin=";


  url +=
    String(timeBuf);


  url +=
    "&maxResults=" +
    String(maxResults) +
    "&singleEvents=true&orderBy=startTime";


  WiFiClientSecure client;

  client.setInsecure();


  HTTPClient https;


  if (
    !https.begin(
      client,
      url
    )
  ) {

    return
      "Failed to connect to Calendar.";
  }


  https.addHeader(
    "Authorization",
    "Bearer " +
    token
  );


  int httpCode =
    https.GET();


  String result =
    "You have no upcoming events.";


  if (
    httpCode ==
    HTTP_CODE_OK
  ) {

    String response =
      https.getString();


    JsonDocument doc;


    DeserializationError err =
      deserializeJson(
        doc,
        response
      );


    if (!err) {

      JsonArray items =
        doc["items"]
        .as<JsonArray>();


      if (
        items.size() >
        0
      ) {

        result =
          "Upcoming events:\n";


        for (
          JsonObject item :
          items
        ) {

          const char* summary =
            item["summary"] |
            "Untitled Event";


          const char* start =
            item["start"]["dateTime"] |
            item["start"]["date"] |
            "";


          result +=
            "- " +
            String(summary) +
            " (Starts: " +
            String(start) +
            ")\n";
        }
      }
    }

  } else {

    result =
      "Failed to fetch calendar events (HTTP " +
      String(httpCode) +
      ").";
  }


  https.end();


  return result;
}


// ============================================================
// CREATE CALENDAR EVENT
// ============================================================

String createCalendarEvent(
  String summary,
  String startTimeIso,
  String endTimeIso = ""
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


  if (
    startTimeIso.length() ==
    0
  ) {

    return
      "Event start time is missing.";
  }


  if (
    endTimeIso.length() ==
    0
  ) {

    // Keeping your previous behavior.
    // Ideally calculate +1 hour.
    endTimeIso =
      startTimeIso;
  }


  WiFiClientSecure client;

  client.setInsecure();


  HTTPClient https;


  if (
    !https.begin(
      client,
      "https://www.googleapis.com/calendar/v3/calendars/primary/events"
    )
  ) {

    return
      "Unable to connect to Google Calendar.";
  }


  https.addHeader(
    "Authorization",
    "Bearer " +
    token
  );


  https.addHeader(
    "Content-Type",
    "application/json"
  );


  JsonDocument doc;


  doc["summary"] =
    summary;


  doc["start"]["dateTime"] =
    startTimeIso;


  doc["end"]["dateTime"] =
    endTimeIso;


  String body;


  serializeJson(
    doc,
    body
  );


  int httpCode =
    https.POST(
      body
    );


  String result;


  if (
    httpCode ==
      HTTP_CODE_OK
    ||
    httpCode ==
      201
  ) {

    result =
      "Event '" +
      summary +
      "' successfully created.";

  } else {

    result =
      "Failed to create event (HTTP " +
      String(httpCode) +
      ").";


    LOG_DEBUG_PRINTLN(
      https.getString()
    );
  }


  https.end();


  return result;
}


// ============================================================
// GMAIL GET UNREAD
// ============================================================

String getUnreadEmails(
  int maxResults = 3
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


  WiFiClientSecure client;

  client.setInsecure();


  HTTPClient https;


  String url =
    "https://gmail.googleapis.com/gmail/v1/users/me/messages?q=is:unread&maxResults=" +
    String(maxResults);


  if (
    !https.begin(
      client,
      url
    )
  ) {

    return
      "Failed to connect to Gmail.";
  }


  https.addHeader(
    "Authorization",
    "Bearer " +
    token
  );


  int httpCode =
    https.GET();


  String result =
    "You have no unread emails.";


  if (
    httpCode ==
    HTTP_CODE_OK
  ) {

    String response =
      https.getString();


    JsonDocument doc;


    DeserializationError err =
      deserializeJson(
        doc,
        response
      );


    if (!err) {

      JsonArray messages =
        doc["messages"]
        .as<JsonArray>();


      if (
        messages.size() >
        0
      ) {

        result =
          "Unread emails:\n";


        for (
          JsonObject msg :
          messages
        ) {

          String msgId =
            String(
              msg["id"] |
              ""
            );


          https.end();


          String detailUrl =
            "https://gmail.googleapis.com/gmail/v1/users/me/messages/" +
            msgId +
            "?format=metadata&metadataHeaders=From&metadataHeaders=Subject";


          if (
            !https.begin(
              client,
              detailUrl
            )
          ) {

            continue;
          }


          https.addHeader(
            "Authorization",
            "Bearer " +
            token
          );


          int detailCode =
            https.GET();


          if (
            detailCode ==
            HTTP_CODE_OK
          ) {

            JsonDocument msgDoc;


            DeserializationError msgErr =
              deserializeJson(
                msgDoc,
                https.getString()
              );


            if (!msgErr) {

              String sender =
                "Unknown";


              String subject =
                "No Subject";


              JsonArray headers =
                msgDoc["payload"]["headers"]
                .as<JsonArray>();


              for (
                JsonObject header :
                headers
              ) {

                String name =
                  String(
                    header["name"] |
                    ""
                  );


                if (
                  name.equalsIgnoreCase(
                    "From"
                  )
                ) {

                  sender =
                    String(
                      header["value"] |
                      "Unknown"
                    );
                }


                if (
                  name.equalsIgnoreCase(
                    "Subject"
                  )
                ) {

                  subject =
                    String(
                      header["value"] |
                      "No Subject"
                    );
                }
              }


              result +=
                "- From: " +
                sender +
                " | Subject: " +
                subject +
                "\n";
            }
          }


          https.end();
        }
      }
    }

  } else {

    result =
      "Failed to fetch emails (HTTP " +
      String(httpCode) +
      ").";
  }


  https.end();


  return result;
}


// ============================================================
// GMAIL SEND
// ============================================================

String sendEmail(
  String toAddress,
  String subject,
  String bodyContent
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


  String mimeMsg =
    "To: " +
    toAddress +
    "\r\n" +

    "Subject: " +
    subject +
    "\r\n" +

    "Content-Type: text/plain; charset=\"UTF-8\"\r\n"
    "\r\n" +

    bodyContent;


  size_t outLen =
    0;


  char* b64 =
    base64Encode(
      (const uint8_t*)
        mimeMsg.c_str(),
      mimeMsg.length(),
      &outLen
    );


  if (!b64) {

    return
      "Error encoding email.";
  }


  String encodedMsg =
    String(b64);


  free(b64);


  encodedMsg.replace(
    "+",
    "-"
  );


  encodedMsg.replace(
    "/",
    "_"
  );


  encodedMsg.replace(
    "=",
    ""
  );


  WiFiClientSecure client;

  client.setInsecure();


  HTTPClient https;


  if (
    !https.begin(
      client,
      "https://gmail.googleapis.com/gmail/v1/users/me/messages/send"
    )
  ) {

    return
      "Unable to connect to Gmail.";
  }


  https.addHeader(
    "Authorization",
    "Bearer " +
    token
  );


  https.addHeader(
    "Content-Type",
    "application/json"
  );


  JsonDocument doc;


  doc["raw"] =
    encodedMsg;


  String payload;


  serializeJson(
    doc,
    payload
  );


  int httpCode =
    https.POST(
      payload
    );


  String result;


  if (
    httpCode ==
      HTTP_CODE_OK
    ||
    httpCode ==
      201
  ) {

    result =
      "Email successfully sent to " +
      toAddress;

  } else {

    result =
      "Failed to send email (HTTP " +
      String(httpCode) +
      ").";


    LOG_ERROR_PRINTLN(
      "[Error]: Gmail send failed."
    );


    LOG_DEBUG_PRINTLN(
      https.getString()
    );
  }


  https.end();


  return result;
}
