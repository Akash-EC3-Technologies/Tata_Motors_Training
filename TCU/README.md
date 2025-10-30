# TCU MQTT→CAN Bridge — README

This is a small C program for a Raspberry Pi that acts as a **Telematics Control Unit (TCU)** bridge:

* Connects to an **MQTT broker over mTLS (TLS client certs)**
* Subscribes to `status/door`
* On payload `lock` / `unlock`, transmits a CAN frame on a SocketCAN interface

It works with the Arduino-BCM that listens on **CAN ID `0x200`** and expects command bytes:
* `0x30` = Lock
* `0x31` = Unlock

## Requirements

* Raspberry Pi
* SocketCAN-capable CAN interface
* MQTT broker that supports TLS on port **8883** and client cert auth
* Certificates/keys:
  * **CA** file: CA that signed the broker cert
  * **Client cert** and **Client key** for mTLS

## Copy Certs from cloud server (Raspberry Pi)

```bash
mkdir certs
scp user@<IpAddressOfRaspberryPiCloud>:mosquitto/certs/ca.crt certs/ca.crt
scp user@<IpAddressOfRaspberryPiCloud>:mosquitto/certs/tcu.crt certs/tcu.crt
scp user@<IpAddressOfRaspberryPiCloud>:mosquitto/certs/tcu.key certs/tcu.key
```

## Install & Build

### install dependent libraries

```bash
sudo apt-get install -y libmosquitto-dev mosquitto-clients build-essential
```

### Build the application
``` 
gcc tcu.c -o tcu $(pkg-config --cflags --libs libmosquitto)
```

## Bring Up CAN

Example for `can0` at **125 kbps**:

```bash
# MCP2515 example (adapt pins/osc as needed)
# dtoverlay=mcp2515-can0,oscillator=8000000,interrupt=25
# dtoverlay=spi-bcm2835

sudo ip link set can0 up type can bitrate 125000
ip -details link show can0
```

> Ensure correct bitrate

## Run

```bash
sudo ./tcu \
  --host <IpAddressOfRaspberryPiCloud> \
  --port 8883 \
  --cafile ./certs/ca.crt \
  --cert  ./certs/tcu.crt \
  --key   ./certs/tcu.key \
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

## Topic & Payloads

* **Subscribe**: `status/door`
* **Payloads** (case-insensitive, trimmed):

  * `lock`   → CAN ID `0x200`, DLC=1, `data[0]=0x30`
  * `unlock` → CAN ID `0x200`, DLC=1, `data[0]=0x31`
* Any other payload is ignored with a warning log.

## Protocol Mapping (TCU → BCM)

| Direction | CAN ID | DLC | Data[0] | Meaning |
| --------: | -----: | --: | ------: | ------- |
| TCU → BCM |  0x200 |   1 |    0x30 | Lock    |
| TCU → BCM |  0x200 |   1 |    0x31 | Unlock  |

## Troubleshooting

* **MQTT connect failed**: Check CA/cert/key paths, broker ip address, broker logs
* **No CAN frames**: Confirm `ip link show can0` is UP, bitrate matches bus, and you’re using the right interface name.
* **No LED/BCM action**: Verify BCM is powered, listening on `0x200`, and CAN wiring/termination.

## Extending

* **ACK publishing**: After writing to CAN, publish an MQTT ack (e.g., `status/door/ack`)—easy to add next to `can_send_cmd`
