# TCU (Telematics Control Unit) MQTT–CAN Gateway

## Overview
The **TCU application** bridges messages between an MQTT broker and the vehicle CAN bus.  
It is designed to run as a systemd service.  

## Installed Components

| Path | Description |
|------|--------------|
| `/usr/bin/tcu` | Compiled TCU binary |
| `/lib/systemd/system/tcu.service` | Systemd unit file controlling service startup |
| `/etc/default/tcu.env` | Environment file with configurable parameters |
| `/etc/tcu/certs/` | Directory for mTLS certificates and keys |

## First Boot Configuration

Follow these steps **once** after flashing your image.

### 0. Login

login to the tcu with ssh to perform initial configuration

```bash
ssh root@<IPAddressOfTCU>
```
 > The username for tcu is root without any password

### 1. Copy mTLS Certificates from the MQTT Broker Machine

Use `scp` to copy the TCU client certificate, key, and CA certificate from the broker host to your tcu:

```bash
scp user@<BROKER_HOST_IP>:MqttBroker/mosquitto/certs/{tcu.crt,tcu.key,ca.crt} /etc/tcu/certs/
```

### 2. Configure the TCU Environment File

Open `/etc/default/tcu.env` in editor:

```bash
vim /etc/default/tcu.env
```

Set the MQTT broker details and other parameters:

```bash
# MQTT Broker Configuration
MQTT_HOST="<IP address of the MQTT Broker>"   # Example: 192.168.1.100
MQTT_PORT="8883"

# CAN interface to use
CAN_INTERFACE="can0"

# mTLS Certificates
TCU_CA_CERT="/etc/tcu/certs/ca.crt"
TCU_CLIENT_CERT="/etc/tcu/certs/tcu.crt" # rename the cert to match your client cert name
TCU_CLIENT_KEY="/etc/tcu/certs/tcu.key"  # rename the key to match your client key name
```

Save and exit (`:wq` in vim).

Restart the TCU to load the new configs

```bash
reboot
```

### 3. Testing

```bash
systemctl status tcu
```

Follow live logs:

```bash
journalctl -u tcu -f
```

If everything is correct, you’ll see a message confirming connection to the MQTT broker.

## Service Behavior

* The service starts automatically after basic networking services (`network.target`).
* It retries connection until the MQTT broker is reachable.
* Logs are written to the systemd journal (`journalctl -u tcu`).
* Restarts automatically on errors.

## Systemd commands

* To restart the service manually:

  ```bash
  systemctl restart tcu
  ```
* To update configuration:

  ```bash
  vim /etc/default/tcu.env
  systemctl restart tcu
  ```
* To inspect logs:

  ```bash
  journalctl -u tcu
  ```
