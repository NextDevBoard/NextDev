#include <FS.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>    
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// Callback function definitions
void callback(char* topic, byte* payload, unsigned int length);
void configModeCallback(WiFiManager *myWiFiManager);
void saveConfigCallback();

//const char* password = "ea3d3fa7adeada";
char mqtt_server[40];
char realm[34];
char label[50];
char mServer[40];

bool shouldSaveConfig = false;

WiFiManager wifiManager;
WiFiClient espClient;
PubSubClient client(espClient);


void setup() {
    Serial.begin(115200);

    pinMode(16, OUTPUT);    // Relay 1
    pinMode(14, OUTPUT);    // Relay 2
    pinMode(12, OUTPUT);    // Relay 3
    pinMode(13, OUTPUT);    // Relay 4
    digitalWrite(16, LOW);
    digitalWrite(14, LOW);
    digitalWrite(12, LOW);
    digitalWrite(13, LOW);

    //clean FS, for testing
    //SPIFFS.format();
    if (SPIFFS.begin()) {
        Serial.println("Mounted FS.");
        loadConfig();
    }
    else {
        Serial.println("Failed to mount FS!");
    }

    wifiManager.setAPCallback(configModeCallback);
    wifiManager.setSaveConfigCallback(saveConfigCallback);

    //wifiManager.resetSettings();

    // The extra parameters to be configured (can be either global or just in the setup)
    // After connecting, parameter.getValue() will get you the configured value
    // id/name placeholder/prompt default length
    WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
    WiFiManagerParameter custom_realm("realm", "station call", realm, 34);
    WiFiManagerParameter custom_label("label", "descriptive label", label, 50);

    wifiManager.addParameter(&custom_mqtt_server);
    wifiManager.addParameter(&custom_realm);
    wifiManager.addParameter(&custom_label);

    wifiManager.setConfigPortalTimeout(180);

    if (!wifiManager.autoConnect()) {
        Serial.println("failed to connect and hit timeout");
        delay(3000);
        //reset and try again, or maybe put it to deep sleep
        ESP.reset();
        delay(5000);
    }

    //if you get here you have connected to the WiFi
    Serial.println("Connected...yay :)");
  
    // read updated parameters
    strcpy(mqtt_server, custom_mqtt_server.getValue());
    strcpy(realm, custom_realm.getValue());
    strcpy(label, custom_label.getValue());

    if (shouldSaveConfig) {
        Serial.println("Saving config...");
        saveConfig();
    }

    Serial.print("Local ip = ");
    Serial.println(WiFi.localIP());
    Serial.print("MQTT Broker = ");
    Serial.println(mqtt_server);
    Serial.print("Realm = ");
    Serial.println(realm);

    String s = String(mqtt_server);
    int pos = s.indexOf(':');
    int port = 1883;
    if (pos >= 0) {
        String s2 = s.substring(0, pos);
        strcpy(mServer, s2.c_str());
        port = s.substring(pos + 1).toInt();
    }
    else {
        strcpy(mServer, mqtt_server);
    }
    Serial.print("Connecting to server ");
    Serial.println(mServer);
    Serial.print("Over port ");
    Serial.println(port);

    // Configure connection to MQTT broker.
    client.setServer(mServer, port);
    client.setCallback(callback);
}


void reconnect() {
    // Loop until we're reconnected
    String clientId = "ESP";
    clientId.concat(ESP.getChipId());
    while (!client.connected()) {
        Serial.print("Attempting MQTT connection...");
        // Attempt to connect
        if (client.connect(clientId.c_str())) {
            Serial.println("connected");
            // ... and resubscribe
        } else {
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again in 5 seconds");
            // Wait 5 seconds before retrying
            wifiManager.resetSettings();
            delay(5000);
            ESP.reset();
        }
    }
    String basePath = "/";
    basePath.concat(realm);
    publishInfo(basePath + "/Announce");   
    basePath.concat("/ESP");
    basePath.concat(ESP.getChipId());
    basePath.concat("/Cmds");
    String s = basePath;
    s.concat("/#");
    Serial.print("Subscribing to ");
    Serial.println(s);
    client.subscribe(s.c_str());
    String s2 = "/" + String(realm) + "/Discover";
    Serial.print("Subscribing to ");
    Serial.println(s2);
    client.subscribe(s2.c_str());
}


