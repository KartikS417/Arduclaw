import io
import os
import tempfile
import threading
from typing import Optional

from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import JSONResponse, Response
from pydantic import BaseModel

app = FastAPI(title="ArduClaw Local Voice Server")

_stt_model = None
_stt_lock = threading.Lock()
_tts_lock = threading.Lock()


class TTSRequest(BaseModel):
    text: str


def _get_stt_model():
    global _stt_model
    with _stt_lock:
        if _stt_model is None:
            try:
                from faster_whisper import WhisperModel
            except Exception as e:
                raise RuntimeError(f"faster-whisper import failed: {e}")

            model_size = os.getenv("STT_MODEL_SIZE", "base")
            compute_type = os.getenv("STT_COMPUTE_TYPE", "int8")
            _stt_model = WhisperModel(model_size, device="cpu", compute_type=compute_type)
        return _stt_model


def _env_bool(name: str, default: bool) -> bool:
    v = (os.getenv(name, "1" if default else "0") or "").strip().lower()
    return v in ("1", "true", "yes", "on")


def _looks_like_garbage_text(text: str) -> bool:
    t = (text or "").strip()
    if not t:
        return True
    if len(t) >= 20 and len(set(t)) <= 3:
        return True
    non_ascii = sum(1 for c in t if ord(c) > 127)
    if len(t) >= 12 and (non_ascii / len(t)) > 0.6:
        return True
    return False


@app.get("/health")
def health():
    return {"ok": True}


@app.post("/stt")
async def stt(request: Request):
    body = await request.body()
    if not body:
        raise HTTPException(status_code=400, detail="No audio payload")
    wav_path = None

    try:
        model = _get_stt_model()
        language: Optional[str] = os.getenv("STT_LANGUAGE", "en")
        vad_filter = _env_bool("STT_VAD_FILTER", False)
        no_speech_threshold = float(os.getenv("STT_NO_SPEECH_THRESHOLD", "0.6"))

        with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
            f.write(body)
            wav_path = f.name

        segments, _ = model.transcribe(
            wav_path,
            language=language,
            vad_filter=vad_filter,
            beam_size=1,
            temperature=0.0,
            no_speech_threshold=no_speech_threshold,
        )
        text = " ".join(seg.text.strip() for seg in segments).strip()
        if _looks_like_garbage_text(text):
            text = ""
        return JSONResponse({"text": text})
    except HTTPException:
        raise
    except RuntimeError as e:
        raise HTTPException(status_code=500, detail=str(e))
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"STT failed: {e}")
    finally:
        try:
            if wav_path:
                os.remove(wav_path)
        except OSError:
            pass


def _synthesize_with_pyttsx3(text: str) -> bytes:
    try:
        import pyttsx3
    except Exception as e:
        raise RuntimeError(f"pyttsx3 import failed: {e}")

    with _tts_lock:
        engine = pyttsx3.init()
        rate = int(os.getenv("TTS_RATE", "175"))
        volume = float(os.getenv("TTS_VOLUME", "1.0"))
        voice_hint = os.getenv("TTS_VOICE_HINT", "").lower().strip()

        engine.setProperty("rate", rate)
        engine.setProperty("volume", volume)

        voices = engine.getProperty("voices")
        if voice_hint:
            for v in voices:
                name = (getattr(v, "name", "") or "").lower()
                vid = (getattr(v, "id", "") or "").lower()
                if voice_hint in name or voice_hint in vid:
                    engine.setProperty("voice", v.id)
                    break
        else:
            # Prefer Indian-English voices when available on Windows.
            preferred_hints = ["heera", "ravi", "en-in", "india", "indian english"]
            selected = False
            for hint in preferred_hints:
                for v in voices:
                    name = (getattr(v, "name", "") or "").lower()
                    vid = (getattr(v, "id", "") or "").lower()
                    if hint in name or hint in vid:
                        engine.setProperty("voice", v.id)
                        selected = True
                        break
                if selected:
                    break

        with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
            wav_path = f.name

        try:
            engine.save_to_file(text, wav_path)
            engine.runAndWait()
            with open(wav_path, "rb") as rf:
                return rf.read()
        finally:
            try:
                os.remove(wav_path)
            except OSError:
                pass


@app.post("/tts")
def tts(req: TTSRequest):
    text = req.text.strip()
    if not text:
        raise HTTPException(status_code=400, detail="Text is empty")

    try:
        wav_bytes = _synthesize_with_pyttsx3(text)
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"TTS failed: {e}")

    return Response(content=wav_bytes, media_type="audio/wav")
