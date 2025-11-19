// latency_server.js
// Servidor Node.js para conectar ao ESP32, receber dados e calcular estatísticas de latência.

const net = require('net'); // Módulo nativo do Node.js para conexões TCP

// --- CONFIGURAÇÃO (MODIFIQUE ESTES VALORES) ---
const TARGET_IP = '10.128.32.12'; // <-- COLOQUE O IP REAL DO SEU ESP32 AQUI!
const TARGET_PORT = 9001;            // Porta configurada no ESP32 (dataServer)
const MAX_SAMPLES = 20;              // Número de amostras a serem coletadas para as estatísticas
// ---------------------------------------------

const latencies = []; // Array para armazenar cada valor de latência medido
let buffer = '';      // Buffer para lidar com pacotes de dados que chegam quebrados

/**
 * Função para calcular estatísticas a partir de um array de números.
 * @param {number[]} arr O array com os valores de latência.
 * @returns {object} Um objeto contendo min, max, avg (média) e std (desvio padrão).
 */
function calculateStats(arr) {
    if (arr.length === 0) return { min: 0, max: 0, avg: 0, std: 0 };
    
    const sum = arr.reduce((a, b) => a + b, 0);
    const avg = sum / arr.length;
    const min = Math.min(...arr);
    const max = Math.max(...arr);
    
    const variance = arr.map(x => Math.pow(x - avg, 2)).reduce((a, b) => a + b, 0) / arr.length;
    const std = Math.sqrt(variance);
    
    return { 
        min: min.toFixed(2), 
        max: max.toFixed(2), 
        avg: avg.toFixed(2), 
        std: std.toFixed(2) 
    };
}

// Cria um novo cliente TCP
const client = new net.Socket();

// Tenta se conectar ao ESP32
client.connect(TARGET_PORT, TARGET_IP, () => {
    console.log(`[CONEXÃO] Sucesso! Conectado ao ESP32 em ${TARGET_IP}:${TARGET_PORT}`);
    console.log(`[MEDIÇÃO] Coletando ${MAX_SAMPLES} amostras de latência. Por favor, gere dados na balança...`);
});

// Evento disparado sempre que chegam novos dados do ESP32
client.on('data', (data) => {
    buffer += data.toString(); // Adiciona os dados recebidos ao buffer

    let newlineIndex;
    // Processa o buffer linha por linha (pois os dados podem chegar "quebrados")
    while ((newlineIndex = buffer.indexOf('\n')) !== -1) {
        const line = buffer.substring(0, newlineIndex).trim();
        buffer = buffer.substring(newlineIndex + 1);

        if (line.length > 0) {
            try {
                // PASSO 1: Marca o tempo exato de chegada da mensagem
                const receivedTime = Date.now(); 

                // PASSO 2: Interpreta a string JSON recebida
                const payload = JSON.parse(line);

                // Se o payload tiver o timestamp e ainda não coletamos todas as amostras
                if (payload.timestamp_ms && latencies.length < MAX_SAMPLES) {
                    
                    // PASSO 3: Calcula a latência
                    // (Tempo de Chegada no Servidor) - (Tempo de Saída do ESP32)
                    const latency = receivedTime - payload.timestamp_ms;
                    
                    latencies.push(latency);
                    
                    // PASSO 4: Exibe o valor da latência para esta amostra
                    console.log(`> Amostra ${latencies.length}/${MAX_SAMPLES} | Latência: ${latency} ms`);

                    // Se a coleta terminou, exibe as estatísticas finais
                    if (latencies.length >= MAX_SAMPLES) {
                        console.log("\n--- COLETA FINALIZADA ---");
                        const stats = calculateStats(latencies);
                        console.log("Estatísticas de Latência (em milissegundos):");
                        console.log(`  - Latência Média:   ${stats.avg} ms`);
                        console.log(`  - Latência Mínima:  ${stats.min} ms`);
                        console.log(`  - Latência Máxima:  ${stats.max} ms`);
                        console.log(`  - Desvio Padrão:    ${stats.std} ms`);
                        
                        client.destroy(); // Encerra a conexão
                    }
                }
            } catch (e) {
                console.error('[ERRO] Não foi possível processar a linha recebida. Verifique o formato do JSON. Linha:', line);
            }
        }
    }
});

// Evento para quando a conexão é fechada
client.on('close', () => {
    console.log('[CONEXÃO] Conexão encerrada.');
    process.exit(0); // Garante que o script termine
});

// Evento para lidar com erros de conexão
client.on('error', (err) => {
    console.error(`[ERRO DE CONEXÃO] Não foi possível conectar ao ESP32. Verifique o IP (${TARGET_IP}) e se ele está na mesma rede.`, err.message);
});