#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ESP32 DevKitC V4
constexpr uint8_t PIN_MIC = 34;
constexpr uint8_t PIN_POT_BPM = 35;
constexpr uint8_t PIN_START_STOP = 25;
constexpr uint8_t PIN_MODO = 26;
constexpr uint8_t PIN_SIMULAR_SOM = 27;
constexpr uint8_t PIN_LED_R = 14;
constexpr uint8_t PIN_LED_G = 12;
constexpr uint8_t PIN_LED_B = 13;
constexpr uint8_t PIN_BUZZER = 23;
constexpr uint8_t PIN_OLED_SDA = 21;
constexpr uint8_t PIN_OLED_SCL = 22;

constexpr uint16_t BPM_MIN = 40;
constexpr uint16_t BPM_MAX = 200;
constexpr uint16_t ADC_MAXIMO = 4095;
constexpr uint16_t CENTRO_MIC = 2048;
constexpr uint16_t LIMIAR_ATAQUE = 650;
constexpr uint16_t LIMIAR_REARME = 250;
constexpr uint16_t JANELA_AMOSTRAGEM_MS = 20;
constexpr uint16_t TEMPO_REARME_MS = 300;

Adafruit_SSD1306 display(128, 64, &Wire, -1);
bool oledDisponivel = false;

enum Modo : uint8_t {
  DETECCAO,
  METRONOMO
};

Modo modoAtual = DETECCAO;
bool ativo = true;

struct Botao {
  uint8_t pino;
  bool leituraAnterior = HIGH;
  bool estadoEstavel = HIGH;
  unsigned long mudouEm = 0;

  explicit Botao(uint8_t pinoConfigurado) : pino(pinoConfigurado) {}

  bool foiPressionado(unsigned long agora) {
    const bool leitura = digitalRead(pino);
    if (leitura != leituraAnterior) {
      leituraAnterior = leitura;
      mudouEm = agora;
    }

    if ((agora - mudouEm) >= 25 && leitura != estadoEstavel) {
      estadoEstavel = leitura;
      return estadoEstavel == LOW;
    }
    return false;
  }
};

Botao botaoStart(PIN_START_STOP);
Botao botaoModo(PIN_MODO);
Botao botaoSimularSom(PIN_SIMULAR_SOM);

uint16_t bpm = 120;
int ultimoPot = -100;
int ultimaAmostraMic = CENTRO_MIC;
int ultimaAmplitudeMic = 0;
int menorAmostraJanela = ADC_MAXIMO;
int maiorAmostraJanela = 0;
bool detectorArmado = false;

unsigned long proximaBatida = 0;
unsigned long ultimoAtaque = 0;
unsigned long inicioJanelaMic = 0;
unsigned long sinalBaixoDesde = 0;
unsigned long apagarLedEm = 0;
unsigned long atualizarTelaEm = 0;
unsigned long imprimirMicEm = 0;

void definirCor(uint8_t vermelho, uint8_t verde, uint8_t azul) {
  analogWrite(PIN_LED_R, vermelho);
  analogWrite(PIN_LED_G, verde);
  analogWrite(PIN_LED_B, azul);
}

unsigned long intervaloBatidaMs() {
  return 60000UL / bpm;
}

void silenciarSaidas() {
  noTone(PIN_BUZZER);
  digitalWrite(PIN_BUZZER, LOW);
  definirCor(0, 0, 0);
  apagarLedEm = 0;
}

void registrarAtaque(unsigned long agora, const __FlashStringHelper *origem) {
  ultimoAtaque = agora;
  definirCor(0, 255, 0);
  apagarLedEm = agora + 100;

  Serial.print(F("ATAQUE | origem="));
  Serial.print(origem);
  Serial.print(F(" | amostra="));
  Serial.print(ultimaAmostraMic);
  Serial.print(F(" | amplitude="));
  Serial.println(ultimaAmplitudeMic);
}

void lerMicrofone(unsigned long agora) {
  if (modoAtual != DETECCAO || !ativo) return;

  ultimaAmostraMic = analogRead(PIN_MIC);
  menorAmostraJanela = min(menorAmostraJanela, ultimaAmostraMic);
  maiorAmostraJanela = max(maiorAmostraJanela, ultimaAmostraMic);

  // Mede a envoltoria (pico a pico / 2) em vez de testar cada amostra.
  // Assim, uma onda continua do microfone virtual gera no maximo um ataque.
  if ((agora - inicioJanelaMic) >= JANELA_AMOSTRAGEM_MS) {
    ultimaAmplitudeMic = (maiorAmostraJanela - menorAmostraJanela) / 2;
    menorAmostraJanela = ADC_MAXIMO;
    maiorAmostraJanela = 0;
    inicioJanelaMic = agora;

    if (detectorArmado && ultimaAmplitudeMic >= LIMIAR_ATAQUE) {
      detectorArmado = false;
      sinalBaixoDesde = 0;
      registrarAtaque(agora, F("MIC"));
    } else if (!detectorArmado) {
      if (ultimaAmplitudeMic <= LIMIAR_REARME) {
        if (sinalBaixoDesde == 0) sinalBaixoDesde = agora;
        if ((agora - sinalBaixoDesde) >= TEMPO_REARME_MS) {
          detectorArmado = true;
          sinalBaixoDesde = 0;
          Serial.println(F("DETECTOR ARMADO: aumente o nivel do microfone ou pressione T"));
        }
      } else {
        sinalBaixoDesde = 0;
      }
    }
  }

  // Permite observar o sinal virtual mesmo quando ele nao cruza o limiar.
  if ((long)(agora - imprimirMicEm) >= 0) {
    imprimirMicEm = agora + 500;
    Serial.print(F("MIC | amostra="));
    Serial.print(ultimaAmostraMic);
    Serial.print(F(" | amplitude="));
    Serial.print(ultimaAmplitudeMic);
    Serial.print(F(" | estado="));
    Serial.println(detectorArmado ? F("ARMADO") : F("AGUARDANDO SILENCIO"));
  }
}

