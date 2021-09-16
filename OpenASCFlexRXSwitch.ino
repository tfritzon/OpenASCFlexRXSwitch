#include <stdio.h>
#include <Esp.h>
#include <EEPROM.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>


const char * ssid = "SK0UX-Teknik";
const char * pwd = "RX42YM4L3Q9JW8VM";

WebServer server(80);

//const char * ssid = "lommen";
//const char * pwd = "d6nw5v1x2pc7st9m";

const char * host = "10.1.1.15";
//const char * host = "192.168.0.42";
const int port = 4992;
const int LED_BUILTIN = 2;
int rxant = 0;
WiFiClient client;

const int ENABLE_PIN = 15;

const int NR_PINS = 11;
const int dir_pins[] = { 23, 22, 32, 21, 19, 18, 5, 17, 16, 33, 25 };
const int default_gains[] = { 24, 24, 24, 24, 24, 32, 24, 24, 24, 24, 24 };
int gains[NR_PINS];
int dir = -1;
int tx_gain;
String my_id;
char buf[2048];
char tx_ant[8];

String clientidpat;
String txginapat;
String rfgainpat;

const char* loginIndex = 
 "<form name='loginForm'>"
    "<table width='20%' bgcolor='A09F9F' align='center'>"
        "<tr>"
            "<td colspan=2>"
                "<center><font size=4><b>ESP32 Login Page</b></font></center>"
                "<br>"
            "</td>"
            "<br>"
            "<br>"
        "</tr>"
        "<td>Username:</td>"
        "<td><input type='text' size=25 name='userid'><br></td>"
        "</tr>"
        "<br>"
        "<br>"
        "<tr>"
            "<td>Password:</td>"
            "<td><input type='Password' size=25 name='pwd'><br></td>"
            "<br>"
            "<br>"
        "</tr>"
        "<tr>"
            "<td><input type='submit' onclick='check(this.form)' value='Login'></td>"
        "</tr>"
    "</table>"
"</form>"
"<script>"
    "function check(form)"
    "{"
    "if(form.userid.value=='admin' && form.pwd.value=='admin')"
    "{"
    "window.open('/serverIndex')"
    "}"
    "else"
    "{"
    " alert('Error Password or Username')/*displays error message*/"
    "}"
    "}"
"</script>";

const char* serverIndex = 
"<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
"<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
   "<input type='file' name='update'>"
        "<input type='submit' value='Update'>"
    "</form>"
 "<div id='prg'>progress: 0%</div>"
 "<script>"
  "$('form').submit(function(e){"
  "e.preventDefault();"
  "var form = $('#upload_form')[0];"
  "var data = new FormData(form);"
  " $.ajax({"
  "url: '/update',"
  "type: 'POST',"
  "data: data,"
  "contentType: false,"
  "processData:false,"
  "xhr: function() {"
  "var xhr = new window.XMLHttpRequest();"
  "xhr.upload.addEventListener('progress', function(evt) {"
  "if (evt.lengthComputable) {"
  "var per = evt.loaded / evt.total;"
  "$('#prg').html('progress: ' + Math.round(per*100) + '%');"
  "}"
  "}, false);"
  "return xhr;"
  "},"
  "success:function(d, s) {"
  "console.log('success!')" 
 "},"
 "error: function (a, b, c) {"
 "}"
 "});"
 "});"
 "</script>";


void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(ENABLE_PIN, INPUT_PULLUP);

  for( int i= 0; i < NR_PINS; i++ ) {
    pinMode(dir_pins[i], INPUT_PULLUP);
  }

  Serial.begin(115200);

  wifiConnect();
  
  Serial.println("Arduino WebOTA booting");
  /*use mdns for host name resolution*/
  if (!MDNS.begin(host)) { //http://esp32.local
    Serial.println("Error setting up MDNS responder!");
    while (1) {
      delay(1000);
    }
  }
  Serial.println("mDNS responder started");
  /*return index page which is stored in serverIndex */
  server.on("/", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", loginIndex);
  });
  server.on("/serverIndex", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", serverIndex);
  });
  server.on("/reset", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", "OK");
    ESP.restart();
  });
  /*handling uploading firmware file */
  server.on("/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("Update: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      /* flashing firmware to ESP*/
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { //true to set the size to the current progress
        Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
    }
  });
  server.begin();  
  
  Serial.println("RX Switch starting...");
  loadGains();
  dir = get_dir();
  catConnect();
  loadRadio();
}