void loop() {

    if (!client.connected()) {
        reconnect();
    }
    client.loop();
}


void callback(char* topic, byte* payload, unsigned int length) {

    String basePath = "/";
    basePath.concat(realm);
    basePath.concat("/ESP");
    basePath.concat(ESP.getChipId());
    basePath.concat("/Status");

    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    String topicStr = String(topic);
    String buf = "";
    for (int i = 0; i < length; i++) {
        Serial.print((char)payload[i]);
        buf.concat((char) payload[i]);
    }
    Serial.println();

    if (topicStr.endsWith("Discover")) {
        publishInfo("/" + String(realm) + "/Announce");
        return;
    }

    if (topicStr.endsWith("Cmds")) {
        if (buf == "\"getInfo\"") {
            publishInfo(basePath);
            return;
        }
        else if (buf == "\"reset\"") {
            wifiManager.resetSettings();
            client.publish(basePath.c_str(), "\"reset\"");
            delay(1000);
            ESP.reset();
            return;
        }
        else if (buf == "\"restart\"") {
            client.publish(basePath.c_str(), "\"restart\"");
            delay(1000);
            ESP.reset();
        }
        else if (buf == "\"allOff\"") {
            setRelaysFromHex('0');
            client.publish(basePath.c_str(), "\"allOff\"");
            return;         
        }
        else if (buf == "\"reqStatus\"") {
            buf = "\"" + getRelaysAsHex() + "\"";
            client.publish(basePath.c_str(), buf.c_str());
            return;         
        }
        else if (buf.startsWith("\"set")) {
            int pos = buf.indexOf('x');
            if (pos == -1) return;
            setRelaysFromHex(buf.charAt(pos + 2));
            buf = "\"" + getRelaysAsHex() + "\"";
            client.publish(basePath.c_str(), buf.c_str());
            return;         
        }
        else {
            Serial.print("Unrecognized command ");
            Serial.println(buf);
            return;
        }
     
    }

    
    // Every command after this point is assumed to manipulate individual relays.

    if (topicStr.endsWith("/Cmds/Relay1")) {
        Serial.print("Relay1 " + buf);
        basePath.concat("/Relay1");
        if (buf == "\"on\"") {
            digitalWrite(16, HIGH);
        }
        else if (buf == "\"exclusiveOn\"") {
            digitalWrite(16, HIGH);
            digitalWrite(14, LOW);
            digitalWrite(12, LOW);
            digitalWrite(13, LOW);
        } 
        else if (buf == "\"reqStatus\"") {
            buf = digitalRead(16) == HIGH ? "\"on\"" : "\"off\"";
        }
        else if (buf == "\"toggleState\"") {
            digitalWrite(16, digitalRead(16) == LOW ? HIGH : LOW);
            buf = digitalRead(16) == HIGH ? "\"on\"" : "\"off\"";
        }
        else {
            digitalWrite(16, LOW);
        }
    }

    if (topicStr.endsWith("/Cmds/Relay2")) {
        Serial.print("Relay2 " + buf);
        basePath.concat("/Relay2");
        if (buf == "\"on\"") {
            digitalWrite(14, HIGH);
        }
        else if (buf == "\"exclusiveOn\"") {
            digitalWrite(14, HIGH);
            digitalWrite(16, LOW);
            digitalWrite(12, LOW);
            digitalWrite(13, LOW);
        } 
        else if (buf == "\"reqStatus\"") {
            buf = digitalRead(14) == HIGH ? "\"on\"" : "\"off\"";
        }
        else if (buf == "\"toggleState\"") {
            digitalWrite(14, digitalRead(14) == LOW ? HIGH : LOW);
            buf = digitalRead(14) == HIGH ? "\"on\"" : "\"off\"";
        }
        else {
            digitalWrite(14, LOW);
        }
    }

    if (topicStr.endsWith("/Cmds/Relay3")) {
        Serial.print("Relay3 " + buf);
        basePath.concat("/Relay3");
        if (buf == "\"on\"") {
            digitalWrite(12, HIGH);
        }
        else if (buf == "\"exclusiveOn\"") {
            digitalWrite(12, HIGH);
            digitalWrite(16, LOW);
            digitalWrite(14, LOW);
            digitalWrite(13, LOW);
        } 
        else if (buf == "\"reqStatus\"") {
            buf = digitalRead(12) == HIGH ? "\"on\"" : "\"off\"";
        }
        else if (buf == "\"toggleState\"") {
            digitalWrite(12, digitalRead(12) == LOW ? HIGH : LOW);
            buf = digitalRead(12) == HIGH ? "\"on\"" : "\"off\"";
        }
        else {
            digitalWrite(12, LOW);
        }
    }

    if (topicStr.endsWith("/Cmds/Relay4")) {
        Serial.print("Relay4 " + buf);
        basePath.concat("/Relay4");
        if (buf == "\"on\"") {
            digitalWrite(13, HIGH);
        }
        else if (buf == "\"exclusiveOn\"") {
            digitalWrite(13, HIGH);
            digitalWrite(16, LOW);
            digitalWrite(14, LOW);
            digitalWrite(12, LOW);
        } 
        else if (buf == "\"reqStatus\"") {
            buf = digitalRead(13) == HIGH ? "\"on\"" : "\"off\"";
        }
        else if (buf == "\"toggleState\"") {
            digitalWrite(13, digitalRead(13) == LOW ? HIGH : LOW);
            buf = digitalRead(13) == HIGH ? "\"on\"" : "\"off\"";
        }
        else {
            digitalWrite(13, LOW);
        }
    }

    client.publish(basePath.c_str(), buf.c_str());
}


