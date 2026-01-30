#include <WiFi.h>
#include <WebSocketsClient.h>
#include <driver/i2s.h>
#include <base64.h>
#include <TM1637Display.h>
#include <Arduino.h>
#include "AudioFileSourceHTTPStream.h"
#include "AudioFileSourceBuffer.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"
#include <Preferences.h>

// ===== 상수 정의 =====
#define MIC_GAIN 3.0
#define WIFI_TIMEOUT 20000
#define CONNECTION_CHECK_INTERVAL 3000
#define PROFILE_CHANGE_INTERVAL 500
#define BLINK_INTERVAL 200
#define SPEAKER_GAIN 0.8
#define BUFFER_SIZE 8192

// ===== I2S 마이크 핀 =====
#define I2S_MIC_BCLK 13
#define I2S_MIC_LRCL 12
#define I2S_MIC_DOUT 14

// ===== I2S 스피커 핀 =====
#define I2S_SPK_BCLK 3
#define I2S_SPK_LRCL 2
#define I2S_SPK_DIN 4

// ===== 버튼 핀 =====
#define BUTTON_RECORD_PIN 12
#define BUTTON_PLAY_PIN 11

// ===== FND 핀 =====
#define FND_CLK 8
#define FND_DIO 9

// ===== 오디오 설정 =====
#define SAMPLE_RATE 16000
#define SAMPLE_BUFFER_SIZE 512

Preferences prefs;

// ===== Wi-Fi 설정 =====
String wifiSSID;
String wifiPASS;
String serverURL;

// ===== 오디오 버퍼 =====
int32_t micBuffer[SAMPLE_BUFFER_SIZE];
int16_t pcm16[SAMPLE_BUFFER_SIZE];

// ===== 글로벌 객체 =====
WebSocketsClient webSocket;
TM1637Display display(FND_CLK, FND_DIO);

// ===== 상태 변수 =====
uint8_t currentProfile = 0;
const uint8_t maxProfiles = 5;
bool isConnected = false;
bool wifiConnected = false;
bool isRecording = false;
bool isPlaying = false;
bool displayOn = true;

// ===== 버튼 상태 =====
bool lastRecordButtonState = HIGH;
bool lastPlayButtonState = HIGH;
bool playButtonPressed = false;

// ===== 타이밍 변수 =====
unsigned long lastConnectionAttempt = 0;
unsigned long playButtonPressTime = 0;
unsigned long lastProfileChangeTime = 0;
unsigned long lastBlinkTime = 0;

// ===== MP3 재생 객체 =====
AudioGeneratorMP3 *mp3 = nullptr;
AudioFileSourceHTTPStream *fileHttp = nullptr;
AudioFileSourceBuffer *fileBuf = nullptr;
AudioOutputI2S *out = nullptr;

// ===== URL 정규화 함수 =====
String normalizeServerURL(String url)
{
  url.trim();
  
  // http:// 또는 https:// 제거
  if (url.startsWith("http://"))
  {
    url = url.substring(7);
  }
  else if (url.startsWith("https://"))
  {
    url = url.substring(8);
  }
  
  // 마지막 슬래시 제거
  while (url.endsWith("/"))
  {
    url = url.substring(0, url.length() - 1);
  }
  
  return url;
}

String buildHTTPURL(String path)
{
  String normalized = normalizeServerURL(serverURL);
  return "http://" + normalized + path;
}

String buildHTTPSURL(String path)
{
  String normalized = normalizeServerURL(serverURL);
  return "https://" + normalized + path;
}

// ===== 설정 관리 =====
void loadSettings()
{
  prefs.begin("config", true);
  wifiSSID = prefs.getString("ssid", "");
  wifiPASS = prefs.getString("pass", "");
  serverURL = prefs.getString("server", "");
  prefs.end();

  if (wifiSSID.isEmpty() || wifiPASS.isEmpty() || serverURL.isEmpty())
  {
    Serial.println("⚠️ 설정 없음. 시리얼로 설정하세요.");
  }
  else
  {
    Serial.println("✅ 설정 로드 완료");
    Serial.println("SSID   : " + wifiSSID);
    Serial.println("SERVER : " + serverURL);
    Serial.println("정규화 : " + normalizeServerURL(serverURL));
  }
}

