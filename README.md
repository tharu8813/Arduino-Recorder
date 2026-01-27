# Arduino-Recorder

[![Visual Studio Code](https://img.shields.io/badge/-Visual%20Studio%20Code-007ACC?style=flat&logo=visual-studio-code&logoColor=white)](https://code.visualstudio.com/)
[![Platform](https://img.shields.io/badge/Platform-ESP32-E7352C?style=flat&logo=espressif&logoColor=white)](https://www.espressif.com/en/products/socs/esp32)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-orange?style=flat&logo=platformio&logoColor=white)](https://platformio.org/)
[![Framework](https://img.shields.io/badge/Framework-Arduino-00979D?style=flat&logo=arduino&logoColor=white)](https://www.arduino.cc/)
[![License](https://img.shields.io/badge/License-GPL3.0-blue.svg)](LICENSE)

Arduino-Recorder는 Arduino Nano ESP32를 기반으로 한 음성 녹음 및 재생 시스템입니다.  
WebSocket을 통해 [서버](https://github.com/tharu8813/Arduino-Recorder-Server)와 실시간으로 통신하며, 최대 5개의 프로필에 음성을 녹음하고 재생할 수 있습니다.

## 배선도

> 아래 배선도는 참고용이며, 실제 핀 배치와 다를 수 있습니다.

<img width="605" height="473" alt="image" src="https://github.com/user-attachments/assets/fb578447-4deb-4f8b-af20-d71b0a69d343" />

### 핀 연결

| 부품 | ESP32 핀 | 기능 |
|------|----------|------|
| I2S 마이크 BCLK | GPIO 13 | 비트 클럭 |
| I2S 마이크 LRCL | GPIO 12 | 워드 선택 |
| I2S 마이크 DOUT | GPIO 14 | 데이터 출력 |
| I2S 스피커 BCLK | GPIO 3 | 비트 클럭 |
| I2S 스피커 LRCL | GPIO 2 | 워드 선택 |
| I2S 스피커 DIN | GPIO 4 | 데이터 입력 |
| TM1637 CLK | GPIO 8 | 클럭 |
| TM1637 DIO | GPIO 9 | 데이터 |
| 녹음 버튼 | GPIO 12 | 풀업 |
| 재생 버튼 | GPIO 11 | 풀업 |

## 빌드 및 업로드

1. Visual Studio Code에서 **PlatformIO IDE** 확장을 설치합니다.
2. 프로젝트를 열고 Arduino Nano ESP32를 USB로 연결합니다.
3. VS Code 하단의 **Upload(→)** 버튼을 눌러 빌드 및 업로드합니다.

## 사용 방법

### 초기 설정

1. 시리얼 모니터를 열고 다음 명령어로 WiFi와 서버를 설정합니다:

```
WIFI SSID <와이파이이름>
WIFI PASS <와이파이비밀번호>
SERVER <서버주소>
SAVE
```

2. `REBOOT` 명령어로 재부팅하거나 전원을 다시 연결합니다.

### 녹음하기

1. **녹음 버튼**을 누르고 있는 동안 음성이 녹음됩니다.
2. 디스플레이에 "REC"와 현재 프로필 번호가 표시됩니다.
3. 버튼을 떼면 녹음이 종료되고 서버에 저장됩니다.

### 재생하기

1. **재생 버튼**을 짧게 누르면 현재 프로필의 녹음이 재생됩니다.
2. 재생 중 다시 버튼을 누르면 재생이 중지됩니다.

### 프로필 변경

1. **재생 버튼**을 길게 누르고 있으면 프로필이 순환 변경됩니다.
2. 디스플레이에 현재 프로필 번호(1~5)가 표시됩니다.

## 시리얼 명령어

| 명령어 | 설명 |
|--------|------|
| `WIFI SSID <이름>` | WiFi SSID 설정 |
| `WIFI PASS <비밀번호>` | WiFi 비밀번호 설정 |
| `SERVER <주소>` | 서버 주소 설정 |
| `SAVE` | 설정 저장 |
| `STATUS` | 현재 설정 및 연결 상태 확인 |
| `CONNECT` | WiFi 및 WebSocket 재연결 시도 |
| `REBOOT` | ESP32 재부팅 |

## 서버 구성

### WebSocket 서버 요구사항

- WebSocket 엔드포인트: `wss://<서버주소>/ws`
- 녹음 시작 메시지: `__START__:<프로필번호>`
- 녹음 중지 메시지: `__STOP__:<프로필번호>`
- 오디오 데이터: Base64 인코딩된 16-bit PCM (16kHz)

### MP3 파일 서빙

서버는 다음 형식의 URL로 MP3 파일을 제공해야 합니다:
```
http://<서버주소>/recording_<프로필번호>.mp3
```
