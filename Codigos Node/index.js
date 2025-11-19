const net = require('net')

// IP e porta da balança
const BALANCA_IP = '10.128.32.144'
const BALANCA_PORTA = 9000

const client = new net.Socket()

client.connect(BALANCA_PORTA, BALANCA_IP, () => {
  console.log('Conectado à balança')
  // Se a balança não estiver em modo contínuo, envie um comando:
  // client.write('P\r')  // ou outro comando, dependendo do protocolo
})

client.on('data', (data) => {
  const resposta = data.toString().trim()
  console.log( resposta)
  // Se quiser desconectar após uma leitura:
  // client.destroy()
})

client.on('close', () => {
  console.log('Conexão encerrada')
})

client.on('error', (err) => {
  console.error('Erro na conexão:', err.message)
})