void saveSettings()
{
  // 저장 전 URL 정규화
  serverURL = normalizeServerURL(serverURL);
  
  prefs.begin("config", false);
  prefs.putString("ssid", wifiSSID);
  prefs.putString("pass", wifiPASS);
  prefs.putString("server", serverURL);
  prefs.end();
  Serial.println("💾 설정 저장 완료");
  Serial.println("정규화된 서버: " + serverURL);
}

// ===== Wi-Fi 연결 =====
bool connectWiFi()
{
  if (wifiSSID.isEmpty())
  {
    Serial.println("❌ WiFi 설정 없음");
    return false;
  }

  Serial.print("📡 WiFi 연결 중: ");
  Serial.println(wifiSSID);
  WiFi.begin(wifiSSID.c_str(), wifiPASS.c_str());

  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");

    if (millis() - startTime > WIFI_TIMEOUT)
    {
      Serial.println("\n⏱️ WiFi 연결 시간 초과!");
      Serial.println("❌ WiFi 연결 실패");
      Serial.println("💡 시리얼 명령어로 설정을 확인하거나 변경하세요.");
      WiFi.disconnect();
      return false;
    }
  }

  Serial.println("\n✅ WiFi 연결 완료");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  return true;
}

// ===== 디스플레이 관리 =====
void setDisplayBrightness(bool on)
{
  if (on)
  {
    display.setBrightness(0x0f);
  }
  else
  {
    display.clear();
  }
}

void updateDisplay()
{
  if (!isConnected)
  {
    uint8_t dashData[] = {SEG_G, SEG_G, SEG_G, SEG_G};
    display.setSegments(dashData);
  }
  else if (isRecording)
  {
    uint8_t recData[] = {
        0,
        SEG_E | SEG_G,
        SEG_A | SEG_D | SEG_E | SEG_F | SEG_G,
        SEG_A | SEG_D | SEG_E | SEG_F};
    display.setSegments(recData);
    display.showNumberDecEx(currentProfile + 1, 0, false, 1, 0);
  }
  else
  {
    display.showNumberDec(currentProfile + 1, true);
  }
}

void handleDisplayBlink()
{
  bool shouldBlink = !isConnected || isPlaying || isRecording;
  
  if (shouldBlink) {
    unsigned long now = millis();
    if (now - lastBlinkTime >= BLINK_INTERVAL) {
      lastBlinkTime = now;
      displayOn = !displayOn;
      setDisplayBrightness(displayOn);
      if (displayOn) updateDisplay();
    }
  }
  else {
    // 깜빡임 중지 시 항상 켜진 상태로 복구
    if (!displayOn) {
      displayOn = true;
      setDisplayBrightness(true);
    }
    updateDisplay();  // 매번 업데이트
  }
}

void resetDisplay()
{
  displayOn = true;
  display.setBrightness(0x0f);
  updateDisplay();
}

// ===== I2S 초기화 =====
void setupMic()
{
  i2s_config_t micConfig = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
      .communication_format = I2S_COMM_FORMAT_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 4,
      .dma_buf_len = 512,
      .use_apll = false,
      .tx_desc_auto_clear = false,
      .fixed_mclk = 0};

  i2s_pin_config_t micPins = {
      .bck_io_num = I2S_MIC_BCLK,
      .ws_io_num = I2S_MIC_LRCL,
      .data_out_num = I2S_PIN_NO_CHANGE,
      .data_in_num = I2S_MIC_DOUT};

  i2s_driver_install(I2S_NUM_0, &micConfig, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &micPins);
  Serial.println("✅ 마이크 초기화 완료");
}

void setupSpeaker()
{
  if (out) delete out;
  out = new AudioOutputI2S(1);
  out->SetPinout(I2S_SPK_BCLK, I2S_SPK_LRCL, I2S_SPK_DIN);
  out->SetGain(SPEAKER_GAIN);
  out->SetOutputModeMono(true);
  Serial.println("✅ 스피커 초기화 완료");
}

