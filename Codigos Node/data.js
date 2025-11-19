// Importa os módulos necessários
const net = require('net');   // Para a conexão de rede TCP
const fs = require('fs');     // Para gravar no arquivo CSV
const path = require('path'); // Para lidar com caminhos de arquivos

// --- CONFIGURAÇÕES ---
const ESP32_IP = '10.128.32.178';         // <<<--- VERIFIQUE E COLOQUE O IP CORRETO DO SEU ESP32 AQUI
const ESP32_PORTA = 9000;                // Porta TCP do ESP32
const NOME_ARQUIVO_CSV = 'log_bruto.csv'; // Nome do arquivo de log
// --------------------

const caminhoArquivo = path.join(__dirname, NOME_ARQUIVO_CSV);
const client = new net.Socket();

// Função para formatar números com zero à esquerda (para a hora)
function pad(num) {
  return num < 10 ? '0' + num : num.toString();
}

// Cria o arquivo CSV com cabeçalho se ele não existir
if (!fs.existsSync(caminhoArquivo)) {
  console.log(`[INFO] Arquivo ${NOME_ARQUIVO_CSV} não encontrado. Criando com cabeçalho...`);
  const cabecalho = "Data;Hora;DadoBruto;IP_Conexao\n"; // Usando ';' como separador para Excel
  fs.writeFileSync(caminhoArquivo, cabecalho, 'utf8');
}

// Tenta se conectar ao ESP32
client.connect(ESP32_PORTA, ESP32_IP, () => {
  console.log(`[REDE] Conectado com sucesso ao ESP32 em ${ESP32_IP}`);
});

// Evento: Recebimento de dados do ESP32
client.on('data', (data) => {
  // Converte os dados brutos para uma string e remove espaços extras no início/fim
  const dadoBruto = data.toString().trim();

  // Apenas exibe no console e grava, sem filtros
  console.log('Dado Recebido:', dadoBruto);

  // Pega a data e hora atuais
  const agora = new Date();
  const dataFormatada = agora.toLocaleDateString('pt-BR');
  const horaFormatada = agora.toLocaleTimeString('pt-BR');
  const ipConexao = client.remoteAddress;

  // Monta a linha do CSV
  const linhaCsv = `${dataFormatada};${horaFormatada};"${dadoBruto}";${ipConexao}\n`;

  // Adiciona a linha ao final do arquivo CSV
  fs.appendFile(caminhoArquivo, linhaCsv, 'utf8', (err) => {
    if (err) {
      console.error('❌ Erro ao salvar no CSV:', err.message);
    }
  });
});

// Evento: Conexão encerrada
client.on('close', () => {
  console.log('[REDE] Conexão encerrada. Tentando reconectar em 5 segundos...');
  // Tenta reconectar após 5 segundos
  setTimeout(() => {
    client.connect(ESP32_PORTA, ESP32_IP);
  }, 5000);
});

// Evento: Erro na conexão
client.on('error', (err) => {
  console.error(`[REDE] Erro de conexão: ${err.message}.`);
  // O evento 'close' será acionado em seguida para tentar a reconexão
});