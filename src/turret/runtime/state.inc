// Global constants, runtime state, and forward declarations.
// ===== Turret position / defaults =====
float x_turret = BUILD_CONFIG.xTurretCm;
float y_turret = BUILD_CONFIG.yTurretCm;
float z_turret = BUILD_CONFIG.zTurretCm;
float default_z_target = BUILD_CONFIG.defaultTargetZCm;

// ===== Pins =====
const int YAW_POT_PIN   = 34;
const int YAW_SERVO_PIN = 18;
const int PITCH_POT_PIN   = 35;
const int PITCH_SERVO_PIN = 19;
const int RELAY_CH1_PIN = 21;
const int RELAY_CH2_PIN = 22;
const int RELAY_CH3_PIN = 23;
const int ESC_PIN       = 25;

// ===== Relay / Fire timing =====
const bool RELAY_ACTIVE_LOW = true;
const unsigned long RELAY_STEP_DELAY_MS = 250;
const unsigned long FIRE_COMMAND_HOLD_MS = 3000;

// ===== Serial / network timing =====
const unsigned long SERIAL_PRINT_INTERVAL_MS = TURRET_SERIAL_PRINT_INTERVAL_MS;
const unsigned long WIFI_RETRY_INTERVAL_MS = 5000;
const unsigned long MQTT_RETRY_INTERVAL_MS = 3000;
const unsigned long MQTT_STATUS_INTERVAL_MS = 10000;
const unsigned long WIFI_CONNECT_WAIT_MS = 12000;

// ===== ADC limits =====
#define YAW_LOW_CUT    300
#define YAW_HIGH_CUT   3700
#define PITCH_LOW_CUT  1700
#define PITCH_HIGH_CUT 2400

// ===== Servo / ESC =====
const int SERVO_STOP_US = 1500;
const int SERVO_MAX_US  = 500;
const int ESC_MIN_US  = 1000;
const int ESC_MAX_US  = 2000;
const int ESC_RUN_US  = 1800;
const int ESC_STOP_US = 1000;

// ===== PID gains =====
float Kp = 0.80f;
float Ki = 0.020f;
float Kd = 0.15f;

// ===== Control parameters =====
const float YAW_DEADBAND   = 15.0f;
const float PITCH_DEADBAND = 20.0f;
const float YAW_MIN_DRIVE   = 85.0f;
const float PITCH_MIN_DRIVE = 75.0f;
const float YAW_I_LIMIT   = 3000.0f;
const float PITCH_I_LIMIT = 3000.0f;
const int YAW_DIR_HYST = 30;

// ===== Yaw linearization =====
const float a_ccw = 0.000000054507f;
const float b_ccw = -0.000214602580f;
const float c_ccw = 1.265950091727f;
const float d_ccw = -96.842440399760f;
const float a_cw = 0.000000160563f;
const float b_cw = -0.001681412932f;
const float c_cw = 6.588812706009f;
const float d_cw = -5788.444633305053f;
const float m_line = 13.4666666667f;
const float b_line = 2050.0f;

// ===== Scale / tolerance =====
const float YAW_ADC_PER_DEG   = 2260.0f / 360.0f;
const float PITCH_ADC_PER_DEG = (3110.0f - 2050.0f) / 180.0f;
const float YAW_REACHED_TOL_DEG   = 2.0f;
const float PITCH_REACHED_TOL_DEG = 2.0f;

// ===== Ballistics =====
const float v0_cm_s = 3962.4f;
const float g_cm_s2 = 981.0f;
const float PITCH_CMD_MIN_DEG = ((PITCH_LOW_CUT  - 2050.0f) * 180.0f) / (3110.0f - 2050.0f);
const float PITCH_CMD_MAX_DEG = ((PITCH_HIGH_CUT - 2050.0f) * 180.0f) / (3110.0f - 2050.0f);

// ===== Idle behavior =====
const float IDLE_YAW_MIN = -70.0f;
const float IDLE_YAW_MAX =  70.0f;
const float IDLE_YAW_SWEEP_SPEED_DEG_PER_SEC = 25.0f;
const float IDLE_PITCH_MIN_DEG = -30.0f;
const float IDLE_PITCH_MAX_DEG = 30.0f;
const float IDLE_PITCH_SWEEP_SPEED_DEG_PER_SEC = 30.0f;

// ===== MQTT =====
const size_t MQTT_PAYLOAD_BUFFER_SIZE = 512;
char mqttCommandTopic[128] = {0};
char mqttClientId[64] = {0};
unsigned long lastWiFiRetryMs = 0;
unsigned long lastMqttRetryMs = 0;
unsigned long lastMqttStatusMs = 0;

// ===== Devices =====
Servo yawServo;
Servo pitchServo;
Servo esc;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// ===== Targets / state =====
float yawTargetDeg   = 0.0f;
float pitchTargetDeg = 0.0f;
float lastTargetX = 0.0f;
float lastTargetY = 0.0f;
float lastTargetZ = 0.0f;
int yawRawCurrent   = 0;
int pitchRawCurrent = 0;
float yawCurrentDeg   = 0.0f;
float pitchCurrentDeg = 0.0f;
float yawPrevErrorPseudo   = 0.0f;
float pitchPrevErrorPseudo = 0.0f;
float yawIntegralPseudo    = 0.0f;
float pitchIntegralPseudo  = 0.0f;
int yawDirRefRaw = -1;
bool lastYawIsCW = true;
float yawWarpADC = 0.0f;
bool lastPitchValid = true;
bool idleSweepForward = true;
bool idlePitchUp = true;
float deadYawHoldDeg = 0.0f;
unsigned long lastLoopMs = 0;
unsigned long lastSerialPrintMs = 0;
bool fireTriggeredForCurrentTarget = false;
bool pendingFireWhenAimReached = false;
bool manualFireQueued = false;
char serialBuf[64];
size_t serialLen = 0;
bool yawServoAttached = false;
bool pitchServoAttached = false;
bool escAttached = false;
bool escArmed = false;
bool relayOutputsAttached = false;
bool wifiStartEnabled = (TURRET_WIFI_START_ON_SERIAL == 0);
bool wifiManualStartWaitLogged = false;
bool wifiBootDelayLogged = false;

void updateCurrentAngles();

// ===== Mode / fire state =====
enum ControlMode {
  MODE_HOLD,
  MODE_IDLE,
  MODE_TARGET,
  MODE_DEAD
};

ControlMode currentMode = MODE_HOLD;
ControlMode postFireMode = MODE_HOLD;

enum FireState {
  FIRE_IDLE,
  FIRE_CH2_ON_WAIT,
  FIRE_CH1_ON_WAIT,
  FIRE_CH3_ON_WAIT,
  FIRE_BLDC_HOLD,
  FIRE_BLDC_OFF_WAIT,
  FIRE_CH3_OFF_WAIT,
  FIRE_CH1_OFF_WAIT
};

FireState fireState = FIRE_IDLE;
unsigned long fireStateTs = 0;
unsigned long fireKeepAliveUntilMs = 0;
bool fireRestartRequested = false;