// ===== 스피커 제어 =====
void cleanupAudioObjects()
{
  if (mp3)
  {
    mp3->stop();
    delete mp3;
    mp3 = nullptr;
  }
  if (fileBuf)
  {
    delete fileBuf;
    fileBuf = nullptr;
  }
  if (fileHttp)
  {
    delete fileHttp;
    fileHttp = nullptr;
  }
  if (out)
  {
    delete out;
    out = nullptr;
  }
}

void stopSpeaker()
{
  cleanupAudioObjects();
  isPlaying = false;
  resetDisplay();
}

void stopPlaybackAndResetMic()
{
  stopSpeaker();
  delay(200);
  i2s_driver_uninstall(I2S_NUM_0);
  setupMic();
}

// ===== MP3 재생 =====
bool canPlayAudio()
{
  if (isRecording)
  {
    Serial.println("⚠️ 녹음 중에는 재생할 수 없습니다!");
    return false;
  }
  if (!isConnected)
  {
    Serial.println("⚠️ 재생 불가: 서버 연결 안됨");
    return false;
  }
  return true;
}

bool checkFileExists(String url)
{
  HTTPClient http;
  http.begin(url);
  int httpCode = http.GET();
  http.end();
  
  if (httpCode == 404)
  {
    Serial.println("❌ 404 오류: 파일을 찾을 수 없습니다");
    return false;
  }
  else if (httpCode < 0)
  {
    Serial.println("❌ HTTP 요청 실패");
    return false;
  }
  
  return (httpCode == 200);
}

void playMP3(int profileIndex)
{
  if (!canPlayAudio()) return;

  // URL 정규화를 사용하여 올바른 URL 생성
  String url = buildHTTPURL("/recording_" + String(profileIndex) + ".mp3");
  
  // 파일 존재 여부 먼저 확인
  if (!checkFileExists(url))
  {
    Serial.println("💡 프로필 " + String(profileIndex + 1) + "의 녹음 파일이 없습니다");
    return;
  }

  Serial.println("▶ 재생 중: " + url);

  stopSpeaker();
  delay(100);
  setupSpeaker();
  delay(50);

  fileHttp = new AudioFileSourceHTTPStream(url.c_str());
  if (!fileHttp->isOpen())
  {
    Serial.println("❌ HTTP 스트림 열기 실패!");
    stopSpeaker();
    return;
  }

  fileBuf = new AudioFileSourceBuffer(fileHttp, BUFFER_SIZE);
  mp3 = new AudioGeneratorMP3();

  if (!mp3->begin(fileBuf, out))
  {
    Serial.println("❌ MP3 시작 실패!");
    stopSpeaker();
  }
  else
  {
    Serial.println("✅ MP3 재생 시작됨");
    isPlaying = true;
  }
}

// ===== WebSocket 이벤트 =====
void onWebSocketEvent(WStype_t type, uint8_t *payload, size_t length)
{
  switch (type)
  {
  case WStype_CONNECTED:
    Serial.println("✅ WebSocket 연결됨!");
    isConnected = true;
    resetDisplay();
    break;

  case WStype_DISCONNECTED:
    Serial.println("❌ WebSocket 연결 끊김!");
    isConnected = false;
    break;

  case WStype_TEXT:
    Serial.printf("📩 서버 메시지: %s\n", payload);
    break;

  case WStype_ERROR:
    Serial.println("⚠️ WebSocket 오류!");
    isConnected = false;
    break;

  default:
    break;
  }
}

// ===== WebSocket 연결 관리 =====
void checkConnection()
{
  if (!wifiConnected) return;

  unsigned long now = millis();
  if (!webSocket.isConnected() && (now - lastConnectionAttempt > CONNECTION_CHECK_INTERVAL))
  {
    Serial.println("🔄 WebSocket 연결 시도 중...");
    
    // URL 정규화를 사용하여 WebSocket 연결
    String normalized = normalizeServerURL(serverURL);
    webSocket.beginSSL(normalized.c_str(), 443, "/ws");
    webSocket.onEvent(onWebSocketEvent);
    webSocket.setReconnectInterval(5000);
    lastConnectionAttempt = now;
  }
}

