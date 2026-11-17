# Edihub Totem Escolar — Painel IoT Interativo de Editais

Dispositivo físico baseado em **Edge Computing** desenvolvido para alertar estudantes sobre prazos de vestibulares e concursos militares em tempo real, além de monitorar o engajamento presencial.

## Integrantes

| Nome | RM |
| :--- | :--- |
| Henrique Nunes Mororó | 574073 |
| Bernardo de Paula Rodrigues | 572376 |
| Arthur Moreira Costa | 571532 |

## Arquitetura da Solução

O sistema opera na borda (ESP32), processando dados localmente e enviando apenas telemetria resumida para a nuvem:

* **Entrada de Dados (API Simulada):** Arquivo `db.json` hospedado no GitHub (requisições `HTTPS GET` a cada 10 minutos). Caso haja queda de conexão, o dispositivo assume automaticamente uma lista de contingência offline gravada na memória.
* **Processamento na Borda (ESP32):** Sincroniza o horário via NTP, calcula os dias/horas restantes para cada certame, filtra o evento mais crítico da categoria selecionada e controla os atuadores e alertas locais.
* **Telemetria em Nuvem (ThingSpeak):** Envio de dados via `HTTP GET` a cada 20 segundos contendo:
  * **Field 1:** Contagem diária de aproximações de alunos.
  * **Field 2:** ID do último certame consultado.
  * **Field 3:** Nível de luminosidade ambiente (0 a 100%).

## Hardware e Pinagem (Simulação Wokwi)

| Componente | Função | Pino ESP32 | Observação |
| :--- | :--- | :--- | :--- |
| **Display OLED SSD1306** | Exibição do certame, prazo e status | `GPIO 21` (SDA) / `GPIO 22` (SCL) | Interface I2C (`0x3C`) |
| **Sensor HC-SR04** | Detecção de presença (< 100 cm) | `GPIO 5` (TRIG) / `GPIO 18` (ECHO) | Alimentação 5V |
| **Sensor LDR** | Medição de luz ambiente (ajuste de brilho) | `GPIO 34` (AO) | Canal ADC1 (compatível com Wi-Fi) |
| **LED RGB (Cátodo Comum)** | Status visual de urgência | `GPIO 25` (R), `26` (G), `27` (B) | Resistores de 220 Ω em cada canal |
| **Buzzer** | Alerta sonoro para prazos < 24h | `GPIO 13` | Acionado apenas com presença detectada |
| **Pushbutton** | Alternância entre `VESTIBULAR` e `MILITAR` | `GPIO 4` | Configurado com `INPUT_PULLUP` |

### Lógica de Sinalização Visual (LED RGB)
* **Verde:** Inscrições abertas (prazo superior a 24 horas).
* **Vermelho Piscando:** Alerta crítico (últimas 24 horas para o encerramento).
* **Azul:** Sem dados disponíveis ou relógio NTP ainda não sincronizado.

## Como Executar

1. **API Simulada:** Hospede o arquivo `db.json` em um repositório público no GitHub e copie a URL **Raw**.
2. **ThingSpeak:** Crie um canal com 3 campos habilitados e copie a **Write API Key**.
3. **Wokwi:**
   * Crie um projeto **ESP32 (Arduino)**.
   * Adicione as bibliotecas no `libraries.txt`: `Adafruit SSD1306`, `Adafruit GFX Library` e `ArduinoJson`.
   * Importe o circuito (`diagram.json`) e o código-fonte (`sketch.ino`).
   * Insira a `URL_API` e a `THINGSPEAK_KEY` nas constantes de configuração no topo do `sketch.ino`.
   * Inicie a simulação.

## Impacto Social (ODS)

Projeto alinhado aos Objetivos de Desenvolvimento Sustentável da ONU **ODS 4** (Educação de Qualidade) e **ODS 10** (Redução das Desigualdades), democratizando o acesso a datas de isenção e inscrição de processos seletivos para estudantes sem acesso contínuo a dispositivos pessoais ou internet.
