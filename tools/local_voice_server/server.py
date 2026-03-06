import os
import tempfile
import threading
import json
import wave
from io import BytesIO
from pathlib import Path
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
            backend = (os.getenv("STT_BACKEND", "vosk") or "vosk").strip().lower()
            if backend == "vosk":
                try:
                    from vosk import Model
                except Exception as e:
                    raise RuntimeError(f"vosk import failed: {e}")

                default_model_path = Path(__file__).resolve().parent / "models" / "vosk-model-en-in-0.4"
                model_path = Path(os.getenv("STT_MODEL_PATH", str(default_model_path))).expanduser()
                if not model_path.exists():
                    raise RuntimeError(
                        f"Vosk model path not found: {model_path}. "
                        "Download and extract vosk-model-en-in-0.4 there, or set STT_MODEL_PATH."
                    )
                _stt_model = Model(str(model_path))
            else:
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


def _wav_to_pcm16_mono_16k(wav_bytes: bytes) -> bytes:
    with wave.open(BytesIO(wav_bytes), "rb") as wf:
        channels = wf.getnchannels()
        sample_width = wf.getsampwidth()
        sample_rate = wf.getframerate()
        comp = wf.getcomptype()
        frames = wf.readframes(wf.getnframes())

    if comp != "NONE":
        raise RuntimeError(f"Unsupported WAV compression: {comp}")
    # Keep runtime dependencies minimal and Python-3.13-safe (audioop removed).
    # ESP sketch already sends 16-bit mono 16k WAV for STT.
    if sample_width != 2 or channels != 1 or sample_rate != 16000:
        raise RuntimeError(
            f"Unsupported WAV format for Vosk: {sample_rate} Hz, {channels} ch, {sample_width * 8} bit. "
            "Expected 16000 Hz mono 16-bit PCM."
        )

    return frames


def _transcribe_with_vosk(model, wav_bytes: bytes) -> str:
    try:
        from vosk import KaldiRecognizer
    except Exception as e:
        raise RuntimeError(f"vosk runtime import failed: {e}")

    pcm16 = _wav_to_pcm16_mono_16k(wav_bytes)
    rec = KaldiRecognizer(model, 16000)
    rec.SetWords(False)

    results = []
    step = 4000
    for i in range(0, len(pcm16), step):
        chunk = pcm16[i:i + step]
        if rec.AcceptWaveform(chunk):
            r = json.loads(rec.Result())
            t = (r.get("text") or "").strip()
            if t:
                results.append(t)

    final_r = json.loads(rec.FinalResult())
    final_t = (final_r.get("text") or "").strip()
    if final_t:
        results.append(final_t)

    return " ".join(results).strip()


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
        backend = (os.getenv("STT_BACKEND", "vosk") or "vosk").strip().lower()
        model = _get_stt_model()
        if backend == "vosk":
            text = _transcribe_with_vosk(model, body)
        else:
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
        print(f"[STT] runtime error: {e}")
        raise HTTPException(status_code=500, detail=str(e))
    except Exception as e:
        print(f"[STT] unexpected error: {e}")
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
