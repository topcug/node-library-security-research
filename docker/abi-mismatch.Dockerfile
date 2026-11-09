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

# Debian 11 does not provide the libcrypto.so.3 required by the Bookworm build.
FROM node:22-bullseye-slim@sha256:5736e7ef1f3f2109be7ef8aea0cbdf931804aee9a18c6760507b8ded078b25a9 AS runtime

WORKDIR /app

COPY app ./app
COPY --from=builder /app/build/Release/native_versions.node ./build/Release/native_versions.node

USER node

CMD ["node", "app/server.js"]
