Título: Metrônomo Inteligente
Integrantes: Arthur Viana, Lorenzo Tottola & Maurício Gomez
 
Resumo:
Metrônomo inteligente com Arduino Uno R3 capaz de gerar BPM configurável, captar ataques de instrumentos por microfone e indicar por LED se a execução ocorreu antes, no tempo ou depois da batida esperada.
 
Descrição detalhada:
O projeto consiste em um metrônomo embarcado com Arduino Uno R3. O usuário configura o BPM e o sistema gera a referência rítmica por buzzer. Ao mesmo tempo, um microfone MAX4466 capta o som do instrumento e o Arduino detecta os ataques sonoros. Cada ataque é comparado com o instante esperado da batida, classificando a execução como adiantada, correta ou atrasada. O resultado é mostrado por LED RGB. Inicialmente o foco será em instrumentos com ataques bem definidos, como percussão, podendo ser testado também com cordas de ataque claro.
 
Lista de componentes:
Arduino Uno R3; microfone MAX4466; LED RGB; resistores; buzzer passivo; botões; potenciômetro de 10 kΩ; protoboard; jumpers; display OLED SSD1306 opcional.
 
Esquemático simplificado: https://drive.google.com/file/d/1DMHSrcoCYn2P-NMMVuYoiNboP_MKyjt_/view?usp=drivesdk

CRONOGRAMA — ÉPICOS E QUINZENAS
ÉPICO 1 — DESENVOLVIMENTO DO METRÔNOMO
Quinzena 1 — Construção do Metrônomo
Objetivo: Desenvolver a primeira versão funcional do projeto, já contendo o metrônomo capaz de gerar batidas em BPM configurável.
Entregável: Primeira versão funcional do metrônomo, com Arduino, buzzer, LED, botões e potenciômetro, permitindo configurar e reproduzir diferentes BPMs.
KPIs: BPM funcionando entre 40 e 200; funcionamento contínuo por pelo menos 5 minutos.
Quinzena 2 — Testes, Melhorias e Captação
Objetivo: Testar e melhorar o metrônomo e iniciar a integração do microfone para a captação dos ataques sonoros. Implementar calibração automática do microfone (detecta o ruído do ambiente)


Entregável: Metrônomo aprimorado com o microfone MAX4466 integrado ao Arduino e capaz de realizar a primeira detecção de ataques.
KPIs: Erro de temporização menor que 20 ms; detecção inicial dos ataques funcionando em compassos de 4/4.

ÉPICO 2 — DETECÇÃO RÍTMICA E DESIGN DO PROJETO
Quinzena 3 — Detecção e Comparação dos Ataques
Objetivo: Desenvolver o sistema de análise dos ataques e sua comparação com as batidas esperadas pelo metrônomo.
Entregável: Sistema capaz de detectar os ataques pelo microfone e indicar, por meio do LED RGB, se o instrumento foi tocado adiantado, no tempo ou atrasado.
KPIs: Pelo menos 80% de reconhecimento correto com ataques de percussão em compassos diferentes (4/4, 3/4, 7/8 etc) - medição de compasso funcional.
Quinzena 4 — Design e Integração do Projeto
Objetivo: Transformar o protótipo em um projeto mais organizado e próximo da versão final, pensando na disposição dos componentes, estrutura física e interação com o usuário.
Entregável: Protótipo com componentes organizados fora da configuração inicial de protoboard, definição do design físico do dispositivo e integração dos principais componentes, incluindo testes com percussão e instrumento de corda com ataque claro.
KPIs: Dois tipos de fonte sonora funcionando; componentes integrados em uma estrutura organizada e funcional.

ÉPICO 3 — APRIMORAMENTO E ENTREGA FINAL
Quinzena 5 — Documentação, Otimização e Apresentação
Objetivo: Finalizar o projeto, aprimorar o código e preparar a documentação e apresentação final.
Entregável: Versão final do metrônomo inteligente, com código revisado, melhorias de funcionamento, documentação completa e demonstração preparada.
KPIs: Sistema funcionando de forma estável por 10 minutos; 100 testes registrados e documentados.


