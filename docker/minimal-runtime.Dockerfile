FROM node:22-bookworm@sha256:18a6d90fd60542269850c80ca03b29b1d9b14d6b221ac7ee8e35b0d33ef904bd AS builder

WORKDIR /app

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        python3 \
        libssl-dev \
        zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

COPY package.json package-lock.json binding.gyp ./
COPY native ./native

RUN npm ci --ignore-scripts \
    && npm run build

FROM node:22-bookworm-slim@sha256:4b150b48b4116e2a22f2c39cc27025fbf4f153e2078fd3b14d159fdb5c7279da AS runtime

ENV NODE_ENV=production \
    ARCHIVE_DIR=/data/events \
    HOST=0.0.0.0 \
    PORT=3000

WORKDIR /app

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        libssl3 \
        zlib1g \
        ca-certificates \
        tini \
    && rm -rf /var/lib/apt/lists/* \
    && mkdir -p /data/events \
    && chown node:node /data/events

COPY --chown=node:node package.json ./
COPY --chown=node:node app ./app
COPY --chown=node:node --from=builder /app/build/Release/native_versions.node ./build/Release/native_versions.node

USER node

EXPOSE 3000

HEALTHCHECK --interval=30s --timeout=3s --start-period=5s --retries=3 \
  CMD ["node", "-e", "fetch('http://127.0.0.1:3000/healthz').then(r => { if (!r.ok) process.exit(1) }).catch(() => process.exit(1))"]

ENTRYPOINT ["/usr/bin/tini", "--"]
CMD ["node", "app/server.js"]
