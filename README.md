# Gateway IoT: Comunicação Serial/USB em Rede para Balanças Industriais

> **Projeto de Conclusão de Curso - Engenharia da Computação (Facens)**
> **Autores:** Johanna Bernecker, Pedro Henrique Garcia Silveira, Wesley Davi Zanon Novaes.

![Badge ESP32](https://img.shields.io/badge/Hardware-ESP32-red) ![Badge NodeJS](https://img.shields.io/badge/Backend-Node.js-green) ![Badge Protocol](https://img.shields.io/badge/Protocol-TCP%2FIP-blue)

Este projeto apresenta um **Gateway IoT de baixo custo** desenvolvido para conectar balanças industriais (interface RS-232/USB) à rede corporativa (Wi-Fi). O sistema utiliza um microcontrolador ESP32 para capturar dados de pesagem, filtrar redundâncias e transmiti-los via TCP para um servidor ou sistema ERP, eliminando a necessidade de apontamentos manuais.

---

## 📑 Índice

1. [Visão Geral e Objetivos](#-visão-geral-e-objetivos)
2. [Arquitetura de Hardware](#-arquitetura-de-hardware)
3. [Funcionamento do Sistema](#-funcionamento-do-sistema)
4. [Instalação e Configuração](#-instalação-e-configuração)
5. [Resultados e Performance](#-resultados-e-performance)
6. [Viabilidade Econômica](#-viabilidade-econômica)

---

## 🔭 Visão Geral e Objetivos

O objetivo principal é modernizar balanças legadas que possuem apenas interfaces locais (Serial DB9 ou USB), integrando-as à **Indústria 4.0** sem o alto custo de substituição do equipamento.

**Principais Funcionalidades:**
* **Conectividade Universal:** Suporte a redes WPA2-Pessoal e **WPA2-Enterprise** (Corporativo).
* **Configuração Web:** Interface embarcada (SoftAP) para configuração de Wi-Fi e IP (DHCP ou Estático).
* **Otimização de Dados:** Algoritmo *LineChangeDetector* que reduz o tráfego de rede em 90% ao enviar apenas alterações de peso.
* **Resiliência:** Reconexão automática em caso de falha de rede sem perda de pacotes.

---

## 🛠 Arquitetura de Hardware

O projeto foi validado utilizando a balança **Toledo Prix 9094 Plus** e o seguinte hardware:

| Componente | Função |
| :--- | :--- |
| **ESP32 DevKitC V4** | Núcleo de processamento e conectividade Wi-Fi. |
| **Módulo MAX3232** | Conversor de níveis de tensão RS232 (±12V) para TTL (3.3V). |
| **Fonte de Alimentação** | Fonte externa 5V/3.3V para estabilidade do circuito. |
| **Conector DB9** | Interface física com a balança. |

### Diagrama de Conexões (Pinout)

As conexões entre o módulo conversor e o ESP32 utilizam a porta `Serial2`:

| Pino ESP32 | Função | Conexão no MAX3232 |
| :--- | :--- | :--- |
| **GPIO 16 (RX2)** | Receber Dados (RX) | Pino TX (TTL) |
| **GPIO 17 (TX2)** | Transmitir Dados (TX) | Pino RX (TTL) |
| **GND** | Aterramento | GND |
| **VCC (3.3V)** | Alimentação | VCC |

---

## 🧠 Funcionamento do Sistema

O firmware opera em uma máquina de estados:

1.  **Inicialização:** Tenta conectar ao último Wi-Fi salvo.
2.  **Modo AP (Configuração):** Se falhar, cria a rede `ESP32_Config` (IP 192.168.4.1) para configuração via navegador.
3.  **Modo Operação:**
    * Lê a porta Serial RS-232.
    * Aplica o filtro de dados repetidos.
    * Abre um Servidor TCP na porta **9000**.
    * Transmite dados limpos para o backend (Node.js).

---

## ⚙️ Instalação e Configuração

### 1. Firmware (ESP32)
1.  Abra o projeto no **Arduino IDE**.
2.  Certifique-se de que as bibliotecas `WiFi.h`, `Preferences.h` e `WebServer.h` estão instaladas.
3.  Ajuste o *Baud Rate* da serial conforme sua balança (ex: 9600 ou 115200) no arquivo principal:
    ```cpp
    SerialRS232.begin(115200, SERIAL_8N1, RS232_RX, RS232_TX); //
    ```
4.  Compile e carregue na placa.

### 2. Configuração de Rede
1.  Conecte-se à rede Wi-Fi **ESP32_Config**.
2.  Acesse `http://192.168.4.1`.
3.  Configure o SSID, Senha e escolha entre **DHCP** ou **IP Estático**.
4.  O dispositivo reiniciará e mostrará o IP obtido no Monitor Serial.

### 3. Backend (Node.js)
Para capturar os dados no computador/servidor:
1.  Instale o Node.js.
2.  Configure o IP do ESP32 no script `coleta_de_dados.js`:
    ```javascript
    const BALANCA_IP = '10.128.32.8'; 
    const BALANCA_PORTA = 9000;
    ```
3.  Execute o script: `node coleta_de_dados.js`.
4.  Os dados serão salvos automaticamente no arquivo `pesagens.csv`.

---

## 📊 Resultados e Performance

### Otimização de Tráfego
O sistema implementa filtragem inteligente. A tabela abaixo (baseada nos testes do TCC) demonstra que dados repetidos (balança estável) não consomem banda de rede.

| Estado | Dado Bruto | Ação do Gateway | Resultado |
| :--- | :--- | :--- | :--- |
| Instável | `I00.005` | Envia | Dado registrado no servidor |
| Instável | `I00.035` | Envia | Dado registrado no servidor |
| **Estável** | `E00.060` | **Envia** | Dado registrado (Peso Final) |
| **Estável** | `E00.060` | **Filtra** | **Nenhum pacote enviado (Economia)** |

### Latência
A latência média medida entre a leitura do peso e o registro no servidor foi de **~48ms**, viabilizando aplicações em tempo real.

| Métrica | Valor Médio |
| :--- | :--- |
| Latência Média | 48.75 ms |
| Perda de Pacotes | 0% |
| Tempo máx. sem falhas | 3h contínuas |

---

## 💰 Viabilidade Econômica

Um dos maiores diferenciais do projeto é o custo reduzido em comparação com a modernização oferecida pelos fabricantes de balanças.

| Solução | Custo Estimado (R$) | Descrição |
| :--- | :--- | :--- |
| **Gateway IoT (Este Projeto)** | **R$ 120,68** | Solução flexível, código aberto e Wi-Fi. |
| Modernização Comercial (Ethernet) | R$ 1.990,10 | Kit proprietário do fabricante. |
| **Economia** | **~93%**  |

---
*Trabalho desenvolvido no Centro Universitário Facens, Sorocaba/SP - 2025.