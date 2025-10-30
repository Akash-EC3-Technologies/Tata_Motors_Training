# Digital Twin MicroService

**Digital Twin** is a lightweight Python service that bridges **HTTP API calls** to **MQTT messages**, enabling secure and simple control of remote commands.

## Project Structure

```
DigitalTwin/
├── digitaltwin.py        # Main Flask + MQTT bridge
├── Dockerfile            # Docker build instructions
├── docker-compose.yml    # Docker Compose configuration
└── certs/                # TLS certificates for MQTT
    ├── ca.crt
    ├── digitaltwin.crt
    └── digitaltwin.key
```

## Place your certificates

Add your certs under the `DigitalTwin/certs/` directory:

```bash
mkdir -p ~/DigitalTwin/certs
cp ~/MqttBroker/mosquitto/certs/ca.crt ~/DigitalTwin/certs/ca.crt
cp ~/MqttBroker/mosquitto/certs/digitaltwin.crt ~/DigitalTwin/certs/digitaltwin.crt
cp ~/MqttBroker/mosquitto/certs/digitaltwin.key ~/DigitalTwin/certs/digitaltwin.key
```

## Configuration (Environment Variables)

Adjust configs in `docker-compose.yaml`

| Variable           | Default                  | Description                           |
| ------------------ | ------------------------ | ------------------------------------- |
| `MQTT_HOST`        | `127.0.0.1`              | MQTT broker hostname/IP               |
| `MQTT_PORT`        | `8883`                   | MQTT TLS port                         |
| `MQTT_TOPIC`       | `door/status`            | MQTT topic for published messages     |
| `MQTT_CLIENT_ID`   | `digital-twin`           | MQTT client identifier                |
| `MQTT_CA_CERT`     | `/certs/ca.crt`          | Path to CA certificate                |
| `MQTT_CLIENT_CERT` | `/certs/digitaltwin.crt` | Path to client certificate            |
| `MQTT_CLIENT_KEY`  | `/certs/digitaltwin.key` | Path to client private key            |
| `TLS_INSECURE`     | `false`                  | Allow self-signed or mismatched certs |
| `MQTT_QOS`         | `1`                      | MQTT publish QoS                      |
| `MQTT_RETAIN`      | `false`                  | Retain last MQTT message              |
| `HTTP_HOST`        | `0.0.0.0`                | HTTP listen address                   |
| `HTTP_PORT`        | `8000`                   | HTTP port for API + UI                |

## Build and Run the Docker image

```bash
docker-compose build
```

```bash
docker-compose up
```

This starts **Digital Twin** micro service on port **8000**.

## Dashboard UI

Once the container is running, from your laptop's browser, visit:

```
http://<raspberrypi-cloud-ip>:8000/
```

## API Endpoints

| Method | Endpoint      | Description                        |
| ------ | ------------- | ---------------------------------- |
| `GET`  | `/`           | Web dashboard                      |
| `GET`  | `/health`     | Service + MQTT connection health   |
| `POST` | `/api/lock`   | Publishes `"LOCK"` to MQTT topic   |
| `POST` | `/api/unlock` | Publishes `"UNLOCK"` to MQTT topic |

### Example Request

```bash
curl -X POST http://<pi-ip>:8000/api/lock
```