void atualizarBpm(unsigned long agora) {
  static unsigned long proximaLeitura = 0;
  if (modoAtual != METRONOMO || (long)(agora - proximaLeitura) < 0) return;
  proximaLeitura = agora + 50;

  const int leitura = analogRead(PIN_POT_BPM);
  if (ultimoPot < 0 || abs(leitura - ultimoPot) >= 16) {
    ultimoPot = leitura;
    const uint16_t novoBpm = map(leitura, 0, ADC_MAXIMO, BPM_MIN, BPM_MAX);
    if (novoBpm != bpm) {
      bpm = novoBpm;
      if (ativo) proximaBatida = agora + intervaloBatidaMs();
    }
  }
}

void executarMetronomo(unsigned long agora) {
  if (modoAtual != METRONOMO || !ativo) return;
  if ((long)(agora - proximaBatida) < 0) return;

  tone(PIN_BUZZER, 1400, 35);
  definirCor(0, 0, 255);
  apagarLedEm = agora + 60;

  proximaBatida += intervaloBatidaMs();
  if ((long)(agora - proximaBatida) >= 0) {
    proximaBatida = agora + intervaloBatidaMs();
  }
}

void alternarModo(unsigned long agora) {
  silenciarSaidas();
  modoAtual = (modoAtual == DETECCAO) ? METRONOMO : DETECCAO;

  if (modoAtual == METRONOMO) {
    ultimoPot = -100;
    proximaBatida = agora;
    Serial.println(F("MODO: METRONOMO | potenciometro controla 40-200 BPM"));
  } else {
    ultimoAtaque = agora;
    detectorArmado = false;
    sinalBaixoDesde = 0;
    inicioJanelaMic = agora;
    menorAmostraJanela = ADC_MAXIMO;
    maiorAmostraJanela = 0;
    imprimirMicEm = agora;
    Serial.println(F("MODO: DETECCAO | buzzer desativado"));
    Serial.println(F("Para o MIC: deixe o nivel baixo por 300 ms e depois aumente"));
    Serial.println(F("Para teste direto: pressione T para simular um ataque"));
  }
}

void processarBotoes(unsigned long agora) {
  if (botaoStart.foiPressionado(agora)) {
    ativo = !ativo;
    silenciarSaidas();
    if (ativo && modoAtual == METRONOMO) proximaBatida = agora;
    Serial.println(ativo ? F("SISTEMA: ATIVO") : F("SISTEMA: PAUSADO"));
  }

  if (botaoModo.foiPressionado(agora)) {
    alternarModo(agora);
  }

  if (botaoSimularSom.foiPressionado(agora) &&
      modoAtual == DETECCAO && ativo) {
    ultimaAmostraMic = CENTRO_MIC + LIMIAR_ATAQUE + 100;
    ultimaAmplitudeMic = LIMIAR_ATAQUE + 100;
    registrarAtaque(agora, F("SIMULADO"));
  }
}

void atualizarDisplay(unsigned long agora) {
  if (!oledDisponivel || (long)(agora - atualizarTelaEm) < 0) return;
  atualizarTelaEm = agora + 150;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("METRONOMO INTELIGENTE"));

  display.setTextSize(2);
  display.setCursor(0, 17);
  if (modoAtual == DETECCAO) {
    display.println(F("DETECCAO"));
    display.setTextSize(1);
    display.setCursor(0, 43);
    display.print(F("MIC: "));
    display.print(ultimaAmostraMic);
    display.print(F(" A:"));
    display.println(ultimaAmplitudeMic);
    display.setCursor(0, 55);
    display.print(F("T=SIMULAR  "));
  } else {
    display.print(bpm);
    display.println(F(" BPM"));
    display.setTextSize(1);
    display.setCursor(0, 47);
    display.print(F("MODO METRONOMO"));
  }

  display.setTextSize(1);
  display.setCursor(88, 55);
  display.print(ativo ? F("ATIVO") : F("PAUSA"));
  display.display();
}

void setup() {
  pinMode(PIN_START_STOP, INPUT_PULLUP);
  pinMode(PIN_MODO, INPUT_PULLUP);
  pinMode(PIN_SIMULAR_SOM, INPUT_PULLUP);
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  silenciarSaidas();

  Serial.begin(115200);
  analogReadResolution(12);
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  oledDisponivel = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  // Autoteste somente visual: nenhum som na inicializacao.
  definirCor(255, 0, 0);
  delay(120);
  definirCor(0, 255, 0);
  delay(120);
  definirCor(0, 0, 255);
  delay(120);
  definirCor(0, 0, 0);

  const unsigned long agora = millis();
  ultimoAtaque = agora;
  inicioJanelaMic = agora;
  imprimirMicEm = agora;

  Serial.println(F("ESP32 - teste inicial de hardware"));
  Serial.println(F("MODO: DETECCAO | buzzer desativado"));
  Serial.println(F("S=start/stop, M=modo, T=simular ataque"));
  Serial.println(F("O MIC so arma apos 300 ms de sinal baixo"));
}

void loop() {
  const unsigned long agora = millis();

  processarBotoes(agora);
  lerMicrofone(agora);
  atualizarBpm(agora);
  executarMetronomo(agora);

  if (apagarLedEm != 0 && (long)(agora - apagarLedEm) >= 0) {
    definirCor(0, 0, 0);
    apagarLedEm = 0;
  }

  atualizarDisplay(agora);
}
