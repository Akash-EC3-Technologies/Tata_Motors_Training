# TCU MQTT→CAN Bridge — README

This is a small C program for a Raspberry Pi that acts as a **Telematics Control Unit (TCU)** bridge:

* Connects to an **MQTT broker over mTLS (TLS client certs)**
* Subscribes to `status/door`
* On payload `lock` / `unlock`, transmits a CAN frame on a SocketCAN interface

It’s designed to work with your BCM sketch that listens on **CAN ID `0x200`** and expects command bytes:

* `0x30` = Lock
* `0x31` = Unlock

---

## Features

* Secure MQTT connection with **mutual TLS** (CA + client cert + key)
* Simple topic/payload mapping:

  * `status/door` → `lock` → send CAN `0x200 [1] 30`
  * `status/door` → `unlock` → send CAN `0x200 [1] 31`
* Minimal dependencies: `libmosquitto` + SocketCAN

---

## Requirements

* Raspberry Pi (Debian/Raspberry Pi OS)
* SocketCAN-capable CAN interface (e.g., MCP2515 HAT or USB-CAN)
* MQTT broker that supports TLS on port **8883** and client cert auth
* Certificates/keys:

  * **CA** file: CA that signed the broker cert
  * **Client cert** and **Client key** for mTLS

---

## Install & Build

```bash
sudo apt-get update
sudo apt-get install -y libmosquitto-dev mosquitto-clients build-essential

# Build
gcc tcu.c -o tcu -lmosquitto
```

> File to build: `tcu.c` (the single-file source provided earlier)

---

## Bring Up CAN

Example for `can0` at **125 kbps**:

```bash
# MCP2515 example (adapt pins/osc as needed)
# dtoverlay=mcp2515-can0,oscillator=8000000,interrupt=25
# dtoverlay=spi-bcm2835

sudo ip link set can0 up type can bitrate 125000
ip -details link show can0
```

> Ensure bus termination (120 Ω at each end) and correct bitrate.

---

## Run

```bash
sudo ./tcu \
  --host broker.local \
  --port 8883 \
  --cafile /etc/ssl/certs/ca.crt \
  --cert  /etc/ssl/certs/client.crt \
  --key   /etc/ssl/private/client.key \
  --canif can0
```

* `--host` : MQTT broker hostname (must match cert CN/SAN)
* `--port` : Typically `8883` for TLS
* `--cafile` : CA cert to verify broker
* `--cert` / `--key` : Client certificate & private key
* `--canif` : SocketCAN interface (default `can0`)

When connected, you should see logs like:

```
[CAN] Opened interface can0
[MQTT] Connected. Subscribing to status/door
[MAIN] Running. Press Ctrl+C to exit.
```

---

## Topic & Payloads

* **Subscribe**: `status/door`
* **Payloads** (case-insensitive, trimmed):

  * `lock`   → CAN ID `0x200`, DLC=1, `data[0]=0x30`
  * `unlock` → CAN ID `0x200`, DLC=1, `data[0]=0x31`
* Any other payload is ignored with a warning log.

---

## Protocol Mapping (TCU → BCM)

| Direction | CAN ID | DLC | Data[0] | Meaning |
| --------: | -----: | --: | ------: | ------- |
| TCU → BCM |  0x200 |   1 |    0x30 | Lock    |
| TCU → BCM |  0x200 |   1 |    0x31 | Unlock  |

> Matches your BCM sketch’s `REMOTE_CMD_MSG_ID = 0x200` and supported command bytes.

---

## Quick Test

1. **Run bridge** (as above)
2. **Publish MQTT messages** (from any client with access to the broker):

   ```bash
   mosquitto_pub -h broker.local -p 8883 --cafile ca.crt \
     --cert client.crt --key client.key \
     -t status/door -m lock

   mosquitto_pub -h broker.local -p 8883 --cafile ca.crt \
     --cert client.crt --key client.key \
     -t status/door -m unlock
   ```
3. **Observe CAN** (from another shell):

   ```bash
   candump can0
   # Expect frames like:
   #  can0  200   [1]  30
   #  can0  200   [1]  31
   ```

---

## Running as a Service (systemd)

Create `/etc/systemd/system/tcu.service`:

```ini
[Unit]
Description=TCU MQTT to CAN Bridge
After=network-online.target

[Service]
Type=simple
ExecStart=/usr/local/bin/tcu \
  --host broker.local --port 8883 \
  --cafile /etc/ssl/certs/ca.crt \
  --cert /etc/ssl/certs/client.crt \
  --key /etc/ssl/private/client.key \
  --canif can0
Restart=on-failure
RestartSec=2
User=root
AmbientCapabilities=CAP_NET_RAW

[Install]
WantedBy=multi-user.target
```

```bash
sudo cp tcu /usr/local/bin/
sudo systemctl daemon-reload
sudo systemctl enable --now tcu
systemctl status tcu
```

## Troubleshooting

* **MQTT connect failed**: Check CA/cert/key paths, broker hostname, firewall, broker logs.
* **No CAN frames**: Confirm `ip link show can0` is UP, bitrate matches bus, and you’re using the right interface name.
* **No LED/BCM action**: Verify BCM is powered, listening on `0x200`, and CAN wiring/termination.

---

## Extending

* **ACK publishing**: After writing to CAN, publish an MQTT ack (e.g., `status/door/ack`)—easy to add next to `can_send_cmd`
