# Dockerfile
# Works on Raspberry Pi (arm64/armv7). For Pi Zero/1 (armv6), see note below.

FROM python:3.11-slim

# Install curl for HEALTHCHECK (optional but handy)
RUN apt-get update \
 && apt-get install -y --no-install-recommends curl \
 && rm -rf /var/lib/apt/lists/*

# Create a non-root user
RUN useradd -m -u 1000 appuser

WORKDIR /app

# Install Python deps first (cached layer)
RUN pip install --no-cache-dir \
      "flask>=3,<4" \
      "paho-mqtt>=2,<3"

# Copy your script into the image
# (Rename the file here if your filename is different.)
COPY digitaltwin.py /app/digitaltwin.py

# Default envs (all can be overridden at runtime)
ENV HTTP_HOST=0.0.0.0 \
    HTTP_PORT=8000 \
    MQTT_HOST=127.0.0.1 \
    MQTT_PORT=8883 \
    MQTT_TOPIC=status/door \
    MQTT_CLIENT_ID=digitat-twin \
    TLS_INSECURE=false \
    MQTT_QOS=1 \
    MQTT_RETAIN=false \
    MQTT_CA_CERT=/certs/ca.crt \
    MQTT_CLIENT_CERT=/certs/digitaltwin.crt \
    MQTT_CLIENT_KEY=/certs/digitaltwin.key

# Expose the HTTP port
EXPOSE 8000

# Basic health check against /health
HEALTHCHECK --interval=30s --timeout=3s --start-period=15s --retries=3 \
  CMD curl -fsS "http://127.0.0.1:${HTTP_PORT}/health" || exit 1

# Drop privileges
USER appuser

# Run it
CMD ["python", "-u", "/app/digitaltwin.py"]
