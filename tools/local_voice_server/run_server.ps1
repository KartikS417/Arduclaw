$ErrorActionPreference = "Stop"

if (-not (Test-Path ".venv")) {
  python -m venv .venv
}

.\.venv\Scripts\python -m pip install --upgrade pip
.\.venv\Scripts\python -m pip install -r requirements.txt

# Optional env vars:
# $env:STT_MODEL_SIZE="base"
# $env:STT_LANGUAGE="en"
# $env:TTS_VOICE_HINT="zira"

.\.venv\Scripts\python -m uvicorn server:app --host 0.0.0.0 --port 8765
