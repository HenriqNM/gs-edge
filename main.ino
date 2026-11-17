/*
  EDIHUB TOTEM ESCOLAR - Global Solution (Edge Computing)
  ESP32 + HC-SR04 + LDR + OLED SSD1306 + LED RGB + Buzzer + Botao
  Dados: API simulada (JSON no GitHub) com reserva offline no proprio ESP32
  Nuvem: ThingSpeak (telemetria)
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <time.h>

// ================== 1. CONFIGURACOES (EDITE AQUI) ==================
const char* WIFI_SSID      = "Wokwi-GUEST";   // rede virtual do Wokwi
const char* WIFI_SENHA     = "";              // sem senha
const char* URL_API        = "https://raw.githubusercontent.com/HenriqNM/edihub-mock-api/refs/heads/main/db.json";
const char* THINGSPEAK_KEY = "1XR8XKOF1F4BI7DH";

// ================== 2. PINOS ==================
#define PIN_TRIG    5
#define PIN_ECHO    18
#define PIN_LDR     34   // ADC1 (funciona junto com o Wi-Fi)
#define PIN_BOTAO   4
#define PIN_LED_R   25
#define PIN_LED_G   26
#define PIN_LED_B   27
#define PIN_BUZZER  13

// ================== 3. PARAMETROS ==================
#define DIST_PRESENCA_CM   100      // aluno a menos de 1 metro
#define INTERVALO_TS       20000    // ThingSpeak: a cada 20 s (minimo gratis = 15 s)
#define INTERVALO_API      600000   // recarrega a API a cada 10 min
#define LIMITE_URGENTE_H   24       // ultimas 24 horas = alerta vermelho

Adafruit_SSD1306 display(128, 64, &Wire, -1);

// ================== 4. ESTRUTURA DE DADOS ==================
struct Certame {
  int id;
  String nome;
  String tipo;     // "vestibular" ou "militar"
  String evento;   // ex: "Isencao", "Inscricao"
  String prazo;    // formato AAAA-MM-DD
};

#define MAX_CERTAMES 10
Certame certames[MAX_CERTAMES];
int  totalCertames = 0;
bool dadosDaApi    = false;

const char* CATEGORIAS[] = { "vestibular", "militar" };
int categoria = 0;               // 0 = vestibular, 1 = militar

// Telemetria
int  aproximacoes     = 0;       // Campo 1
int  ultimoConsultado = 0;       // Campo 2 (id do certame)
int  diaAtual         = -1;
bool estavaPerto      = false;

unsigned long tUltimoTS = 0, tUltimaApi = 0, tUltimaTela = 0;

// ================== 5. RESERVA OFFLINE ==================
// Usada se o Wi-Fi ou a API falharem: o totem nunca fica sem informacao.
void carregarReservaOffline() {
  Certame locais[] = {
    {1, "UERJ",   "vestibular", "Isencao",   "2026-11-20"},
    {2, "UFRJ",   "vestibular", "Inscricao", "2026-11-28"},
    {3, "ENEM",   "vestibular", "Resultado", "2027-01-16"},
    {4, "EsPCEx", "militar",    "Inscricao", "2026-11-18"},
    {5, "IME",    "militar",    "Inscricao", "2026-12-02"}
  };
  totalCertames = sizeof(locais) / sizeof(locais[0]);
  for (int i = 0; i < totalCertames; i++) certames[i] = locais[i];
  dadosDaApi = false;
  Serial.println("[DADOS] Usando reserva OFFLINE");
}

// ================== 6. BUSCAR DADOS NA API SIMULADA ==================
bool buscarCertamesApi() {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure cliente;
  cliente.setInsecure();               // simulacao: nao valida certificado
  HTTPClient http;
  http.begin(cliente, URL_API);
  int codigo = http.GET();
  if (codigo != 200) {
    Serial.printf("[API] Erro HTTP %d\n", codigo);
    http.end();
    return false;
  }
  String corpo = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError erro = deserializeJson(doc, corpo);
  if (erro) {
    Serial.printf("[API] JSON invalido: %s\n", erro.c_str());
    return false;
  }

  totalCertames = 0;
  for (JsonObject c : doc["certames"].as<JsonArray>()) {
    if (totalCertames >= MAX_CERTAMES) break;
    certames[totalCertames].id     = c["id"] | 0;
    certames[totalCertames].nome   = c["nome"].as<String>();
    certames[totalCertames].tipo   = c["tipo"].as<String>();
    certames[totalCertames].evento = c["evento"].as<String>();
    certames[totalCertames].prazo  = c["prazo"].as<String>();
    totalCertames++;
  }
  dadosDaApi = totalCertames > 0;
  Serial.printf("[API] %d certames carregados\n", totalCertames);
  return dadosDaApi;
}

void atualizarDados() {
  if (!buscarCertamesApi()) {
    if (totalCertames == 0) carregarReservaOffline();
  }
}

// ================== 7. CALCULO DE PRAZO (PROCESSAMENTO NA BORDA) ==================
bool relogioOk() { return time(nullptr) > 1700000000; }   // NTP ja sincronizou?

// Horas que faltam ate 23:59:59 do dia do prazo
long horasRestantes(const String& prazo) {
  struct tm fim = {};
  fim.tm_year = prazo.substring(0, 4).toInt() - 1900;
  fim.tm_mon  = prazo.substring(5, 7).toInt() - 1;
  fim.tm_mday = prazo.substring(8, 10).toInt();
  fim.tm_hour = 23; fim.tm_min = 59; fim.tm_sec = 59;
  time_t tFim = mktime(&fim);
  return (long)(difftime(tFim, time(nullptr)) / 3600);
}

// Escolhe o certame mais urgente (ainda nao vencido) da categoria atual
int escolherCritico() {
  int melhor = -1;
  long menor = 0;
  for (int i = 0; i < totalCertames; i++) {
    if (certames[i].tipo != CATEGORIAS[categoria]) continue;
    if (!relogioOk()) return i;               // sem relogio: mostra o primeiro
    long h = horasRestantes(certames[i].prazo);
    if (h < 0) continue;                      // prazo ja passou
    if (melhor == -1 || h < menor) { melhor = i; menor = h; }
  }
  return melhor;
}

// ================== 8. SENSORES ==================
float lerDistanciaCm() {
  digitalWrite(PIN_TRIG, LOW);  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH); delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  long duracao = pulseIn(PIN_ECHO, HIGH, 30000);
  if (duracao == 0) return 999;
  return duracao * 0.0343 / 2;
}

// No modulo LDR do Wokwi: quanto MAIS escuro, MAIOR o valor lido
int lerLuzPercent() {
  int bruto = analogRead(PIN_LDR);
  return map(bruto, 0, 4095, 100, 0);
}

// ================== 9. ATUADORES ==================
void corLed(bool r, bool g, bool b) {
  digitalWrite(PIN_LED_R, r);
  digitalWrite(PIN_LED_G, g);
  digitalWrite(PIN_LED_B, b);
}

void atualizarLed(int idx) {
  if (idx < 0 || !relogioOk()) { corLed(0, 0, 1); return; }    // azul = sem dados
  long h = horasRestantes(certames[idx].prazo);
  if (h <= LIMITE_URGENTE_H) {
    bool aceso = (millis() / 300) % 2;                         // pisca vermelho
    corLed(aceso, 0, 0);
  } else {
    corLed(0, 1, 0);                                           // verde = aberto
  }
}

void ajustarBrilho(int luz) {
  // Corredor escuro -> tela mais fraca (economia de energia)
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(luz < 30 ? 10 : 255);
}

void desenharTela(int idx, bool perto) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("EDIHUB | ");
  display.print(categoria == 0 ? "VESTIBULAR" : "MILITAR");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  if (idx < 0) {
    display.setCursor(0, 24);
    display.print("Nenhum prazo aberto");
  } else {
    Certame& c = certames[idx];
    display.setTextSize(2);
    display.setCursor(0, 14);
    display.print(c.nome);

    display.setTextSize(1);
    display.setCursor(0, 33);
    display.print(c.evento);

    display.setCursor(0, 43);
    if (!relogioOk()) {
      display.print("Prazo: " + c.prazo);
    } else {
      long h = horasRestantes(c.prazo);
      if (h <= LIMITE_URGENTE_H) display.print("ULTIMAS 24H!");
      else                       display.printf("Termina em %ld dias", h / 24);
    }
  }

  display.setCursor(0, 56);
  display.print(perto ? "Ola, estudante!" : "Aproxime-se");
  display.setCursor(92, 56);
  display.print(dadosDaApi ? "[API]" : "[OFF]");
  display.display();
}

// ================== 10. NUVEM (THINGSPEAK) ==================
void enviarThingSpeak(int luz) {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url = String("http://api.thingspeak.com/update?api_key=") + THINGSPEAK_KEY +
               "&field1=" + aproximacoes +
               "&field2=" + ultimoConsultado +
               "&field3=" + luz;
  http.begin(url);
  int codigo = http.GET();
  Serial.printf("[ThingSpeak] HTTP %d | resposta: %s\n", codigo, http.getString().c_str());
  http.end();
}

// ================== 11. SETUP ==================
void setup() {
  Serial.begin(115200);
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_BOTAO, INPUT_PULLUP);
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Falha no OLED");
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Edihub Totem");
  display.println("Conectando Wi-Fi...");
  display.display();

  WiFi.begin(WIFI_SSID, WIFI_SENHA, 6);
  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 40) {
    delay(250);
    tentativas++;
  }
  Serial.println(WiFi.status() == WL_CONNECTED ? "[WiFi] Conectado" : "[WiFi] Sem conexao");

  configTime(-3 * 3600, 0, "pool.ntp.org");   // horario de Brasilia
  for (int i = 0; i < 20 && !relogioOk(); i++) delay(250);

  atualizarDados();
  tUltimaApi = millis();
}

// ================== 12. LOOP ==================
void loop() {
  // Botao: alterna vestibular <-> militar
  static bool botaoAntes = HIGH;
  bool botao = digitalRead(PIN_BOTAO);
  if (botaoAntes == HIGH && botao == LOW) {
    categoria = (categoria + 1) % 2;
    int idx = escolherCritico();
    if (idx >= 0) ultimoConsultado = certames[idx].id;
    Serial.printf("[BOTAO] Categoria: %s\n", CATEGORIAS[categoria]);
    delay(50);                                       // debounce
  }
  botaoAntes = botao;

  // Zera a contagem de aproximacoes quando o dia muda
  if (relogioOk()) {
    time_t agora = time(nullptr);
    struct tm* t = localtime(&agora);
    if (diaAtual != -1 && t->tm_mday != diaAtual) aproximacoes = 0;
    diaAtual = t->tm_mday;
  }

  // Sensores
  float dist = lerDistanciaCm();
  int luz = lerLuzPercent();
  bool perto = dist < DIST_PRESENCA_CM;
  int idx = escolherCritico();

  // Novo aluno chegou perto (borda: longe -> perto)
  if (perto && !estavaPerto) {
    aproximacoes++;
    if (idx >= 0) ultimoConsultado = certames[idx].id;
    Serial.printf("[PRESENCA] %.0f cm | total hoje: %d\n", dist, aproximacoes);
    if (idx >= 0 && relogioOk() && horasRestantes(certames[idx].prazo) <= LIMITE_URGENTE_H) {
      tone(PIN_BUZZER, 2000, 300);                   // alerta sonoro de prazo
    }
  }
  estavaPerto = perto;

  atualizarLed(idx);

  if (millis() - tUltimaTela > 500) {
    ajustarBrilho(luz);
    desenharTela(idx, perto);
    tUltimaTela = millis();
  }

  if (millis() - tUltimoTS > INTERVALO_TS) {
    enviarThingSpeak(luz);
    tUltimoTS = millis();
  }

  if (millis() - tUltimaApi > INTERVALO_API) {
    atualizarDados();
    tUltimaApi = millis();
  }

  delay(50);
}
