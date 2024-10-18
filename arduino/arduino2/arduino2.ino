#include <SPI.h>
#include <MFRC522.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESPAsyncWebServer.h>

#define RST_PIN         4          // Pino RST do RFID
#define SS_PIN          5          // Pino SS do RFID

MFRC522 mfrc522(SS_PIN, RST_PIN);  // Instância do módulo RFID
Servo myServo;                     // Instância do servo motor
int servoPin = 22;                 // Pino para o servo motor

// Pinos dos sensores
const int sensor1Pin = 34;
const int sensor2Pin = 35;

// Pinos dos LEDs RGB
#define PIN_RED1    32
#define PIN_GREEN1  33
#define PIN_BLUE1   25

#define PIN_RED2    14
#define PIN_GREEN2  12
#define PIN_BLUE2   13

// Credenciais Wi-Fi
const char* ssid = "Vieira";            // Substitua pelo seu SSID
const char* password = "202715hgv";       // Substitua pela sua senha

// Endereço IP do servidor Flask
const char* serverIP = "192.168.3.15";   // Substitua pelo IP do seu servidor Flask
const int serverPort = 5000;              // Porta do servidor Flask (padrão é 5000)

// Variáveis de estado
bool lastStateSensor1 = HIGH;
bool lastStateSensor2 = HIGH;
bool vaga1Reservada = false;
bool vaga2Reservada = false;

String vaga1Status = "vaga 1 livre";
String vaga2Status = "vaga 2 livre";

// Cria o servidor na porta 80
AsyncWebServer server(80);

// Função para alternar as cores dos LEDs RGB durante o tempo de catraca aberta
void intercalarLeds() {
  for (int i = 0; i < 10; i++) {
    // Acende vermelho
    setRGBColor(PIN_RED1, PIN_GREEN1, PIN_BLUE1, LOW, HIGH, HIGH);
    setRGBColor(PIN_RED2, PIN_GREEN2, PIN_BLUE2, LOW, HIGH, HIGH);
    delay(500);
    
    // Acende verde
    setRGBColor(PIN_RED1, PIN_GREEN1, PIN_BLUE1, HIGH, LOW, HIGH);
    setRGBColor(PIN_RED2, PIN_GREEN2, PIN_BLUE2, HIGH, LOW, HIGH);
    delay(500);
  }
}

void setup() {
  Serial.begin(115200);

  // Configuração dos pinos dos sensores e LEDs
  pinMode(sensor1Pin, INPUT);
  pinMode(sensor2Pin, INPUT);
  
  pinMode(PIN_RED1, OUTPUT);
  pinMode(PIN_GREEN1, OUTPUT);
  pinMode(PIN_BLUE1, OUTPUT);
  
  pinMode(PIN_RED2, OUTPUT);
  pinMode(PIN_GREEN2, OUTPUT);
  pinMode(PIN_BLUE2, OUTPUT);

  // Configurações do servo motor
  myServo.attach(servoPin);
  myServo.write(0); // Inicializa o servo na posição fechada (0 graus)

  // Conexão Wi-Fi
  Serial.print("Conectando-se ao Wi-Fi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nConectado ao Wi-Fi!");
  Serial.print("IP do Arduino: ");
  Serial.println(WiFi.localIP());

  // Rota da API que retorna o estado das vagas
  server.on("/vaga", HTTP_GET, [](AsyncWebServerRequest *request){
    String jsonResponse = "{\"estado vaga 1\": \"" + vaga1Status + "\",\"estado vaga 2\": \"" + vaga2Status + "\"}";
    request->send(200, "application/json", jsonResponse);
  });

  // Rota para reservar a vaga 1
  server.on("/reservar_vaga1", HTTP_POST, [](AsyncWebServerRequest *request){
    vaga1Reservada = true;
    vaga1Status = "vaga 1 reservada";
    setRGBColor(PIN_RED1, PIN_GREEN1, PIN_BLUE1, HIGH, HIGH, LOW); // Azul
    request->send(200, "application/json", "{\"message\": \"Vaga 1 reservada com sucesso\"}");
    Serial.println("Vaga 1 reservada via API");
  });

  // Rota para reservar a vaga 2
  server.on("/reservar_vaga2", HTTP_POST, [](AsyncWebServerRequest *request){
    vaga2Reservada = true;
    vaga2Status = "vaga 2 reservada";
    setRGBColor(PIN_RED2, PIN_GREEN2, PIN_BLUE2, HIGH, HIGH, LOW); // Azul
    request->send(200, "application/json", "{\"message\": \"Vaga 2 reservada com sucesso\"}");
    Serial.println("Vaga 2 reservada via API");
  });

  // Rota para liberar a vaga 1
  server.on("/liberar_vaga1", HTTP_POST, [](AsyncWebServerRequest *request){
    vaga1Reservada = false;
    vaga1Status = "vaga 1 livre";
    setRGBColor(PIN_RED1, PIN_GREEN1, PIN_BLUE1, HIGH, LOW, HIGH); // Verde
    request->send(200, "application/json", "{\"message\": \"Vaga 1 liberada com sucesso\"}");
    Serial.println("Vaga 1 liberada via API");
  });

  // Rota para liberar a vaga 2
  server.on("/liberar_vaga2", HTTP_POST, [](AsyncWebServerRequest *request){
    vaga2Reservada = false;
    vaga2Status = "vaga 2 livre";
    setRGBColor(PIN_RED2, PIN_GREEN2, PIN_BLUE2, HIGH, LOW, HIGH); // Verde
    request->send(200, "application/json", "{\"message\": \"Vaga 2 liberada com sucesso\"}");
    Serial.println("Vaga 2 liberada via API");
  });

  // Rota para abrir a catraca via HTTP
  server.on("/abrir_catraca", HTTP_POST, [](AsyncWebServerRequest *request){
    abrirCatraca();
    request->send(200, "application/json", "{\"message\": \"Catraca aberta via API\"}");
    Serial.println("Catraca aberta via API");
  });

  // Inicia o servidor
  server.begin();
  
  SPI.begin();            // Inicializa o barramento SPI
  mfrc522.PCD_Init();     // Inicializa o módulo RFID
}

