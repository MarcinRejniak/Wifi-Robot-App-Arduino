#include <ArduinoJson.h> 
#include "WiFiS3.h" 
#include "arduino_secrets.h" 

char ssid[] = SECRET_SSID; 
char pass[] = SECRET_PASS;

String direction = "forward"; 

int lastSpeed = 0; 
int status = WL_IDLE_STATUS; 

WiFiServer server(80); 

const int DIR1 = 8; 
const int DIR2 = 10; 
const int PWM1 = 9; 
const int PWM2 = 11; 
const int TRIG_A = 3; 
const int TRIG_B = 5; 
const int ECHO_A = 4; 
const int ECHO_B = 6; 

bool obstacleDetected = false;

void setup() { 

  Serial.begin(9600); 
  Serial.println("--- System Initialization Started ---"); 

  initHardware(); 
  stopMotors();

  if (!setupWiFi()) {
    Serial.println("CRITICAL ERROR: System Halted.");
    while (true);
  }

  server.begin(); 
  printWiFiStatus(); 
  Serial.println("--- System Ready: Awaiting Commands ---");
} 

void loop() { 

  checkWiFiStatus();

  float distA = measurement_A();
  float distB = measurement_B();

  handleObstacleAvoidance(distA, distB);

  Serial.println("distA"); 
  Serial.println(distA); 
  Serial.println("distB"); 
  Serial.println(distB); 

  WiFiClient client = server.available(); 

  if (client) { 

    processNetworkRequest(client);

  } 
} 

void initHardware() { 

  pinMode(DIR1, OUTPUT); 
  pinMode(DIR2, OUTPUT); 
  pinMode(PWM1, OUTPUT); 
  pinMode(PWM2, OUTPUT); 

  pinMode(TRIG_A,OUTPUT); 
  pinMode(TRIG_B,OUTPUT); 
  pinMode(ECHO_A,INPUT); 
  pinMode(ECHO_B,INPUT); 

  Serial.println("Hardware pins initialized");
}

void stopMotors() {

  digitalWrite(DIR1, LOW);
  digitalWrite(DIR2, HIGH);

  analogWrite(PWM1, 0);
  analogWrite(PWM2, 0);

  Serial.println("Motors stopped (safe state).");
}

bool setupWiFi() {
  
  if (WiFi.status() == WL_NO_MODULE) { 

    Serial.println("WiFi module not found!"); 
    return false;
  } 

  WiFi.config(IPAddress(192, 48, 56, 2)); 
  Serial.print("Starting AP: "); 
  Serial.println(ssid); 

  status = WiFi.beginAP(ssid, pass); 
  if (status != WL_AP_LISTENING) { 

    Serial.println("AP failed to reach LISTENING state."); 

    return false;
  } 

  delay(10000);
  return true;
}

void checkWiFiStatus() {

  if (status != WiFi.status()) { 

    status = WiFi.status(); 
    Serial.println(status == WL_AP_CONNECTED ? "Device connected" : "Device disconnected"); 

  } 
}

void handleObstacleAvoidance(float distA, float distB) {

  if (distA < 17.00) {

    obstacleDetected = true;
    driveReverse();

  } else if (distB < 20.00) {

    obstacleDetected = true;
    driveForward();
    
  } else if (obstacleDetected && distA > 27.00 && distB > 30.00) {

    obstacleDetected = false;
    stopMotors();

  }
}

void driveForward() {

  digitalWrite(DIR1, LOW);
  digitalWrite(DIR2, HIGH);

  int speed = (lastSpeed > 0) ? lastSpeed : 150;

  applySpeed(speed);

  direction = "forward";
  Serial.println("Action: Driving Forward");
}

void driveReverse() {
  
  digitalWrite(DIR1, HIGH);
  digitalWrite(DIR2, LOW);

  int speed = (lastSpeed > 0) ? lastSpeed : 150;

  applySpeed(speed);

  direction = "back";
  Serial.println("Action: Driving Reverse (Obstacle Avoidance)");
}

void applySpeed(int speed) {
  analogWrite(PWM1, speed);
  analogWrite(PWM2, speed);
}

void processNetworkRequest(WiFiClient &client) {

  Serial.println("New client connected");  

  String request = readRequest(client);

  handleRequestRouting(client, request);

  client.stop(); 
  Serial.println("Client disconnected"); 
}

String readRequest(WiFiClient &client) {

  String req = "";
  unsigned long timeout = millis() + 500;

  while (client.connected() && millis() < timeout) {

    while (client.available()) {

      req += (char)client.read();
      timeout = millis() + 500; 
    }
  }
  
  return req;
}

void handleRequestRouting(WiFiClient &client, String &request) {

  if (request.startsWith("POST")) { 
      handlePost(client, request); 

    } else { 

      sendErrorResponse(client, "Invalid Request Method"); 

    }
}

