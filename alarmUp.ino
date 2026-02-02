// Criado por Lucas Carvalho @br.lcsistemas
//HC-SR501 & Esp32 - 2026-02-02

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>

// ---- Pinos ----
const int pirPin = 27; // PIR OUT
const int ledStatus = 2; // LED indicador (status)
const int relePin = 25; // RELÉ / dispositivo acionado

// ---- Configurações ----
unsigned long tempoAlarmeMs = 10000; // tempo do relé ligado (10s)
const unsigned long cooldownMs = 60000; // tempo mínimo entre disparos
const unsigned long logIntervalMs = 700; // log do PIR (ms)

// ---- Anti-ruído / estabilidade ----
const unsigned long pirWarmupMs = 60000; // tempo de estabilização do PIR (SR501)
const unsigned long posReleIgnoreMs = 800; // ignora PIR logo após ligar/desligar relé
const int amostrasPIR = 8; // amostras para confirmar
const int amostrasMinHigh = 6; // quantas HIGH para confirmar
const int delayAmostraMs = 25; // 8*25 = 200ms

// ---- Estado ----
bool movimentoAtivo = false;
bool releAtivo = false;

unsigned long ultimoDisparo = 0;
unsigned long inicioRele = 0;
unsigned long ultimoLog = 0;

unsigned long inicioSistema = 0;
unsigned long ignorarPIRAte = 0;

//baixe um app chamado discord, crie um servidor nele, crie um canal de texto
//clique em integrações > webhook > novo webhook
static const char* WEBHOOK_URL = "SEU_WEBHOOK_AQUI"; // <-- cole seu webhook

void alerta();
bool pirPronto();
bool movimentoConfirmado();
void ligaRele(unsigned long agora);
void desligaRele(unsigned long agora);

void setup() {
// Para HC-SR501: ajuda a evitar leitura flutuante
pinMode(pirPin, INPUT_PULLDOWN);

pinMode(ledStatus, OUTPUT);
pinMode(relePin, OUTPUT);

digitalWrite(ledStatus, LOW);
digitalWrite(relePin, LOW);

Serial.begin(115200);
delay(200);
Serial.println("\nSensor de Presenca ESP32 (HC-SR501 + Rele) - Versao melhorada");

inicioSistema = millis();

// WiFiManager
WiFiManager wifiManager;
wifiManager.setConfigPortalTimeout(180);

if (!wifiManager.autoConnect("ALARME", "1234567890")) {
Serial.println(F("Falha na conexao. Resetando..."));
delay(1500);
ESP.restart();
}

Serial.println(F("Conectado no Wi-Fi."));
Serial.print(F("IP: "));
Serial.println(WiFi.localIP());

digitalWrite(ledStatus, HIGH);

Serial.print("Aguardando estabilizacao do PIR por ");
Serial.print(pirWarmupMs / 1000);
Serial.println("s...");
}

void loop() {
unsigned long agora = millis();

// Warm-up do SR501
if (!pirPronto()) {
// pisca de leve durante warmup (opcional)
if ((agora / 500) % 2 == 0) digitalWrite(ledStatus, HIGH);
else digitalWrite(ledStatus, LOW);
return;
} else {
digitalWrite(ledStatus, HIGH);
}

// Lê PIR
int pirState = digitalRead(pirPin);

// Log periódico
if (agora - ultimoLog >= logIntervalMs) {
ultimoLog = agora;
Serial.print("PIR: ");
Serial.print(pirState == HIGH ? "HIGH" : "LOW");
if (releAtivo) Serial.print(" | RELE: ON");
Serial.println();
}

// Desliga relé depois do tempo configurado
if (releAtivo && (agora - inicioRele >= tempoAlarmeMs)) {
desligaRele(agora);
}

// Se estiver no período de ignorar PIR (anti-ruído)
if (agora < ignorarPIRAte) {
delay(5);
return;
}

// Cooldown entre disparos
bool podeDisparar = (agora - ultimoDisparo) >= cooldownMs;

// Disparo por borda LOW->HIGH (com confirmação)
if (pirState == HIGH && !movimentoAtivo && podeDisparar) {

// Confirma o movimento para evitar pico falso
if (!movimentoConfirmado()) {
// foi só um "pico"
movimentoAtivo = true; // trava momentânea para não ficar repetindo
Serial.println("⚠️ Pico detectado (nao confirmado). Ignorado.");
} else {
movimentoAtivo = true;
ultimoDisparo = agora;

ligaRele(agora);

Serial.print("🚨 Movimento CONFIRMADO! Relé ligado por ");
Serial.print(tempoAlarmeMs / 1000);
Serial.println("s. Enviando alerta...");

alerta();
}
}

// Rearma quando PIR voltar a LOW
if (pirState == LOW && movimentoAtivo) {
movimentoAtivo = false;
Serial.println("🔄 PIR voltou ao repouso. Re-armado.");
}

delay(10);
}

bool pirPronto() {
return (millis() - inicioSistema) > pirWarmupMs;
}

bool movimentoConfirmado() {
int contHigh = 0;

for (int i = 0; i < amostrasPIR; i++) {
if (digitalRead(pirPin) == HIGH) contHigh++;
delay(delayAmostraMs);
}

// Debug opcional:
Serial.print("Confirmacao PIR: ");
Serial.print(contHigh);
Serial.print("/");
Serial.println(amostrasPIR);

return contHigh >= amostrasMinHigh;
}

void ligaRele(unsigned long agora) {
releAtivo = true;
inicioRele = agora;
digitalWrite(relePin, HIGH);

// ignora PIR por um curto período após ligar relé (ruído)
ignorarPIRAte = agora + posReleIgnoreMs;
}

void desligaRele(unsigned long agora) {
releAtivo = false;
digitalWrite(relePin, LOW);
Serial.println("⏱️ Relé desligado (tempo do alarme encerrou)");

// ignora PIR um pouco após desligar também
ignorarPIRAte = agora + posReleIgnoreMs;
}

void alerta() {
if (WiFi.status() != WL_CONNECTED) {
Serial.println("Wi-Fi desconectado, nao foi possivel enviar alerta.");
return;
}

WiFiClientSecure client;
client.setInsecure();

HTTPClient https;
Serial.println("[HTTPS] Iniciando...");

if (!https.begin(client, WEBHOOK_URL)) {
Serial.println("[HTTPS] Falha ao conectar (begin).");
return;
}

https.addHeader("Content-Type", "application/json");
String httpRequestData = "{\"content\":\"Movimento detectado!\"}";

int httpCode = https.POST(httpRequestData);

if (httpCode > 0) {
Serial.printf("[HTTPS] Codigo da resposta: %d\n", httpCode);
// Não precisa ler payload sempre (webhook às vezes não retorna corpo útil)
} else {
Serial.printf("[HTTPS] Falha na requisicao: %s\n",
https.errorToString(httpCode).c_str());
}

https.end();
}
