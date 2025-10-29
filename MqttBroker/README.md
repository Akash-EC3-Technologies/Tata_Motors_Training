# Mosquitto MQTT in Docker with TLS + mTLS

This guide sets up an **Eclipse Mosquitto** MQTT broker in Docker using Docker Compose

## Prerequisites

### Install Docker + Docker Compose

#### 1. Update your system

```bash
sudo apt update && sudo apt full-upgrade -y
sudo reboot
```

#### 2. Install Docker Engine

```bash
curl -fsSL https://get.docker.com -o get-docker.sh
sudo sh get-docker.sh
```

This will:

* Install `docker-ce`, `docker-ce-cli`, and `containerd`
* Enable and start the Docker service
* Configure systemd startup

#### 3. Add your user to the docker group

```bash
sudo usermod -aG docker $USER
sudo reboot
```

Test it:

```bash
docker run hello-world
```

#### 4. Enable Docker to start on boot

```bash
sudo systemctl enable docker
```


#### 5. Install Docker Compose plugin

```bash
sudo apt install docker-compose-plugin -y
```

Check it:

```bash
docker compose version
```

## 1. Project layout

```bash
mkdir -p mosquitto/{config,data,log,certs}
```

You’ll end up with:

```
.
├─ docker-compose.yml
└─ mosquitto
   ├─ config/
   ├─ data/
   ├─ log/
   └─ certs/
```

## 2. Generate certificates

We’ll create a **private CA**, then a **server certificate** (for the broker) and **client certificates** (for devices/apps).

### 2.1 Create SAN config

```bash
cat > mosquitto/certs/san.cnf <<'EOF'
[ req ]
default_bits       = 2048
distinguished_name = req_distinguished_name
req_extensions     = req_ext
prompt             = no

[ req_distinguished_name ]
CN = MosquittoMQTT-Local

[ req_ext ]
subjectAltName = @alt_names

[ alt_names ]
IP.1 = <Your IP Address>
EOF
```

SANs must match how your clients connect. To connect using IP,  add `IP.1` as your IP Address.

### 2.2 Create a local CA

```bash
# Private CA (10-year validity)
openssl genrsa -out mosquitto/certs/ca.key 4096
openssl req -x509 -new -key mosquitto/certs/ca.key -sha256 -days 3650 \
  -subj "/CN=MosquittoMQTT-Local-CA" -out mosquitto/certs/ca.crt
```

Outputs:

* `mosquitto/certs/ca.key`  (keep private)
* `mosquitto/certs/ca.crt`  (distribute to clients)

### 2.3 Server certificate (for broker)

```bash
# Key + CSR
openssl genrsa -out mosquitto/certs/server.key 2048
openssl req -new -key mosquitto/certs/server.key \
  -config mosquitto/certs/san.cnf -out mosquitto/certs/server.csr

# Issue server certificate (valid ~2.25 years)
openssl x509 -req -in mosquitto/certs/server.csr \
  -CA mosquitto/certs/ca.crt -CAkey mosquitto/certs/ca.key -CAcreateserial \
  -out mosquitto/certs/server.crt -days 825 -sha256 \
  -extfile mosquitto/certs/san.cnf -extensions req_ext
```

Outputs:

* `server.key`, `server.crt` (used by broker)

### 2.4 Client certificate (for each device/app)

Repeat per client (example: `client1`):

```bash
# Private key
openssl genrsa -out mosquitto/certs/client1.key 2048

# CSR with CN = client identity (used as username in ACL)
openssl req -new -key mosquitto/certs/client1.key -subj "/CN=client1" \
  -out mosquitto/certs/client1.csr

# Issue client cert (1 year)
openssl x509 -req -in mosquitto/certs/client1.csr \
  -CA mosquitto/certs/ca.crt -CAkey mosquitto/certs/ca.key -CAcreateserial \
  -out mosquitto/certs/client1.crt -days 365 -sha256
```

Outputs:

* `client1.key`, `client1.crt` (distributed to that client only)

```bash
openssl pkcs12 -export -clcerts \
  -in mosquitto/certs/client1.crt -inkey mosquitto/certs/client1.key \
  -out mosquitto/certs/client1.p12 -name "client1"
```

---

## 3. Mosquitto configuration (TLS + mTLS)

Create `mosquitto/config/mosquitto.conf`:

```conf
# Listen only on secure MQTT port
listener 8883
protocol mqtt

# Broker identity (server cert)
certfile /mosquitto/certs/server.crt
keyfile  /mosquitto/certs/server.key

# mTLS: verify client certs using our CA
cafile /mosquitto/certs/ca.crt
require_certificate true

# Treat client certificate CN as username (useful for ACLs)
use_identity_as_username true

# (Optional) Add password/ACLs in addition to mTLS
# allow_anonymous false
# password_file /mosquitto/config/passwordfile
# acl_file /mosquitto/config/aclfile

# Persistence & logs
persistence true
persistence_location /mosquitto/data/
log_dest stdout
```

## 4. Docker Compose

Create `docker-compose.yml` in the project root:

```yaml
services:
  mosquitto:
    image: eclipse-mosquitto:2
    container_name: mosquitto
    restart: unless-stopped
    ports:
      - "8883:8883"
    volumes:
      - ./mosquitto/config:/mosquitto/config
      - ./mosquitto/data:/mosquitto/data
      - ./mosquitto/log:/mosquitto/log
      - ./mosquitto/certs:/mosquitto/certs:ro
```

Start the broker:

```bash
docker compose up -d
docker compose logs -f
```

## 5. Test the setup

Test useing the **Mosquitto image as a client**. It includes `mosquitto_pub`/`mosquitto_sub` clients.

> Run these from the project root so the `-v "$PWD/mosquitto/certs:/certs"` mapping works.

### 5.1 Subscribe (terminal A)

```bash
docker run --rm -it \
  -v "$PWD/mosquitto/certs:/certs" \
  eclipse-mosquitto:2 \
  mosquitto_sub -h YOUR_DOMAIN_OR_IP -p 8883 \
    --cafile /certs/ca.crt \
    --cert /certs/client1.crt \
    --key  /certs/client1.key \
    -t 'test/topic' -d
```

### 5.2 Publish (terminal B)

```bash
docker run --rm -it \
  -v "$PWD/mosquitto/certs:/certs" \
  eclipse-mosquitto:2 \
  mosquitto_pub -h YOUR_DOMAIN_OR_IP -p 8883 \
    --cafile /certs/ca.crt \
    --cert /certs/client1.crt \
    --key  /certs/client1.key \
    -t 'test/topic' -m 'hello over mTLS' -d
```

You should see the subscriber receive the message.

## 6. Troubleshooting

* **`A TLS error occurred.`**
  Check SAN match, CA trust (`--cafile` path), and that you supplied both `--cert` and `--key` for mTLS.
* **`Client connection from ... denied`**
  If ACLs enabled, ensure CN exists in `aclfile` and rule allows the topic.
* **`Error opening .../server.key`**
  File path or permissions issue. Ensure the host path is correct and mounted read-only as shown.
---

## 7. Clean up

```bash
docker compose down
# (optional) remove generated certs — irreversible!
# rm -rf mosquitto/certs
```
