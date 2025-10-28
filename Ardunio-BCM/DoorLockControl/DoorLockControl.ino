#include <SPI.h>
#include <mcp_can.h>

// Hardware pin connections
constexpr uint8_t  CAN0_CS  = 10;
constexpr uint8_t  CAN0_INT = 2;
constexpr uint8_t  DOOR_STATUS_LED = 8;

// CAN Message Id Configurations
constexpr uint32_t REMOTE_CMD_MSG_ID        = 0x200;
constexpr uint32_t REMOTE_CMD_RESULT_MSG_ID = 0x600;

// Supported Remote commands
constexpr uint8_t  RemoteCommand_DoorLock   = 0x30;
constexpr uint8_t  RemoteCommand_DoorUnlock = 0x31;

// Create an instance of MCP_CAN 
MCP_CAN CAN0(CAN0_CS);

// Flag for ISR function. It is used to check for arrival of new messages
volatile bool can0IntFlag = false;

// CAN0_ISR: Triggered when MCP2515 asserts interrupt, signals a new CAN message is available on CAN0
void CAN0_ISR() {
  can0IntFlag = true;   // keep ISR tiny
}

// LockDoorBlocking Locks the Door. Simulated by blinking the Door Status led once. 
bool LockDoorBlocking() {
  digitalWrite(DOOR_STATUS_LED, HIGH);
  delay(500);
  digitalWrite(DOOR_STATUS_LED, LOW);
  return true;
}

// UnlockDoorBlocking Unlocks the Door. Simulated by blinking the Door Status led twice.
bool UnlockDoorBlocking() {
  for (uint8_t i = 0; i < 2; i++) {
    digitalWrite(DOOR_STATUS_LED, HIGH);
    delay(300);
    digitalWrite(DOOR_STATUS_LED, LOW);
    delay(200);
  }
  return true;
}

// processRemoteCommand parses and executes a received remote command
bool processRemoteCommand(const uint8_t *buf, uint8_t len) {
  if (len < 1) return false;
  switch (static_cast<uint8_t>(buf[0])) {
    case RemoteCommand_DoorLock:   return LockDoorBlocking();
    case RemoteCommand_DoorUnlock: return UnlockDoorBlocking();
    default: return false;
  }
}

// sendRemoteCmdResult Sends Reslut of the Remote Command
static inline bool sendRemoteCmdResult(uint8_t cmd, bool result) {
  const uint8_t len = 2;
  uint8_t buf[8] = { cmd, static_cast<uint8_t>(result), 0,0,0,0,0,0 };
  const byte rc = CAN0.sendMsgBuf(REMOTE_CMD_RESULT_MSG_ID, 0 /*std id*/, len, buf);
  if (rc != CAN_OK) {
    Serial.print(F("sendMsgBuf failed rc=")); Serial.println(rc);
    return false;
  }
  return true;
}

// Reads incoming CAN messages and handles recognized command frames
void handleCanMessage() {
  uint32_t rxId = 0;
  uint8_t  len  = 0;
  uint8_t  buf[8];

  while (CAN0.checkReceive() == CAN_MSGAVAIL) {
    if (CAN0.readMsgBuf(&rxId, &len, buf) != CAN_OK) continue;

    if (rxId == REMOTE_CMD_MSG_ID) {
      Serial.println(F("Incoming remote command"));
      const bool ok = processRemoteCommand(buf, len);
      sendRemoteCmdResult(buf[0], ok);
    }
  }
}

// Initializes serial, CAN bus, and interrupt setup for BCM ECU
void setup() {
  Serial.begin(115200);
  pinMode(CAN0_INT, INPUT_PULLUP);
  pinMode(DOOR_STATUS_LED, OUTPUT);

  if (CAN0.begin(MCP_STDEXT, CAN_125KBPS, MCP_8MHZ) == CAN_OK) {
    Serial.println(F("MCP2515 Initialized Successfully"));
  } else {
    Serial.println(F("Error Initializing MCP2515"));
  }

  // Configure CAN filters to accept only remote command frames
  CAN0.init_Mask(0, 0, 0x7FF);                    
  CAN0.init_Filt(0, 0, REMOTE_CMD_MSG_ID);        
  CAN0.init_Mask(1, 0, 0x7FF);                    
  CAN0.init_Filt(2, 0, REMOTE_CMD_MSG_ID);        

  // Set CAN0 to Normal operations
  if (CAN0.setMode(MCP_NORMAL) != CAN_OK) {
    Serial.println(F("Failed to enter NORMAL mode"));
  }
  // Attach CAN0_ISR function to the interrupt pin on falling edge.
  attachInterrupt(digitalPinToInterrupt(CAN0_INT), CAN0_ISR, FALLING);
}

void loop() {
  if (can0IntFlag) {
    can0IntFlag = false;
    handleCanMessage();
  }
}