void loop() {
  // Lê o estado dos sensores e atualiza os LEDs e o status das vagas
  updateSensors();

  // Lê os cartões RFID
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    // Converte o UID para uma string
    String uidString = uidToString(mfrc522.uid.uidByte, mfrc522.uid.size);
    Serial.print("UID lido: ");
    Serial.println(uidString);

    // Consulta o servidor para verificar o cartão
    if (verificarCartaoNoServidor(uidString)) {
      Serial.println("Cartão válido! Abrindo catraca...");
      abrirCatraca();
    } else {
      Serial.println("Cartão inválido!");
    }
    mfrc522.PICC_HaltA();      // Para a comunicação com o cartão
    mfrc522.PCD_StopCrypto1(); // Para a criptografia
  }
}

// Função para atualizar o status dos sensores de proximidade
void updateSensors() {
  int currentStateSensor1 = digitalRead(sensor1Pin);
  int currentStateSensor2 = digitalRead(sensor2Pin);

  if (!vaga1Reservada && currentStateSensor1 != lastStateSensor1) {
    lastStateSensor1 = currentStateSensor1;
    if (currentStateSensor1 == LOW) {
      setRGBColor(PIN_RED1, PIN_GREEN1, PIN_BLUE1, LOW, HIGH, HIGH); // Vermelho
      vaga1Status = "vaga 1 ocupada";
      Serial.println("Obstáculo detectado - Vaga 1 Ocupada");
    } else {
      setRGBColor(PIN_RED1, PIN_GREEN1, PIN_BLUE1, HIGH, LOW, HIGH); // Verde
      vaga1Status = "vaga 1 livre";
      Serial.println("Vaga 1 Livre");
    }
  }

  if (!vaga2Reservada && currentStateSensor2 != lastStateSensor2) {
    lastStateSensor2 = currentStateSensor2;
    if (currentStateSensor2 == LOW) {
      setRGBColor(PIN_RED2, PIN_GREEN2, PIN_BLUE2, LOW, HIGH, HIGH); // Vermelho
      vaga2Status = "vaga 2 ocupada";
      Serial.println("Obstáculo detectado - Vaga 2 Ocupada");
    } else {
      setRGBColor(PIN_RED2, PIN_GREEN2, PIN_BLUE2, HIGH, LOW, HIGH); // Verde
      vaga2Status = "vaga 2 livre";
      Serial.println("Vaga 2 Livre");
    }
  }
}

// Função para abrir a catraca e intercalar os LEDs
void abrirCatraca() {
  myServo.write(90);  // Move o servo para 90 graus (aberto)
  intercalarLeds();   // Alterna entre vermelho e verde por 10 segundos
  delay(5000);       // Aguarda 10 segundos
  myServo.write(0);   // Fecha a catraca (0 graus)
  Serial.println("Catraca fechada.");
}

// Função para configurar as cores dos LEDs RGB
void setRGBColor(int redPin, int greenPin, int bluePin, int redValue, int greenValue, int blueValue) {
  digitalWrite(redPin, redValue);
  digitalWrite(greenPin, greenValue);
  digitalWrite(bluePin, blueValue);
}

// Função para converter o UID do cartão em uma string hexadecimal
String uidToString(byte *buffer, byte bufferSize) {
  String uidString = "";
  for (byte i = 0; i < bufferSize; i++) {
    if (buffer[i] < 0x10) {
      uidString += "0";
    }
    uidString += String(buffer[i], HEX);
  }
  uidString.toUpperCase();
  return uidString;
}

// Função para verificar se o cartão é válido consultando o servidor Flask
bool verificarCartaoNoServidor(String uid) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = String("http://") + serverIP + ":" + String(serverPort) + "/verificar_cartao?rfid_uid=" + uid;
    http.begin(url);
    int httpCode = http.GET();

    if (httpCode == 200) {
      String payload = http.getString();
      http.end();
      if (payload == "valido") {
        return true;
      }
    } else {
      Serial.printf("Erro ao conectar ao servidor. Código HTTP: %d\n", httpCode);
      http.end();
    }
  } else {
    Serial.println("Wi-Fi não conectado.");
  }
  return false;
}
