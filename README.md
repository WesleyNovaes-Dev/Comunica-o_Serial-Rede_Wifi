# Gateway IoT: Comunicação Serial/USB em Rede para Balanças Industriais

Este projeto consiste em um **Gateway IoT de baixo custo** desenvolvido para conectar balanças industriais (via RS-232/USB) diretamente à rede corporativa (Wi-Fi). O sistema utiliza um **ESP32** para capturar dados de pesagem, filtrar redundâncias e transmiti-los via TCP para um servidor ou sistema ERP, automatizando o processo de coleta de dados.

Inclui validação de backend utilizando **Node.js** para registro de pesagens em arquivo CSV.

---

## 📑 Índice
1. [Visão Geral do Projeto](#visão-geral-do-projeto)
2. [Hardware e Requisitos](#hardware-e-requisitos)
3. [Funcionamento do Firmware](#funcionamento-do-firmware)
4. [Instalação e Configuração](#instalação-e-configuração)
5. [Tutorial de Uso (Backend)](#tutorial-de-uso-backend)
6. [Estrutura de Dados e Protocolos](#estrutura-de-dados-e-protocolos)
7. [Resultados e Performance](#resultados-e-performance)
8. [Parâmetros Configuráveis](#parâmetros-configuráveis)

---

## 🔭 Visão Geral do Projeto

O objetivo principal é eliminar a coleta manual de dados de balanças que possuem apenas interfaces locais (Serial DB9 ou USB). O ESP32 atua como uma ponte transparente e inteligente.

* **Núcleo:** ESP32 (Dual Core, Wi-Fi integrado).
* **Interface:** Leitura Serial RS-232 convertida para TTL.
* **Conectividade:** Suporte a redes WPA2-Pessoal e **WPA2-Enterprise** (Corporativo).
* **Eficiência:** Algoritmo de "Line Change Detection" que reduz o tráfego de rede ao enviar apenas alterações de peso.
* **Resiliência:** Gerenciamento automático de reconexão Wi-Fi sem perda de estado.

---

## 🛠 Hardware e Requisitos

### Lista de Componentes
* **Microcontrolador:** ESP32 DevKitC V4 (ou similar).
* **Conversor de Nível:** Módulo MAX3232 (RS232 ↔ TTL). Necessário para converter os sinais de ±12V da balança para os 3.3V do ESP32.
* **Fonte de Alimentação:** 5V (para o MAX3232) e 3.3V (para o ESP32) ou fonte USB comum.
* **Cabeamento:** Conector DB9 (fêmea/macho conforme a balança) e Jumpers.
* **Balança:** Qualquer balança industrial com saída serial (Ex: Toledo Prix).

### Diagrama de Conexão (Pinagem)

Abaixo, a tabela de conexão entre a balança, o conversor e o ESP32:

| Componente Origem | Pino Origem | Componente Destino | Pino Destino (ESP32) | Descrição |
| :--- | :--- | :--- | :--- | :--- |
| **Balança (DB9)** | TX (Transmissão) | **MAX3232** | RX (Entrada RS232) | Sinal vindo da balança |
| **Balança (DB9)** | GND | **MAX3232** | GND | Terra comum |
| **MAX3232** | VCC | **Fonte** | 3.3V ou 5V | Alimentação do módulo |
| **MAX3232** | GND | **ESP32** | GND | Terra comum |
| **MAX3232** | TX (Saída TTL) | **ESP32** | **GPIO 16 (RX2)** | Entrada de dados no MCU |
| **MAX3232** | RX (Entrada TTL) | **ESP32** | **GPIO 17 (TX2)** | Envio de comandos (se houver) |

---

## 🧠 Funcionamento do Firmware

O firmware foi desenvolvido em C++ (Arduino IDE) e opera em uma máquina de estados para garantir estabilidade:

1.  **Inicialização:** Tenta conectar ao último Wi-Fi salvo na memória não volátil.
2.  **Modo AP (Falha de Conexão):** Se não conseguir conectar, cria o Ponto de Acesso **"ESP32_Config"**.
    * Interface Web disponível em `http://192.168.4.1`.
    * Permite configurar SSID, Senha, Usuário (Enterprise), IP Estático/DHCP.
3.  **Modo Operacional (Conectado):**
    * Monitora a porta `Serial2`.
    * **Filtragem:** Aplica o algoritmo `LineChangeDetector`. Se o peso lido for idêntico ao anterior, o dado é descartado. Se mudar, é processado.
    * **Servidor TCP:** Escuta na porta **9000**.
    * **Transmissão:** Envia o dado filtrado para todos os clientes conectados.
4.  **Falha de Rede:** Se o Wi-Fi cair, o envio TCP para imediatamente (evita travamento) e o LED pisca até a reconexão automática.

---

## ⚙️ Instalação e Configuração

### Passo 1: Preparar o Firmware
1.  Instale o **Arduino IDE** e as bibliotecas do ESP32.
2.  Abra o código fonte.
3.  Verifique a linha de inicialização da serial:
    ```cpp
    Serial2.begin(9600, SERIAL_8N1, 16, 17); // Ajuste 9600 conforme sua balança
    ```
4.  Compile e faça o upload para a placa.

### Passo 2: Configuração via Interface Web
No primeiro uso (ou se mudar de rede):
1.  Conecte seu computador/celular à rede Wi-Fi: `ESP32_Config`.
2.  Abra o navegador e acesse: **http://192.168.4.1**.
3.  Preencha os campos:
    * **SSID/Senha:** Da sua rede local.
    * **Modo IP:** DHCP (automático) ou Estático (recomendado para servidores).
    * **Admin Password:** Defina uma senha para proteger esta tela.
4.  Clique em **Salvar**. O ESP32 irá reiniciar.
5.  Observe o Monitor Serial (ou verifique no roteador) o IP atribuído (ex: `192.168.0.105`).

---

## 💻 Tutorial de Uso (Backend)

Para validar o recebimento dos dados, utilizamos um script em **Node.js**.

### Pré-requisitos
* Node.js v10 ou superior instalado.

### Execução
1.  Baixe o arquivo `coleta_de_dados.js` deste repositório.
2.  Edite o arquivo para apontar para o IP do seu ESP32:
    ```javascript
    const HOST = '192.168.0.105'; // Coloque o IP do ESP32 aqui
    const PORT = 9000;
    ```
3.  Abra o terminal na pasta do arquivo e execute:
    ```bash
    node coleta_de_dados.js
    ```
4.  **Resultado:** O script criará o arquivo `pesagens.csv` e começará a popular com os dados recebidos em tempo real.

---

## 📊 Estrutura de Dados e Protocolos

### 1. Dados Brutos (Origem: Balança)
Exemplo de string típica enviada por balanças (ex: Toledo):
* `I` = Instável (Peso variando)
* `E` = Estável (Peso fixo)

```text
I00.005
I00.015
E00.060