void handlePost(WiFiClient &client, String &request) { 

  if (!request.startsWith("POST") || request.indexOf("Content-Type: application/json") == -1) { 

    sendErrorResponse(client, "Unsupported method or Content-Type"); 

    return; 
  } 

  int jsonStart = request.indexOf("\r\n\r\n") + 4; 

  if (jsonStart < 4 || jsonStart >= request.length()) { 

    sendErrorResponse(client, "No JSON found"); 

    return; 
  } 

  String json = request.substring(jsonStart); 

  StaticJsonDocument<256> doc; 

  if (deserializeJson(doc, json)) { 

    sendErrorResponse(client, "Invalid JSON"); 

    return; 
  } 

  processCommands(client, doc);
} 

void sendJsonResponse(WiFiClient &client, const String &message) { 

  StaticJsonDocument<200> response; 

  response["status"] = "ok"; 
  response["message"] = message; 
  response["direction"] = direction; 

  String responseJson; 

  serializeJson(response, responseJson); 

  client.println("HTTP/1.1 200 OK"); 
  client.println("Content-Type: application/json"); 
  client.println("Connection: close"); 
  client.println(); 
  client.println(responseJson); 

  Serial.println("Response sent:"); 
  Serial.println(responseJson); 

} 

void sendErrorResponse(WiFiClient &client, const String &message) { 

  StaticJsonDocument<200> response; 

  response["status"] = "error"; 
  response["message"] = message; 
  response["direction"] = direction; 

  String responseJson; 

  serializeJson(response, responseJson); 

  client.println("HTTP/1.1 400 Bad Request"); 
  client.println("Content-Type: application/json"); 
  client.println("Connection: close"); 
  client.println(); 
  client.println(responseJson); 

  Serial.println("Error response sent:"); 
  Serial.println(responseJson); 

} 

void printWiFiStatus() { 

  Serial.print("SSID: "); 
  Serial.println(WiFi.SSID()); 
  Serial.print("IP Address: "); 
  Serial.println(WiFi.localIP()); 

} 

float measurement_A(){ 

  unsigned long time; 

  digitalWrite(TRIG_A, HIGH); 

  delayMicroseconds(10); 

  digitalWrite(TRIG_A, LOW); 

  time = pulseIn(ECHO_A, HIGH, 30000);

  if (time == 0) { 
    return 400.00;
  } 

  return time / 58.00; 

} 

float measurement_B(){ 

  unsigned long time; 

  digitalWrite(TRIG_B, HIGH); 

  delayMicroseconds(10); 

  digitalWrite(TRIG_B, LOW); 

  time = pulseIn(ECHO_B, HIGH, 30000);

  if (time == 0) {
    return 400.00;
  } 

  return time / 58.00; 

} 

void processCommands(WiFiClient &client, StaticJsonDocument<256> &doc) {

  if (doc.containsKey("side")) { 

    String side = doc["side"]; 

    if (side == "left") { 

      if(direction == "forward"){ 

        digitalWrite(DIR1,HIGH); 

      } else if(direction == "back"){ 

        digitalWrite(DIR1, LOW); 

      } 

      sendJsonResponse(client, "side left"); 

    } else if (side == "right") { 

      if(direction == "forward"){ 

        digitalWrite(DIR2,LOW); 

      } else if(direction == "back"){ 

        digitalWrite(DIR2, HIGH); 

      } 

      sendJsonResponse(client, "side right"); 

    } else if (side == "normal") { 

      analogWrite(PWM1,lastSpeed); 
      analogWrite(PWM2,lastSpeed); 

      if(direction == "forward"){ 

        digitalWrite(DIR1,LOW); 
        digitalWrite(DIR2,HIGH); 

      } else if(direction == "back"){ 

        digitalWrite(DIR1,HIGH); 
        digitalWrite(DIR2,LOW); 

      } 

      sendJsonResponse(client, "side normal"); 

    } else { 

      sendErrorResponse(client, "Invalid side value"); 

    } 

  } else if (doc.containsKey("direction")) { 

    String newDirection = doc["direction"]; 

    if (newDirection == "forward") { 

      digitalWrite(DIR1,LOW); 
      digitalWrite(DIR2,HIGH); 

      direction = newDirection; 

      sendJsonResponse(client, "Direction changed to " + direction); 

    } else if (newDirection == "back"){ 

      digitalWrite(DIR1,HIGH); 
      digitalWrite(DIR2,LOW); 

      direction = newDirection; 

      sendJsonResponse(client, "Direction changed to " + direction); 

    } else { 

      sendErrorResponse(client, "Invalid direction value"); 

    } 

  } else if (doc.containsKey("speed")) { 

    int speed = doc["speed"]; 

    analogWrite(PWM1,speed); 
    analogWrite(PWM2,speed); 

    lastSpeed = speed; 

    Serial.print("Speed received: "); 
    Serial.println(speed); 

    sendJsonResponse(client, "Speed set to " + String(speed)); 

  } else { 

    sendErrorResponse(client, "Unknown command in JSON"); 

  } 
}