// ===== 녹음 제어 =====
void startRecording()
{
  if (!isConnected)
  {
    Serial.println("⚠️ 녹음 불가: 서버 연결 안됨");
    return;
  }
  
  isRecording = true;
  String startMsg = "__START__:" + String(currentProfile);
  webSocket.sendTXT(startMsg);
  updateDisplay();
  Serial.println("🎙️ 녹음 시작 - 프로필 " + String(currentProfile + 1));
}

void stopRecording()
{
  isRecording = false;
  String stopMsg = "__STOP__:" + String(currentProfile);
  webSocket.sendTXT(stopMsg);
  resetDisplay();
  Serial.println("✋ 녹음 중지");
  delay(100);
}

void handleRecordButton()
{
  bool recordButtonState = digitalRead(BUTTON_RECORD_PIN) == LOW;

  if (recordButtonState && !lastRecordButtonState)
  {
    if (!isRecording && !isPlaying)
    {
      startRecording();
    }
  }

  else if (!recordButtonState && lastRecordButtonState)
  {
    if (isRecording)
    {
      stopRecording();
    }
  }

  lastRecordButtonState = recordButtonState;
}

// ===== 오디오 데이터 전송 =====
int16_t applySampleGain(int32_t sample)
{
  int32_t s = (sample >> 14);
  s = (int32_t)(s * MIC_GAIN);
  
  if (s > 32767) s = 32767;
  if (s < -32768) s = -32768;
  
  return (int16_t)s;
}

void sendAudioData()
{
  if (!isRecording || !isConnected) return;

  size_t bytesRead;
  i2s_read(I2S_NUM_0, micBuffer, sizeof(micBuffer), &bytesRead, 100);

  if (bytesRead > 0) {
    int samples = bytesRead / sizeof(int32_t);
    for (int i = 0; i < samples; i++) {
      pcm16[i] = applySampleGain(micBuffer[i]);
    }

    String encoded = base64::encode((uint8_t *)pcm16, samples * sizeof(int16_t));
    
    // 신규 프로토콜 사용
    String message = "DATA:" + String(currentProfile) + ":" + encoded;
    webSocket.sendTXT(message);
  }
}

// ===== 프로필 변경 =====
void changeProfile()
{
  if (isPlaying)
  {
    stopPlaybackAndResetMic();
  }

  currentProfile = (currentProfile + 1) % maxProfiles;
  updateDisplay();
  Serial.println("🔄 프로필 변경: " + String(currentProfile + 1));
}

void togglePlayback()
{
  if (!isConnected)
  {
    Serial.println("⚠️ 재생 불가: 서버 연결 안됨");
    return;
  }

  if (isPlaying)
  {
    Serial.println("⏹️ 재생 중지");
    stopPlaybackAndResetMic();
  }
  else
  {
    Serial.println("▶ 프로필 재생 " + String(currentProfile + 1));
    playMP3(currentProfile);
  }
}

void handlePlayButton()
{
  bool playButtonState = digitalRead(BUTTON_PLAY_PIN) == LOW;
  unsigned long now = millis();

  // 버튼 눌림 시작
  if (playButtonState && !lastPlayButtonState)
  {
    playButtonPressTime = now;
    playButtonPressed = true;
    lastProfileChangeTime = now;
  }
  // 버튼 계속 눌림 (길게 누르기)
  else if (playButtonState && playButtonPressed)
  {
    unsigned long duration = now - playButtonPressTime;

    if (duration >= PROFILE_CHANGE_INTERVAL)
    {
      if (now - lastProfileChangeTime >= PROFILE_CHANGE_INTERVAL)
      {
        changeProfile();
        lastProfileChangeTime = now;
      }
    }
  }
  // 버튼 떼어짐
  else if (!playButtonState && lastPlayButtonState && playButtonPressed)
  {
    unsigned long duration = now - playButtonPressTime;
    playButtonPressed = false;

    // 짧게 누름 = 재생/중지
    if (duration < PROFILE_CHANGE_INTERVAL)
    {
      togglePlayback();
    }
  }

  lastPlayButtonState = playButtonState;
}

// ===== MP3 재생 루프 =====
void handleMP3Playback()
{
  if (!isPlaying || !mp3) return;

  if (mp3->isRunning())
  {
    if (!mp3->loop())
    {
      Serial.println("✅ MP3 재생 완료");
      stopPlaybackAndResetMic();
    }
  }
  else
  {
    Serial.println("⚠️ MP3 예기치 않게 중지됨");
    stopPlaybackAndResetMic();
  }
}

