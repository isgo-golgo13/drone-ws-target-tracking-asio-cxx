#!/bin/bash
# Generate self-signed TLS certificates for development

set -e

CERT_DIR="${1:-certificates}"

mkdir -p "$CERT_DIR"

# Generate self-signed certificate
openssl req -x509 \
    -newkey rsa:4096 \
    -keyout "$CERT_DIR/server-key.pem" \
    -out "$CERT_DIR/server.pem" \
    -days 365 \
    -nodes \
    -subj "/CN=localhost/O=Development/C=US" \
    -addext "subjectAltName=DNS:localhost,IP:127.0.0.1,IP:::1"

echo "Certificates generated in $CERT_DIR/"
echo "  - server.pem (certificate)"
echo "  - server-key.pem (private key)"
