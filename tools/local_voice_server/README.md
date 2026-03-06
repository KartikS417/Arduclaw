# Local Voice Server (Option 1)

This server gives your ESP32 two local endpoints:
- `POST /stt` for speech-to-text
- `POST /tts` for text-to-speech

It is designed for the ArduClaw voice example:
- `examples/ESP32_S3_TouchLCD183_VoiceAssistant/ESP32_S3_TouchLCD183_VoiceAssistant.ino`

## 1) Start server on Windows

From this folder:

```powershell
cd tools\local_voice_server
.\run_server.ps1
```

Server runs on `http://<your-pc-ip>:8765`.

## 2) Configure ESP32 sketch

Set these in the sketch:

```cpp
const char* STT_URL = "http://<PC_IP>:8765/stt";
const char* TTS_URL = "http://<PC_IP>:8765/tts";
```

Use the same `<PC_IP>` you use for your Local LLM host.

## 3) Optional tuning

Environment variables before starting server:

- `STT_MODEL_SIZE` default `base` (`tiny`, `base`, `small`, ...)
- `STT_COMPUTE_TYPE` default `int8`
- `STT_LANGUAGE` default auto-detect (example: `en`, `hi`)
- `TTS_RATE` default `175`
- `TTS_VOLUME` default `1.0`
- `TTS_VOICE_HINT` default empty (example: `zira`, `david`)
  - For Indian English on Windows, try `heera` or `ravi`

Example:

```powershell
$env:STT_MODEL_SIZE="small"
$env:STT_LANGUAGE="en"
$env:TTS_VOICE_HINT="zira"
.\run_server.ps1
```

## Notes

- First STT call may be slow because model loads on demand.
- `pyttsx3` uses installed Windows voices (offline).
- If STT fails to install, upgrade `pip` and retry.
