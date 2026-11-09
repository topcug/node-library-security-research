'use strict';

const { constants } = require('node:fs');
const { access, mkdir, readFile, writeFile } = require('node:fs/promises');
const { join } = require('node:path');
const native = require('../build/Release/native_versions.node');

const MAX_BODY_BYTES = 1024 * 1024;
const EVENT_PATH = /^\/events\/([a-f0-9]{64})$/;
const STATIC_METHODS = new Map([
  ['/events', 'POST'],
  ['/versions', 'GET'],
  ['/healthz', 'GET'],
  ['/readyz', 'GET'],
]);

function sendJson(response, statusCode, body, headers = {}) {
  const payload = JSON.stringify(body);
  response.writeHead(statusCode, {
    'content-type': 'application/json; charset=utf-8',
    'content-length': Buffer.byteLength(payload),
    ...headers,
  });
  response.end(payload);
}

async function readEvent(request) {
  const chunks = [];
  let length = 0;

  for await (const chunk of request) {
    length += chunk.length;
    if (length <= MAX_BODY_BYTES) {
      chunks.push(chunk);
    }
  }

  if (length > MAX_BODY_BYTES) {
    const error = new Error('Webhook body exceeds 1 MiB');
    error.statusCode = 413;
    throw error;
  }

  const payload = Buffer.concat(chunks);
  let event;
  try {
    event = JSON.parse(payload.toString('utf8'));
  } catch {
    const error = new Error('Webhook body must be valid JSON');
    error.statusCode = 400;
    throw error;
  }

  if (event === null || typeof event !== 'object' || Array.isArray(event)) {
    const error = new Error('Webhook body must be a JSON object');
    error.statusCode = 400;
    throw error;
  }

  return { event, payload };
}

function createRequestHandler({ archiveDir = process.env.ARCHIVE_DIR || '/data/events' } = {}) {
  async function ensureArchive() {
    await mkdir(archiveDir, { recursive: true });
    await access(archiveDir, constants.R_OK | constants.W_OK);
  }

  return async function requestHandler(request, response) {
    const url = new URL(request.url, 'http://localhost');
    const eventMatch = url.pathname.match(EVENT_PATH);
    const allowedMethod = eventMatch === null ? STATIC_METHODS.get(url.pathname) : 'GET';

    if (allowedMethod === undefined) {
      sendJson(response, 404, { error: 'Not found' });
      return;
    }
    if (request.method !== allowedMethod) {
      sendJson(response, 405, { error: 'Method not allowed' }, { allow: allowedMethod });
      return;
    }

    try {
      if (url.pathname === '/healthz') {
        sendJson(response, 200, { status: 'ok' });
        return;
      }

      if (url.pathname === '/versions') {
        sendJson(response, 200, {
          node: process.version,
          nodeOpenSSL: process.versions.openssl,
          nodeZlib: process.versions.zlib,
          nativeOpenSSL: native.opensslVersion(),
          nativeZlib: native.zlibVersion(),
        });
        return;
      }

      if (url.pathname === '/readyz') {
        await ensureArchive();
        const input = Buffer.from('ready');
        const output = native.decompress(native.compress(input));
        if (!output.equals(input)) {
          throw new Error('Native compression check failed');
        }
        sendJson(response, 200, { status: 'ready' });
        return;
      }

      await ensureArchive();
      if (url.pathname === '/events') {
        const { payload } = await readEvent(request);
        const id = native.sha256(payload);
        const compressed = native.compress(payload);
        let duplicate = false;

        try {
          await writeFile(join(archiveDir, `${id}.zlib`), compressed, {
            flag: 'wx',
            mode: 0o600,
          });
        } catch (error) {
          if (error.code !== 'EEXIST') {
            throw error;
          }
          duplicate = true;
        }

        sendJson(response, duplicate ? 200 : 201, {
          id,
          duplicate,
          originalBytes: payload.length,
          storedBytes: compressed.length,
        });
        return;
      }

      const id = eventMatch[1];
      let compressed;
      try {
        compressed = await readFile(join(archiveDir, `${id}.zlib`));
      } catch (error) {
        if (error.code !== 'ENOENT') {
          throw error;
        }
        sendJson(response, 404, { error: 'Event not found' });
        return;
      }

      const payload = native.decompress(compressed);
      sendJson(response, 200, {
        id,
        event: JSON.parse(payload.toString('utf8')),
        originalBytes: payload.length,
        storedBytes: compressed.length,
      });
    } catch (error) {
      const statusCode = Number.isInteger(error.statusCode) ? error.statusCode : 500;
      sendJson(response, statusCode, {
        error: statusCode === 500 ? 'Internal server error' : error.message,
      });
    }
  };
}

module.exports = { createRequestHandler };
