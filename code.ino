/*
 * ============================================================================
 *  METRÔNOMO INTELIGENTE — ESP32 DEVKIT V1
 * ============================================================================
 *  Projeto Integrado de Computação (PIC) — UFES
 *  Membros: Arthur Viana, Lorenzo Tottola e Maurício Gomez
 *
 *  Descrição:
 *    Dispositivo embarcado com dois modos de operação:
 *      - Modo 1 (Metrônomo): gera referência rítmica sonora (buzzer passivo)
 *        e visual (LED azul) entre 30 e 180 BPM ajustável via potenciômetro.
 *      - Modo 2 (Jogo / Detecção Rítmica): calibra o ruído ambiente por 10s
 *        (algoritmo de Welford), detecta 4 ataques musicais via microfone
 *        MAX4466, calcula a acurácia temporal em relação ao BPM alvo e exibe
 *        o feedback no OLED e nos LEDs (Verde >90%, Amarelo 75-90%, Vermelho <75%).
 *      - Easter Egg: 5 cliques rápidos (<3s) no botão MODO tocam "Für Elise"
 *        (Beethoven) em loop até o botão ser pressionado novamente.
 * ============================================================================
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>

// ==================== PINOS (ESP32 DEVKIT V1) ====================

const int PIN_BUZZER       = 23; // D23: Buzzer passivo (sinal PWM via tone)
const int PIN_POT          = 35; // D35: Cursor do potenciômetro 10k (ADC1_CH7)
const int PIN_LED_AZUL     = 32; // D32: LED de pulso (batida / ataque detectado)
const int PIN_LED_VERDE    = 14; // D14: LED indicador de acurácia > 90%
const int PIN_LED_AMARELO  = 13; // D13: LED indicador de acurácia entre 75% e 90%
const int PIN_LED_VERMELHO = 18; // D18: LED indicador de acurácia < 75%
const int PIN_START_STOP   = 25; // D25: Botão Start/Stop (INPUT_PULLUP -> GND)
const int PIN_MODO         = 26; // D26: Botão de troca de modo / Easter Egg (INPUT_PULLUP -> GND)
const int PIN_MIC          = 34; // D34: Saída analógica OUT do microfone MAX4466 (ADC1_CH6)

// Pinos I2C configurados explicitamente para o roteamento da placa dupla face
const int PIN_SDA = 22; // D22: Linha de dados I2C do OLED
const int PIN_SCL = 21; // D21: Linha de clock I2C do OLED

// ==================== OLED (SSD1306 128x64) ====================

Adafruit_SSD1306 display(128, 64, &Wire, -1);

bool oledDisponivel = false; // Indica se o display respondeu no endereço 0x3C
bool atualizarOled = true;   // Flag de dirty-render para evitar redesenhos desnecessários

// ==================== CONFIGURAÇÕES ====================

const int BPM_MINIMO = 30;
const int BPM_MAXIMO = 180;

const int FREQUENCIA_BUZZER = 1000;        // Frequência da batida do metrônomo (Hz)
const unsigned long DURACAO_PULSO = 80;    // Duração do pulso luminoso/sonoro (ms)

const unsigned long TEMPO_CALIBRACAO = 10000;   // Janela de calibração do ruído ambiente (10s)
const unsigned long TEMPO_MORTO_ATAQUE = 120;   // Tempo mínimo entre ataques consecutivos (ms)

// Parâmetros do limiar adaptativo de detecção sonora
const float FATOR_DESVIO  = 7.0;   // Multiplicador sobre o desvio padrão do ambiente
const float MARGEM_MAXIMO = 1.40;  // Margem de segurança sobre o maior pico do ambiente
const float LIMIAR_MINIMO = 100.0; // Piso mínimo em unidades ADC (12 bits)

// Easter egg Beethoven (5 cliques em < 3 segundos no botão MODO)
const int CLIQUES_BEETHOVEN = 5;
const unsigned long JANELA_BEETHOVEN_MS = 3000;

// ==================== ESTADO GERAL ====================

bool sistemaAtivo = true; // Estado global de execução (ativo vs pausado)
int modoAtual = 1;        // 1 = Metrônomo tradicional | 2 = Jogo de detecção
int bpm = 120;            // BPM atual confirmado

// Variáveis de filtragem do potenciômetro (EMA + histerese por confirmação)
int potFiltrado = -1;
int bpmCandidato = 120;
int confirmacoesBpm = 0;

unsigned long proximaBatida = 0;    // Timestamp da próxima batida no Modo 1
unsigned long fimPulso = 0;         // Timestamp para encerrar o pulso de LED/buzzer
unsigned long proximaLeituraPot = 0;// Controle da taxa de amostragem do potenciômetro (50 Hz)

// ==================== BEETHOVEN (FÜR ELISE) ====================

bool tocandoBeethoven = false;
unsigned long historicoCliquesModo[CLIQUES_BEETHOVEN] = {0};
int totalCliquesModo = 0;

struct Nota {
  int freq;                // Frequência em Hz (0 representa pausa)
  unsigned long duracaoMs; // Duração da nota/pausa em milissegundos
};

// Partitura simplificada do tema principal de Für Elise (Ludwig van Beethoven)
const Nota MELODIA_BEETHOVEN[] = {
  {659, 180}, {622, 180}, {659, 180}, {622, 180}, {659, 180}, {494, 180}, {587, 180}, {523, 180},
  {440, 360}, {0,   180}, {262, 180}, {330, 180}, {440, 180}, {494, 360}, {0,   180}, {330, 180},
  {415, 180}, {494, 180}, {523, 360}, {0,   180}, {330, 180}, {659, 180}, {622, 180}, {659, 180},
  {622, 180}, {659, 180}, {494, 180}, {587, 180}, {523, 180}, {440, 360}, {0,   180}, {262, 180},
  {330, 180}, {440, 180}, {494, 360}, {0,   180}, {330, 180}, {523, 180}, {494, 180}, {440, 540},
  {0,   400}
};

const int TOTAL_NOTAS_BEETHOVEN = sizeof(MELODIA_BEETHOVEN) / sizeof(MELODIA_BEETHOVEN[0]);
int indiceNotaBeethoven = 0;
unsigned long proximaNotaBeethoven = 0;

// ==================== MODO 2 (CALIBRAÇÃO E DETECÇÃO) ====================

enum EstadoModo2 {
  MODO2_PARADO,
  MODO2_CALIBRANDO,
  MODO2_DETECTANDO
};

EstadoModo2 estadoModo2 = MODO2_PARADO;

unsigned long inicioCalibracao = 0;
int ultimoSegundoExibido = -1;

// Acumuladores do algoritmo online de Welford (média e variância em passagem única)
unsigned long quantidadeAmostras = 0;
double mediaCalibracao = 0;
double somaQuadrados = 0;

float centroMic = 2048;         // Nível DC médio do MAX4466 (~VCC/2 em 12 bits)
float maiorPicoAmbiente = 0;    // Maior amplitude de ruído observada na calibração
float desvioAmbiente = 0;       // Desvio padrão do ruído ambiente
float limiarAtaque = 200;       // Limiar calculado para disparar detecção de ataque

bool detectorArmado = true;     // Histerese para evitar múltiplos disparos no mesmo som
unsigned long ultimoAtaque = 0; // Timestamp do último ataque válido

unsigned long temposAtaques[4]; // Armazena os instantes dos 4 ataques do compasso
int quantidadeAtaques = 0;

float ultimaAcuracia = -1;      // Último percentual calculado (-1 indica ainda não medido)

// ==================== BOTÕES (DEBOUNCE POR SOFTWARE) ====================

struct Botao {
  int pino;
  bool leituraAnterior = HIGH;
  bool estadoEstavel = HIGH;
  unsigned long mudouEm = 0;

  // Retorna true exatamente uma vez na borda de descida (pressionamento) após 30 ms estáveis
  bool foiPressionado(unsigned long agora) {
    bool leitura = digitalRead(pino);

    if (leitura != leituraAnterior) {
      leituraAnterior = leitura;
      mudouEm = agora;
    }

    if (
      agora - mudouEm >= 30 &&
      leitura != estadoEstavel
    ) {
      estadoEstavel = leitura;
      return estadoEstavel == LOW;
    }

    return false;
  }
};

Botao botaoStartStop = {PIN_START_STOP};
Botao botaoModo = {PIN_MODO};

// ==================== CONTROLE DE SAÍDAS ====================

void apagarLedsAcuracia() {
  digitalWrite(PIN_LED_VERDE, LOW);
  digitalWrite(PIN_LED_AMARELO, LOW);
  digitalWrite(PIN_LED_VERMELHO, LOW);
}

// Sinaliza o desempenho rítmico nas faixas definidas:
// > 90%: Verde | 75% a 90%: Amarelo | < 75%: Vermelho
void mostrarAcuraciaNosLeds(float acuracia) {
  apagarLedsAcuracia();

  if (acuracia > 90.0) {
    digitalWrite(PIN_LED_VERDE, HIGH);
  } else if (acuracia >= 75.0) {
    digitalWrite(PIN_LED_AMARELO, HIGH);
  } else {
    digitalWrite(PIN_LED_VERMELHO, HIGH);
  }
}

void desligarBuzzer() {
  noTone(PIN_BUZZER);
  digitalWrite(PIN_BUZZER, LOW);
}

void desligarSaidas() {
  desligarBuzzer();
  digitalWrite(PIN_LED_AZUL, LOW);
  fimPulso = 0;
}

void iniciarPulsoLed(unsigned long agora) {
  digitalWrite(PIN_LED_AZUL, HIGH);
  fimPulso = agora + DURACAO_PULSO;
}

// Encerra o pulso visual e sonoro de forma não-bloqueante quando DURACAO_PULSO expira
void atualizarFimPulso(unsigned long agora) {
  if (
    fimPulso != 0 &&
    (long)(agora - fimPulso) >= 0
  ) {
    digitalWrite(PIN_LED_AZUL, LOW);
    desligarBuzzer();
    fimPulso = 0;
  }
}

// ==================== EASTER EGG (BEETHOVEN) ====================

void iniciarBeethoven(unsigned long agora) {
  tocandoBeethoven = true;
  totalCliquesModo = 0;
  desligarSaidas();
  apagarLedsAcuracia();
  estadoModo2 = MODO2_PARADO;

  indiceNotaBeethoven = 0;
  proximaNotaBeethoven = agora;
  atualizarOled = true;

  Serial.println("EASTER EGG: TOCANDO BEETHOVEN (FUR ELISE)!");
}

void pararBeethoven(unsigned long agora) {
  tocandoBeethoven = false;
  totalCliquesModo = 0;
  desligarSaidas();

  if (sistemaAtivo) {
    if (modoAtual == 1) {
      proximaBatida = agora;
    } else {
      // Ao sair do Beethoven no Modo 2, reinicia calibração
      estadoModo2 = MODO2_PARADO;
    }
  }

  atualizarOled = true;
  Serial.println("BEETHOVEN PARADO.");
}

// Reproduz as notas da partitura em loop usando temporização não-bloqueante
void atualizarBeethoven(unsigned long agora) {
  if ((long)(agora - proximaNotaBeethoven) < 0) {
    return;
  }

  const Nota& nota = MELODIA_BEETHOVEN[indiceNotaBeethoven];

  if (nota.freq > 0) {
    tone(PIN_BUZZER, nota.freq);
  } else {
    desligarBuzzer();
  }

  proximaNotaBeethoven = agora + nota.duracaoMs;
  indiceNotaBeethoven = (indiceNotaBeethoven + 1) % TOTAL_NOTAS_BEETHOVEN;
}

// ==================== INTERFACE OLED ====================

void desenharCabecalho(const char *titulo) {
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(titulo);
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
}

void mostrarTela() {
  if (!oledDisponivel || !atualizarOled) {
    return;
  }

  atualizarOled = false;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  if (tocandoBeethoven) {
    desenharCabecalho("EASTER EGG");

    display.setTextSize(2);
    display.setCursor(10, 20);
    display.println("BEETHOVEN");

    display.setTextSize(1);
    display.setCursor(10, 44);
    display.print("Fur Elise...");
    display.setCursor(10, 55);
    display.print("MODO p/ parar");

    display.display();
    return;
  }

  if (!sistemaAtivo) {
    desenharCabecalho("METRONOMO");

    display.setTextSize(2);
    display.setCursor(27, 23);
    display.println("PARADO");

    display.setTextSize(1);
    display.setCursor(20, 52);
    display.print("Modo selecionado: ");
    display.print(modoAtual);

    display.display();
    return;
  }

  // Modo tradicional
  if (modoAtual == 1) {
    desenharCabecalho("MODO 1 - METRONOMO");

    display.setTextSize(3);
    display.setCursor(bpm < 100 ? 35 : 17, 18);
    display.print(bpm);

    display.setTextSize(1);
    display.setCursor(93, 36);
    display.print("BPM");

    display.setCursor(0, 54);
    display.print("LED + BUZZER");

    display.display();
    return;
  }

  // Modo 2 calibrando
  if (estadoModo2 == MODO2_CALIBRANDO) {
    desenharCabecalho("MODO 2 - CALIBRACAO");

    unsigned long decorrido =
      millis() - inicioCalibracao;

    int restante =
      (TEMPO_CALIBRACAO - decorrido + 999) / 1000;

    restante = constrain(restante, 0, 10);

    display.setTextSize(1);
    display.setCursor(21, 17);
    display.print("Som ambiente");

    display.setTextSize(3);
    display.setCursor(restante == 10 ? 45 : 54, 29);
    display.print(restante);

    display.setTextSize(1);
    display.setCursor(72, 45);
    display.print("seg");

    display.setCursor(14, 55);
    display.print("Nao faca ataques");

    display.display();
    return;
  }

  // Modo 2 detectando
  desenharCabecalho("MODO 2 - JOGO");

  display.setTextSize(1);
  display.setCursor(0, 15);
  display.print("Alvo: ");
  display.print(bpm);
  display.print(" BPM");

  display.setCursor(0, 28);
  display.print("Ataques: ");
  display.print(quantidadeAtaques);
  display.print("/4");

  if (ultimaAcuracia >= 0) {
    display.setTextSize(1);
    display.setCursor(0, 41);
    display.print("Acuracia:");

    display.setTextSize(2);
    display.setCursor(61, 39);
    display.print((int)ultimaAcuracia);
    display.print("%");
  } else {
    display.setCursor(0, 47);
    display.print("Faca 4 ataques!");
  }

  display.display();
}

// ==================== POTENCIÔMETRO ====================

// Lê o potenciômetro a cada 20 ms com filtro passa-baixas (EMA) e exige 4 leituras
// consistentes antes de alterar o BPM exibido, evitando oscilações por ruído do ADC
void atualizarPotenciometro(unsigned long agora) {
  if ((long)(agora - proximaLeituraPot) < 0) {
    return;
  }

  proximaLeituraPot = agora + 20;

  int leitura = analogRead(PIN_POT);

  if (potFiltrado < 0) {
    potFiltrado = leitura;
  }

  potFiltrado = (potFiltrado * 7 + leitura) / 8;

  int novoBpm = map(
    potFiltrado,
    0,
    4095,
    BPM_MINIMO,
    BPM_MAXIMO
  );

  novoBpm = constrain(
    novoBpm,
    BPM_MINIMO,
    BPM_MAXIMO
  );

  if (novoBpm != bpmCandidato) {
    bpmCandidato = novoBpm;
    confirmacoesBpm = 1;
    return;
  }

  if (bpm != bpmCandidato) {
    confirmacoesBpm++;

    if (confirmacoesBpm >= 4) {
      bpm = bpmCandidato;
      confirmacoesBpm = 0;
      atualizarOled = true;

      Serial.print("BPM alvo: ");
      Serial.println(bpm);
    }
  }
}

// ==================== MODO 1 (METRÔNOMO) ====================

void iniciarBatida(unsigned long agora) {
  iniciarPulsoLed(agora);
  tone(PIN_BUZZER, FREQUENCIA_BUZZER);

  proximaBatida =
    agora + (60000UL / bpm);
}

void atualizarModo1(unsigned long agora) {
  if ((long)(agora - proximaBatida) >= 0) {
    iniciarBatida(agora);
  }
}

// ==================== CALIBRAÇÃO DO MICROFONE ====================

void iniciarCalibracao(unsigned long agora) {
  desligarSaidas();
  apagarLedsAcuracia();

  estadoModo2 = MODO2_CALIBRANDO;
  inicioCalibracao = agora;
  ultimoSegundoExibido = -1;

  quantidadeAmostras = 0;
  mediaCalibracao = 0;
  somaQuadrados = 0;

  maiorPicoAmbiente = 0;
  desvioAmbiente = 0;

  quantidadeAtaques = 0;
  ultimaAcuracia = -1;
  detectorArmado = true;

  atualizarOled = true;

  Serial.println();
  Serial.println("CALIBRANDO POR 10 SEGUNDOS...");
}

void finalizarCalibracao() {
  centroMic = mediaCalibracao;

  if (quantidadeAmostras > 1) {
    desvioAmbiente = sqrt(
      somaQuadrados /
      (quantidadeAmostras - 1)
    );
  }

  // O limiar final adota o maior valor entre: 7x desvio padrão, 1.4x pico ambiente ou piso mínimo
  float porDesvio =
    desvioAmbiente * FATOR_DESVIO;

  float porMaximo =
    maiorPicoAmbiente * MARGEM_MAXIMO;

  limiarAtaque = porDesvio;

  if (porMaximo > limiarAtaque) {
    limiarAtaque = porMaximo;
  }

  if (limiarAtaque < LIMIAR_MINIMO) {
    limiarAtaque = LIMIAR_MINIMO;
  }

  estadoModo2 = MODO2_DETECTANDO;
  atualizarOled = true;

  Serial.println("CALIBRACAO CONCLUIDA");

  Serial.print("Desvio: ");
  Serial.println(desvioAmbiente, 1);

  Serial.print("Maior pico: ");
  Serial.println(maiorPicoAmbiente, 1);

  Serial.print("Limiar: ");
  Serial.println(limiarAtaque, 1);

  Serial.println("JOGO INICIADO!");
}

void atualizarCalibracao(unsigned long agora) {
  int leitura = analogRead(PIN_MIC);

  // Atualiza média e soma dos quadrados pelo método de Welford
  quantidadeAmostras++;

  double diferenca =
    leitura - mediaCalibracao;

  mediaCalibracao +=
    diferenca / quantidadeAmostras;

  double diferenca2 =
    leitura - mediaCalibracao;

  somaQuadrados +=
    diferenca * diferenca2;

  unsigned long decorrido =
    agora - inicioCalibracao;

  // Ignora o primeiro segundo ao medir o maior pico (evita ruído de clique do botão)
  if (decorrido >= 1000) {
    float amplitude =
      fabs(leitura - mediaCalibracao);

    if (amplitude > maiorPicoAmbiente) {
      maiorPicoAmbiente = amplitude;
    }
  }

  int segundoAtual = decorrido / 1000;

  if (segundoAtual != ultimoSegundoExibido) {
    ultimoSegundoExibido = segundoAtual;
    atualizarOled = true;
  }

  if (decorrido >= TEMPO_CALIBRACAO) {
    finalizarCalibracao();
  }
}

// ==================== DETECÇÃO DE ATAQUES E ACURÁCIA ====================

// Compara os 3 intervalos entre os 4 ataques contra o intervalo esperado para o BPM alvo
float calcularAcuracia() {
  float intervaloEsperado =
    60000.0 / bpm;

  float erroTotal = 0;

  // Quatro ataques produzem três intervalos consecutivos
  for (int i = 1; i < 4; i++) {
    float intervaloReal =
      temposAtaques[i] -
      temposAtaques[i - 1];

    float erro =
      fabs(intervaloReal - intervaloEsperado);

    erroTotal +=
      erro / intervaloEsperado;
  }

  float erroMedio = erroTotal / 3.0;

  float acuracia =
    100.0 * (1.0 - erroMedio);

  return constrain(acuracia, 0.0, 100.0);
}

void registrarAtaque(unsigned long agora, float forca) {
  iniciarPulsoLed(agora);

  temposAtaques[quantidadeAtaques] = agora;
  quantidadeAtaques++;

  Serial.print("Ataque ");
  Serial.print(quantidadeAtaques);
  Serial.print("/4 | Forca: ");
  Serial.println((int)forca);

  if (quantidadeAtaques >= 4) {
    ultimaAcuracia = calcularAcuracia();

    // Mantém o resultado aceso nos LEDs até a próxima acurácia calculada
    mostrarAcuraciaNosLeds(ultimaAcuracia);

    Serial.print("ACURACIA: ");
    Serial.print(ultimaAcuracia, 1);
    Serial.println("%");

    // Reinicia a contagem para o próximo compasso de 4 ataques
    quantidadeAtaques = 0;
  }

  atualizarOled = true;
}

// Detector com histerese (45% para rearmar) e tempo morto para evitar duplos disparos
void atualizarDetector(unsigned long agora) {
  int leitura = analogRead(PIN_MIC);

  // Corrige lentamente desvios no centro DC do sinal do microfone
  centroMic +=
    0.002 * (leitura - centroMic);

  float amplitude =
    fabs(leitura - centroMic);

  float limiarRearme =
    limiarAtaque * 0.45;

  if (
    !detectorArmado &&
    amplitude < limiarRearme
  ) {
    detectorArmado = true;
  }

  if (
    detectorArmado &&
    amplitude >= limiarAtaque &&
    agora - ultimoAtaque >= TEMPO_MORTO_ATAQUE
  ) {
    detectorArmado = false;
    ultimoAtaque = agora;

    registrarAtaque(agora, amplitude);
  }
}

void atualizarModo2(unsigned long agora) {
  desligarBuzzer();

  if (estadoModo2 == MODO2_CALIBRANDO) {
    atualizarCalibracao(agora);
  } else if (estadoModo2 == MODO2_DETECTANDO) {
    atualizarDetector(agora);
  }
}

// ==================== AÇÕES DOS BOTÕES ====================

void alternarStartStop(unsigned long agora) {
  sistemaAtivo = !sistemaAtivo;

  desligarSaidas();

  if (sistemaAtivo) {
    if (modoAtual == 1) {
      proximaBatida = agora;
    } else {
      iniciarCalibracao(agora);
    }

    Serial.println("SISTEMA: ATIVO");
  } else {
    estadoModo2 = MODO2_PARADO;
    Serial.println("SISTEMA: PARADO");
  }

  atualizarOled = true;
}

void alternarModo(unsigned long agora) {
  desligarSaidas();

  if (modoAtual == 1) {
    modoAtual = 2;

    if (sistemaAtivo) {
      iniciarCalibracao(agora);
    }

    Serial.println("MODO 2: JOGO");
  } else {
    modoAtual = 1;
    estadoModo2 = MODO2_PARADO;
    proximaBatida = agora;

    Serial.println("MODO 1: METRONOMO");
  }

  atualizarOled = true;
}

// Gerencia clique normal de troca de modo e janela deslizante de 5 cliques para o Easter Egg
void tratarCliqueModo(unsigned long agora) {
  if (tocandoBeethoven) {
    pararBeethoven(agora);
    if (sistemaAtivo && modoAtual == 2) {
      iniciarCalibracao(agora);
    }
    return;
  }

  // Mantém histórico deslizante dos horários dos últimos 5 cliques
  if (totalCliquesModo < CLIQUES_BEETHOVEN) {
    historicoCliquesModo[totalCliquesModo++] = agora;
  } else {
    for (int i = 1; i < CLIQUES_BEETHOVEN; i++) {
      historicoCliquesModo[i - 1] = historicoCliquesModo[i];
    }
    historicoCliquesModo[CLIQUES_BEETHOVEN - 1] = agora;
  }

  // Se 5 cliques ocorrerem dentro da janela de 3 segundos, inicia Beethoven
  if (
    totalCliquesModo == CLIQUES_BEETHOVEN &&
    (agora - historicoCliquesModo[0]) < JANELA_BEETHOVEN_MS
  ) {
    iniciarBeethoven(agora);
    return;
  }

  alternarModo(agora);
}

// ==================== SETUP ====================

void setup() {
  Serial.begin(115200);

  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_AZUL, OUTPUT);
  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_AMARELO, OUTPUT);
  pinMode(PIN_LED_VERMELHO, OUTPUT);

  pinMode(PIN_START_STOP, INPUT_PULLUP);
  pinMode(PIN_MODO, INPUT_PULLUP);
  pinMode(PIN_MIC, INPUT);

  analogReadResolution(12);

  // Inicializa I2C explicitamente nos pinos SDA=22 e SCL=21
  Wire.begin(PIN_SDA, PIN_SCL);

  oledDisponivel = display.begin(
    SSD1306_SWITCHCAPVCC,
    0x3C
  );

  apagarLedsAcuracia();
  desligarSaidas();

  int leituraInicial = analogRead(PIN_POT);

  potFiltrado = leituraInicial;

  bpm = map(
    leituraInicial,
    0,
    4095,
    BPM_MINIMO,
    BPM_MAXIMO
  );

  bpm = constrain(
    bpm,
    BPM_MINIMO,
    BPM_MAXIMO
  );

  bpmCandidato = bpm;
  proximaBatida = millis();

  Serial.println();
  Serial.println("METRONOMO INTELIGENTE");
  Serial.print("BPM inicial: ");
  Serial.println(bpm);

  atualizarOled = true;
}

// ==================== LOOP PRINCIPAL (NÃO-BLOQUEANTE) ====================

void loop() {
  unsigned long agora = millis();

  if (botaoModo.foiPressionado(agora)) {
    tratarCliqueModo(agora);
  }

  // Enquanto o Easter Egg estiver ativo, pausa metrônomo/jogo e prioriza a melodia
  if (tocandoBeethoven) {
    atualizarBeethoven(agora);
    mostrarTela();
    return;
  }

  atualizarPotenciometro(agora);

  if (botaoStartStop.foiPressionado(agora)) {
    alternarStartStop(agora);
  }

  atualizarFimPulso(agora);

  if (sistemaAtivo) {
    if (modoAtual == 1) {
      atualizarModo1(agora);
    } else {
      atualizarModo2(agora);
    }
  }

  mostrarTela();
}
