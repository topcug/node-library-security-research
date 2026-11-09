# One Node.js Process, Two OpenSSL Versions

This is the companion repository for [One Node.js Process Can Use Two OpenSSL Versions at Once](https://www.gulcantopcu.com/posts/one-nodejs-process-can-use-two-openssl-versions-at-once/).

The example looks ordinary at first: one Node.js app and one native addon. The weird part is that they do not have to use the same OpenSSL or zlib. Node.js uses the copies built into its executable, while the addon loads the copies installed by Debian.

## Build and run it

You need Docker, Make, curl, and jq.

```bash
make build

docker run --rm -d --name webhook-archive \
  -p 3000:3000 webhook-archive:minimal >/dev/null

until versions=$(curl -fsS http://127.0.0.1:3000/versions 2>/dev/null); do
  sleep 1
done

jq . <<<"${versions}"
docker stop webhook-archive >/dev/null
```

The response shows the OpenSSL and zlib versions used by Node.js next to the versions loaded by the native addon.

## Reproduce the runtime mismatch

The second Dockerfile builds the addon on Debian 12 Bookworm and runs it on Debian 11 Bullseye:

```bash
docker build \
  --file docker/abi-mismatch.Dockerfile \
  --tag webhook-archive:abi-mismatch \
  .

docker run --rm webhook-archive:abi-mismatch
```

Docker finishes the build, but Node.js cannot load the addon because the Bullseye image does not provide `libcrypto.so.3`.
