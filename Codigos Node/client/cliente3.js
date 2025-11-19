// cliente1.js
const net = require('net');
const client = new net.Socket();
client.connect(9000, '10.128.32.12', () => {
    console.log('Cliente 3 conectado!');
});
client.on('data', (data) => {
    console.log('Cliente 3 recebeu:', data.toString().trim());
});
client.on('close', () => console.log('Cliente 3 desconectado.'));
client.on('error', (err) => console.error('Cliente 3 erro:', err.message));


