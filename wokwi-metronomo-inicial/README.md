# Metrônomo Inteligente — simulação inicial no Wokwi

Projeto inicial de validação de hardware para ESP32 DevKitC V4.

## Pinagem

| Função | Arduino | Componente |
|---|---:|---|
| Áudio analógico | GPIO 34 | OUT do MAX4466 / saída do microfone Wokwi |
| Ajuste de BPM | GPIO 35 | Cursor do potenciômetro de 10 kΩ |
| Start/Stop | GPIO 25 | Botão com pull-up externo de 10 kΩ |
| Modo | GPIO 26 | Botão com pull-up externo de 10 kΩ |
| Simular som | GPIO 27 | Botão com pull-up externo de 10 kΩ |
| LED vermelho | GPIO 14 | Resistor de 220 Ω + R do LED RGB |
| LED verde | GPIO 12 | Resistor de 220 Ω + G do LED RGB |
| LED azul | GPIO 13 | Resistor de 220 Ω + B do LED RGB |
| OLED SDA | GPIO 21 | SSD1306, endereço 0x3C |
| OLED SCL | GPIO 22 | SSD1306, endereço 0x3C |
| Buzzer | GPIO 23 | Buzzer passivo |

## Como usar no Wokwi

1. Crie ou importe um projeto ESP32 em <https://wokwi.com/projects/new/esp32>.
2. Substitua o conteúdo de `sketch.ino` e `diagram.json` pelos arquivos desta pasta.
3. Crie `libraries.txt` com as duas bibliotecas indicadas.
4. Inicie a simulação.

Controles:

- Potenciômetro: seleciona de 40 a 200 BPM.
- `S`: ativa ou pausa o modo atual.
- `M`: alterna entre Detecção e Metrônomo.
- `T`: gera um ataque sonoro simulado no modo Detecção, sem usar o microfone do computador.
- Microfone virtual: primeiro deixe o nível baixo por pelo menos 300 ms; depois aumente-o para gerar um único início de ataque. Uma onda contínua não gera ataques repetidos.

O sistema inicia em **Detecção**, com o buzzer silencioso. Nesse modo, o LED verde indica um ataque real do componente virtual ou um ataque gerado pelo botão `T`.

O detector usa duas etapas: dispara quando a envoltória ultrapassa 650 e só rearma depois de permanecer abaixo de 250 por 300 ms. Isso impede que cada oscilação de um som contínuo seja interpretada como um novo ataque.

No modo **Metrônomo**, o potenciômetro seleciona de 40 a 200 BPM, o buzzer emite pulsos curtos e o LED azul acompanha cada pulso. Não há medição ou seleção de compasso nesta versão.

## Diferenças entre a simulação e o circuito físico

- O componente usado na simulação é o `wokwi-microphone`, um microfone analógico genérico com GND e saída. Ele substitui apenas o comportamento de saída do MAX4466.
- No MAX4466 físico com ESP32, alimente o módulo em 3,3 V, ligue GND ao GND comum e OUT ao GPIO 34. Não aplique 5 V a uma entrada ADC do ESP32.
- Para a simulação, o buzzer está diretamente no GPIO 23. No protótipo físico, use o estágio do esquemático: GPIO 23 → resistor de 1 kΩ → base do 2N2222, emissor no GND, coletor no negativo do buzzer e positivo do buzzer em 5 V.
- O OLED é opcional no projeto físico, mas foi incluído na simulação para validar o barramento I2C e mostrar modo, BPM e leitura do microfone.
- A detecção atual usa limiar fixo e serve apenas para testar o caminho de entrada. Calibração automática, compassos e comparação temporal pertencem às próximas quinzenas.
