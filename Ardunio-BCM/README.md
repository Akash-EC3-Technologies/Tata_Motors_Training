# BCM Remote Command Receiver

## Overview

This Arduino sketch implements a simple **Body Control Module (BCM) ECU** that listens for **remote Door Lock/Unlock commands** on a CAN bus and executes them. It expects commands from a **TCU** on a predefined CAN ID and publishes an execution **result/ack** on another CAN ID. Door actions are simulated via an LED.

* **Command RX ID:** `0x200`
* **Result TX ID:** `0x600`
* **Commands:**
  * Lock: `0x30`
  * Unlock: `0x31`
* **Command payload:** `[cmd, 0,0,0,0,0,0,0]`
* **Result payload:** `[cmd, result, 0,0,0,0,0,0]` where `result = 1 (success) | 0 (failure)`

## Hardware & Wiring

| **Component**          | **Pin Name (on Module)** | **Arduino Pin** |  **Description / Notes**                                                 |
| ---------------------- | ------------------------ | --------------- |  ----------------------------------------------------------------------- |
| **MCP2515 CAN Module** | **VCC**                  | **5V**          |  Powers MCP2515 and transceiver (most modules use 5V; check your board). |
|                        | **GND**                  | **GND**         |  Common ground with Arduino.                                             |
|                        | **CS**                   | **D10**         |  Chip Select (configured as `CAN0_CS = 10`).                             |
|                        | **SO (MISO)**            | **D12**         |  SPI Master In Slave Out.                                                |
|                        | **SI (MOSI)**            | **D11**         |  SPI Master Out Slave In.                                                |
|                        | **SCK**                  | **D13**         |  SPI Clock.                                                              |
|                        | **INT**                  | **D2**          |  Interrupt signal to Arduino (`CAN0_INT = 2`), active low.               |
| **Door Status LED**    | **Anode (+)**            | **D8**          |  Simulates door lock/unlock actions (set as `DOOR_STATUS_LED = 8`).      |
|                        | **Cathode (−)**          | **GND**         |  Connect through a current-limiting resistor (≈220 Ω – 330 Ω).           |
| **CAN Bus**            | **CAN_H**                | —               |  Connects to CAN_H line of bus                                           |
|                        | **CAN_L**                | —               |  Connects to CAN_L line of bus.                                          |

**CAN Bus**

* Connect `CAN_H` ↔ `CAN_H`, `CAN_L` ↔ `CAN_L`
* Ensure proper bus termination (120 Ω at both ends)

## Software Dependencies

* **Arduino IDE** (or PlatformIO)
* **Libraries**

  * `SPI` (built-in)
  * `mcp_can` by Cory J. Fowler

### Installing `mcp_can` Library:
1. Open Library Manager from the side bar
2. Search for mcp_can
3. Install mcp_can by Cory J. Fowler version 1.5.1

## Build & Flash
1. Open the sketch
2. Select your COM port
3. Select your board
    * Tools -> Board: -> Arduino AVR Borads -> Arduino UNO
4. Compile & upload
5. Open Serial Monitor
    1. Tools-> Serial Monitor
    2. Select Baud Rate as `115200`
6. Serial Monitor should show:
    * “**MCP2515 Initialized Successfully**” on success
    * Error messages if init or normal mode fails

## CAN Message Specification

### Command (TCU → BCM)

* **CAN ID:** `0x200` (standard 11-bit)
* **DLC:** ≥ 1
* **Data[0]:**

  * `0x30` = Door Lock
  * `0x31` = Door Unlock
* **Data[1..7]:** ignored

### Result/Ack (BCM → TCU)

* **CAN ID:** `0x600` (standard 11-bit)
* **DLC:** 2
* **Data:**

  * `Data[0] = cmd` (echo of command byte)
  * `Data[1] = result` (`0x01` success, `0x00` failure)
  * `Data[2..7] = 0`

## How It Works (Runtime Flow)

1. MCP2515 interrupt asserts on new frame → ISR sets `can0IntFlag`.
2. `loop()` sees the flag and calls `handleCanMessage()`.
3. Incoming frames are read; only ID `0x200` is processed (filters are configured accordingly).
4. `processRemoteCommand()` executes:
   * `0x30` → `LockDoorBlocking()` (LED 1 blink)
   * `0x31` → `UnlockDoorBlocking()` (LED 2 blinks)
5. BCM sends result on `0x600` as `[cmd, result]`.

## Testing

### With `can-utils`

1. Bring up your interface (example, CANable/PCAN/etc.):

   ```bash
   sudo ip link set can0 up type can bitrate 125000
   ```
2. Send Lock:

   ```bash
   cansend can0 200#30
   ```
3. Send Unlock:

   ```bash
   cansend can0 200#31
   ```
4. Observe LED behavior and capture the ack:

   ```bash
   candump can0 | grep 600
   # Expect  600   [2]  30 01  (for lock success)
   #         600   [2]  31 01  (for unlock success)
   ```

## Troubleshooting

* **No serial “Initialized” message**
  * Check SPI wiring (CS=10, MOSI=11, MISO=12, SCK=13 on Uno).
* **No acks on 0x600**
  * Confirm bus speed and termination.
  * Make sure `CAN0_INT` is wired to D2 and configured with `INPUT_PULLUP`.
* **LED doesn’t blink**
  * Confirm LED on **D8** with correct resistor and polarity, or redirect to onboard LED pin.