void loop() {
  int ndir= -1;

  server.handleClient();

  catRead();

  if( get_dir() != dir ) {
    Serial.println("BINGO!"); 
    dir= get_dir();
    Serial.println(dir);
    if( dir >= 0 && rxant ) {
      Serial.printf("Got pin %d, setting gain to: %d\n", dir_pins[dir], gains[dir] );
      catWrite("C43|display pan s 0x40000000 rfgain %d\r", gains[dir]);
      Serial.printf("C43|display pan s 0x40000000 rfgain %d\r", gains[dir]);
      catRead();
    }
  }

  if( digitalRead(ENABLE_PIN) == LOW ) {
    if( rxant == 0 ) {
      Serial.println("Switch to RX_A, dir=" + dir);
      catWrite("C42|slice s 0 rxant=RX_A\r");
      catRead();
      if( (ndir = get_dir()) >= 0 ) {
        catWrite("C43|display pan s 0x40000000 rfgain %d\r", gains[ndir]);
      } else {
        catWrite("C43|display pan s 0x40000000 rfgain %d\r", tx_gain);
      }
      catRead();
      rxant= 1;
      digitalWrite(LED_BUILTIN, HIGH);
    }
  } else {
    if( rxant == 1 ) {
      Serial.println("Switch to ANT1");
      catWrite("C42|slice s 0 rxant=ANT1\r");
      catRead();
      catWrite("C43|display pan s 0x40000000 rfgain %d\r", tx_gain);
      rxant= 0;
      digitalWrite(LED_BUILTIN, 0);
    }
  }

  delay(100);
}

void loadGains() {
  EEPROM.begin(16);

  for( int i= 0; i < NR_PINS; i++ ) {
    gains[i]= EEPROM.read(i);
    if( gains[i] > 32 ) {
      gains[i] = default_gains[i];
    }
    Serial.print(gains[i]);
    Serial.print(" ");
  }
  Serial.println("Gains loded.");
}

void saveGains() {
  EEPROM.begin(16);

  for( int i= 0; i < NR_PINS; i++ ) {
    EEPROM.write(i, gains[i]);
    Serial.print(gains[i]);
    Serial.print(" ");
  }
  EEPROM.commit();
  Serial.println("Gains saved.");
}

void wifiConnect() {
  int ledState = 0;
  
  Serial.println("Connecting to " + String(ssid));
  WiFi.begin(ssid, pwd);

  while( WiFi.status() != WL_CONNECTED ) {
    digitalWrite(LED_BUILTIN, ledState);
    ledState = ledState==0?1:0;
    Serial.print(".");
    delay(500);
  }

  Serial.println();
  Serial.println("Wifi connected");
  Serial.println(WiFi.localIP());
}

void catConnect() {
  if( !client.connect(host, port) ) {
    Serial.println("connection failed");
    return;
  }

  catWrite("C99|sub pan all\r");
}

void catRead() {
  while( client.available() ) {
    String l = client.readStringUntil('\n');
    Serial.println("> " + l);

    if( l.startsWith("H") ) {
      my_id = l.substring(1);
      Serial.println("Connected to flex with ID: " + my_id);
    }

    if( l.indexOf("rfgain=") > 0 ) {

      if( l.indexOf("rxant=ANT") > l.indexOf("rfgain=") ) {
        tx_gain = l.substring(l.indexOf("rfgain=")+7, l.indexOf("rxant=")-1).toInt();
      }

      if( !rxant ) {
        tx_gain = l.substring(l.indexOf("rfgain=")+7, l.indexOf("pre=")-1).toInt();
      }

      if( rxant ) {
        if( l.indexOf(my_id) < 0 ) {
          if( l.indexOf("|display pan 0x40000000 min_dbm=") > 2 ) {
            gains[dir] = l.substring(l.indexOf("rfgain=")+7, l.indexOf("pre=")-1).toInt();
            saveGains();
          }
        }
      }
    }
    delay(10);
  }
}

void catWrite(const char * format, ...) {
  char buff[2048];
  va_list valist;

  va_start(valist, format);

  vsprintf(buff, format, valist);
  while( client.println(buff) <= 0 ) {
    delay(100);
    Serial.println("Lost connection, reconnecting");
    catConnect();
  }
  Serial.print("Sent: ");
  Serial.println(buff);
}

void loadRadio() {
  catWrite("C1|sub slice all\r");
  String s = String(my_id);
  s.concat(String("|slice 0 in_use=1"));
  s = String(buf);
  tx_gain = s.substring(s.indexOf("rfgain=") + 7, 2).toInt();
}

int get_dir() {
  for( int i= 0; i < NR_PINS; i++ ) {
    if( digitalRead(dir_pins[i]) == LOW ) {
      return i;
      break;
    }
  }
  return -1;
}
