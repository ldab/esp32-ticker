#include <Arduino.h>

#include "https_request.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

// openssl s_client -showcerts -connect alpha-vantage.p.rapidapi.com:443
// NotAfter: Jun 28 17:39:16 2034 GMT
const char *rootCACertificate =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIEdTCCA12gAwIBAgIJAKcOSkw0grd/MA0GCSqGSIb3DQEBCwUAMGgxCzAJBgNV\n"
    "BAYTAlVTMSUwIwYDVQQKExxTdGFyZmllbGQgVGVjaG5vbG9naWVzLCBJbmMuMTIw\n"
    "MAYDVQQLEylTdGFyZmllbGQgQ2xhc3MgMiBDZXJ0aWZpY2F0aW9uIEF1dGhvcml0\n"
    "eTAeFw0wOTA5MDIwMDAwMDBaFw0zNDA2MjgxNzM5MTZaMIGYMQswCQYDVQQGEwJV\n"
    "UzEQMA4GA1UECBMHQXJpem9uYTETMBEGA1UEBxMKU2NvdHRzZGFsZTElMCMGA1UE\n"
    "ChMcU3RhcmZpZWxkIFRlY2hub2xvZ2llcywgSW5jLjE7MDkGA1UEAxMyU3RhcmZp\n"
    "ZWxkIFNlcnZpY2VzIFJvb3QgQ2VydGlmaWNhdGUgQXV0aG9yaXR5IC0gRzIwggEi\n"
    "MA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDVDDrEKvlO4vW+GZdfjohTsR8/\n"
    "y8+fIBNtKTrID30892t2OGPZNmCom15cAICyL1l/9of5JUOG52kbUpqQ4XHj2C0N\n"
    "Tm/2yEnZtvMaVq4rtnQU68/7JuMauh2WLmo7WJSJR1b/JaCTcFOD2oR0FMNnngRo\n"
    "Ot+OQFodSk7PQ5E751bWAHDLUu57fa4657wx+UX2wmDPE1kCK4DMNEffud6QZW0C\n"
    "zyyRpqbn3oUYSXxmTqM6bam17jQuug0DuDPfR+uxa40l2ZvOgdFFRjKWcIfeAg5J\n"
    "Q4W2bHO7ZOphQazJ1FTfhy/HIrImzJ9ZVGif/L4qL8RVHHVAYBeFAlU5i38FAgMB\n"
    "AAGjgfAwge0wDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMCAYYwHQYDVR0O\n"
    "BBYEFJxfAN+qAdcwKziIorhtSpzyEZGDMB8GA1UdIwQYMBaAFL9ft9HO3R+G9FtV\n"
    "rNzXEMIOqYjnME8GCCsGAQUFBwEBBEMwQTAcBggrBgEFBQcwAYYQaHR0cDovL28u\n"
    "c3MyLnVzLzAhBggrBgEFBQcwAoYVaHR0cDovL3guc3MyLnVzL3guY2VyMCYGA1Ud\n"
    "HwQfMB0wG6AZoBeGFWh0dHA6Ly9zLnNzMi51cy9yLmNybDARBgNVHSAECjAIMAYG\n"
    "BFUdIAAwDQYJKoZIhvcNAQELBQADggEBACMd44pXyn3pF3lM8R5V/cxTbj5HD9/G\n"
    "VfKyBDbtgB9TxF00KGu+x1X8Z+rLP3+QsjPNG1gQggL4+C/1E2DUBc7xgQjB3ad1\n"
    "l08YuW3e95ORCLp+QCztweq7dp4zBncdDQh/U90bZKuCJ/Fp1U1ervShw3WnWEQt\n"
    "8jxwmKy6abaVd38PMV4s/KCHOkdp8Hlf9BRUpJVeEXgSYCfOn8J3/yNTd126/+pZ\n"
    "59vPr5KW7ySaNRB6nJHGDn2Z9j8Z3/VyVOEVqQdZe4O/Ui5GjLIAZHYcSNPYeehu\n"
    "VsyuLAOQ1xk4meTKCRlb/weWsKh/NEnfVqn3sF/tM+2MR7cwA130A4w=\n"
    "-----END CERTIFICATE-----\n";

static void setClock()
{
  log_i("Waiting for NTP time sync: ");
  time_t nowSecs = time(nullptr);

  while (nowSecs < 8 * 3600 * 2) {
    delay(500);
    log_i(".");
    yield();
    nowSecs = time(nullptr);
  }

  struct tm timeinfo;
  gmtime_r(&nowSecs, &timeinfo);
  log_i("Current time: %s", asctime(&timeinfo));
}

static float parse_response(const char *response)
{
  // {
  //   "Global Quote": {
  //       "01. symbol": "0QF.F",
  //       "02. open": "99.3400",
  //       "03. high": "105.0800",
  //       "04. low": "99.0000",
  //       "05. price": "105.0800",
  //       "06. volume": "360",
  //       "07. latest trading day": "2024-01-05",
  //       "08. previous close": "99.1200",
  //       "09. change": "5.9600",
  //       "10. change percent": "6.0129%"
  //   }
  // }

  float price = 0;

  if (response != NULL) {
    char *price_json = strstr(response, "\"05. price\": \"");

    if (price_json != NULL) {
      log_d("price_json %s", price_json);
      price = atof(price_json + strlen("\"05. price\": \""));
      log_d("Price %f", price);
    }
  }

  return price;
}

void https_request_init()
{
  setClock(); //
}

esp_err_t https_request_get_symbol_quote(char *symbol, float *quote)
{
  WiFiClientSecure *client = new WiFiClientSecure;
  if (client) {
    client->setCACert(rootCACertificate);
    {
      HTTPClient https;

      char request_url[] =
          "https://alpha-vantage.p.rapidapi.com/"
          "query?function=GLOBAL_QUOTE&symbol=SYMBOLGOESHERE&datatype=json";

      memset(request_url, 0, sizeof(request_url));

      sprintf(request_url,
              "https://alpha-vantage.p.rapidapi.com/"
              "query?function=GLOBAL_QUOTE&symbol=%s&datatype=json",
              symbol);

      if (https.begin(*client, request_url)) {

        https.addHeader("X-Rapidapi-Key", API_TOKEN);

        // start connection and send HTTP header
        int httpCode = https.GET();

        // httpCode will be negative on error
        if (httpCode > 0) {
          // HTTP header has been send and Server response header has been
          // handled
          log_i("[HTTPS] GET... code: %d", httpCode);

          // file found at server
          if (httpCode == HTTP_CODE_OK ||
              httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
            String payload = https.getString();
            log_d("%s", payload.c_str());
            *quote = parse_response(payload.c_str());
          }
        } else {
          log_i("[HTTPS] GET... failed, error: %s",
                https.errorToString(httpCode).c_str());
          return ESP_FAIL;
        }

        https.end();
      } else {
        log_e("[HTTPS] Unable to connect");
        return ESP_FAIL;
      }
    }

    delete client;

  } else {
    log_e("Unable to create client");
    return ESP_FAIL;
  }

  return ESP_OK;
}