void publishInfo(String topic) {
    String resp = "{ 'deviceId': '";
    resp.concat("ESP");
    resp.concat(ESP.getChipId());
    if (label != NULL && strlen(label) != 0) {
        resp.concat("', 'label': '");
        resp.concat(label);
    }
    resp.concat("', 'relays': '" + getRelaysAsHex());
    resp.concat("'}");
    Serial.print("Publishing message ");
    Serial.println(resp);
    Serial.print("To topic ");
    Serial.println(topic);
    client.publish(topic.c_str(), resp.c_str());
}


String getRelaysAsHex() {
     byte b = digitalRead(16) == HIGH ? 0x01 : 0x00;
     b <<= 1;
     b |= digitalRead(14) == HIGH ? 0x01 : 0x00;
     b <<= 1;
     b |= digitalRead(12) == HIGH ? 0x01 : 0x00;
     b <<= 1;
     b |= digitalRead(13) == HIGH ? 0x01 : 0x00;
     return "0x0" + String(b, HEX);
}

void setRelaysFromHex(byte b) {
   b = b >= 0x30 && b <= 0x39 ? b - 0x30 : b - 0x57;
   digitalWrite(16, (b & 0x08) != 0 ? HIGH : LOW);
   digitalWrite(14, (b & 0x04) != 0 ? HIGH : LOW);
   digitalWrite(12, (b & 0x02) != 0 ? HIGH : LOW);
   digitalWrite(13, (b & 0x01) != 0 ? HIGH : LOW);
}

void configModeCallback (WiFiManager *myWiFiManager) {

    Serial.println("Entered config mode");
    Serial.println(WiFi.softAPIP());
    Serial.println(myWiFiManager->getConfigPortalSSID());
}


void saveConfigCallback () {

    Serial.println("Save config callback invoked.");
    shouldSaveConfig = true;
}


void loadConfig() {
    if (SPIFFS.exists("/config.json")) {
        // file exists, reading and loading
        Serial.println("reading config file");
        File configFile = SPIFFS.open("/config.json", "r");
        if (configFile) {
            Serial.println("opened config file");
            size_t size = configFile.size();
            // Allocate a buffer to store contents of the file.
            std::unique_ptr<char[]> buf(new char[size]);
  
            configFile.readBytes(buf.get(), size);
            DynamicJsonBuffer jsonBuffer;
            JsonObject& json = jsonBuffer.parseObject(buf.get());
            json.printTo(Serial);
            if (json.success()) {
                Serial.println("\nparsed json");
                strcpy(mqtt_server, json["mqtt_server"]);
                strcpy(realm, json["realm"]);
                strcpy(label, json["label"]);
            } 
            else {
                Serial.println("failed to load json config");
            }
        }
    }
}


void saveConfig() {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqtt_server;
    json["realm"] = realm;
    json["label"] = label;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
        Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
}