// ===== 시리얼 명령어 처리 =====
void printHelp()
{
  Serial.println("❓ 알 수 없는 명령");
  Serial.println("사용 가능한 명령어:");
  Serial.println("  WIFI SSID <이름>");
  Serial.println("  WIFI PASS <비밀번호>");
  Serial.println("  SERVER <주소>");
  Serial.println("  SAVE");
  Serial.println("  STATUS");
  Serial.println("  CONNECT");
  Serial.println("  REBOOT");
}

void printStatus()
{
  Serial.println("===== 상태 =====");
  Serial.println("SSID   : " + wifiSSID);
  Serial.println("SERVER : " + serverURL);
  Serial.println("정규화 : " + normalizeServerURL(serverURL));
  Serial.println("WiFi   : " + String(wifiConnected ? "연결됨" : "연결 안됨"));
  Serial.println("================");
}

void handleSerialCommand()
{
  if (!Serial.available()) return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();

  if (cmd.startsWith("WIFI SSID "))
  {
    wifiSSID = cmd.substring(10);
    Serial.println("✅ SSID 설정: " + wifiSSID);
  }
  else if (cmd.startsWith("WIFI PASS "))
  {
    wifiPASS = cmd.substring(10);
    Serial.println("✅ 비밀번호 설정됨");
  }
  else if (cmd.startsWith("SERVER "))
  {
    String rawURL = cmd.substring(7);
    serverURL = normalizeServerURL(rawURL);
    Serial.println("✅ 서버 설정: " + rawURL);
    Serial.println("   정규화됨: " + serverURL);
  }
  else if (cmd == "SAVE")
  {
    saveSettings();
  }
  else if (cmd == "REBOOT")
  {
    Serial.println("🔄 재부팅 중...");
    delay(1000);
    ESP.restart();
  }
  else if (cmd == "STATUS")
  {
    printStatus();
  }
  else if (cmd == "CONNECT")
  {
    Serial.println("🔄 WiFi 재연결 시도 중...");
    wifiConnected = connectWiFi();
    if (wifiConnected)
    {
      checkConnection();
      Serial.println("🌐 WebSocket 재연결 중...");
    }
  }
  else
  {
    printHelp();
  }
}

// ===== setup =====
void setup()
{
  Serial.begin(115200);
  delay(3000);

  pinMode(BUTTON_RECORD_PIN, INPUT_PULLUP);
  pinMode(BUTTON_PLAY_PIN, INPUT_PULLUP);

  display.setBrightness(0x0f);
  updateDisplay();

  loadSettings();

  Serial.println("╔════════════════════════════════════╗");
  Serial.println("║  🎙️  ESP32 오디오 녹음기          ║");
  Serial.println("╚════════════════════════════════════╝");

  wifiConnected = connectWiFi();
  setupMic();

  if (wifiConnected)
  {
    checkConnection();
    Serial.println("🌐 WebSocket 서버 연결 중...");
  }
  else
  {
    Serial.println("⚠️ WiFi 연결 실패로 WebSocket 연결을 건너뜁니다.");
    Serial.println("💡 시리얼 명령어를 입력하여 설정을 수정할 수 있습니다.");
  }
}

// ===== loop =====
void loop()
{
  handleSerialCommand();
  
  // WiFi 연결 상태 체크
  if (wifiConnected && WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ WiFi 연결 끊김 감지");
    wifiConnected = false;
    isConnected = false;
    if (isRecording) {
      stopRecording();
    }
    if (isPlaying) {
      stopPlaybackAndResetMic();
    }
  }
  
  // WiFi 자동 재연결 시도
  if (!wifiConnected && WiFi.status() == WL_CONNECTED) {
    Serial.println("✅ WiFi 자동 재연결됨");
    wifiConnected = true;
  }
  
  if (wifiConnected) {
    webSocket.loop();
    checkConnection();
  }

  handleRecordButton();
  sendAudioData();
  handlePlayButton();
  handleMP3Playback();
  handleDisplayBlink();
  
  delay(5);
}
