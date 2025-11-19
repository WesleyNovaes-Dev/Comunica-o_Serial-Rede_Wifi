// cliente2.js
const net = require('net');
const client = new net.Socket();
client.connect(9000, '10.128.32.12', () => {
    console.log('Cliente 2 conectado!');
});
client.on('data', (data) => {
     console.log('Cliente 2 recebeu:', data.toString().trim());
});
client.on('close', () => console.log('Cliente 2 desconectado.'));
client.on('error', (err) => console.error('Cliente 2 erro:', err.message));


