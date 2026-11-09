'use strict';

const http = require('node:http');
const { createRequestHandler } = require('./routes');

const port = Number.parseInt(process.env.PORT || '3000', 10);
const host = process.env.HOST || '0.0.0.0';
const server = http.createServer(createRequestHandler());

server.listen(port, host, () => {
  console.log(`Webhook archive service listening on http://${host}:${port}`);
});

function shutdown(signal) {
  console.log(`Received ${signal}; closing HTTP server`);
  server.close((error) => {
    process.exitCode = error ? 1 : 0;
  });
  setTimeout(() => {
    process.exitCode = 1;
    server.closeAllConnections();
  }, 10_000).unref();
}

process.once('SIGINT', () => shutdown('SIGINT'));
process.once('SIGTERM', () => shutdown('SIGTERM'));

module.exports = server;
