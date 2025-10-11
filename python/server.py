# server.py
import os
import time
import shutil
import tempfile
from fastapi import FastAPI, UploadFile, File, Query, Header, HTTPException, Request
from fastapi.responses import JSONResponse
import uvicorn
import base64
import wave
import struct
import random
from functools import lru_cache
from io import BytesIO
import asyncio

# IMPORTS das funções do seu listen_serial.py
# Certifique-se que listen_serial.py está no mesmo diretório (ou no PYTHONPATH)
from listen_serial import normalize_string, levenshtein, transcrever_fala, carregar_palavras

# Config
API_KEY = os.getenv("ASR_API_KEY", "minha_chave_segura")
UPLOAD_DIR = os.getenv("ASR_UPLOAD_DIR", "/tmp/asr_uploads")
os.makedirs(UPLOAD_DIR, exist_ok=True)

app = FastAPI(title="ASR Remote API for BitDogLab")

def check_api_key(x_api_key: str):
    if x_api_key != API_KEY:
        raise HTTPException(status_code=401, detail="invalid api key")
    

@lru_cache(maxsize=8)
def cached_words(nivel: int):
    return carregar_palavras(nivel)

@app.get("/request_word")
async def request_word(nivel: int = Query(1, ge=1), x_api_key: str = Header(None)):
    check_api_key(x_api_key)
    palavras = cached_words(nivel)
    palavra = random.choice(palavras)
    return {
        "word": palavra,
        "normalized": normalize_string(palavra),
        "nivel": nivel
    }

@app.post("/upload_audio")
async def upload_audio(
    nivel: int = Query(1, ge=1),
    expected: str = Query(None),
    file: UploadFile = File(...),
    x_api_key: str = Header(None)
):
    """Recebe um WAV (audio/wav) e retorna a transcrição e se foi aceita."""
    check_api_key(x_api_key)

    # salva temporariamente
    ts = int(time.time() * 1000)
    tmp_path = os.path.join(UPLOAD_DIR, f"upload_{ts}.wav")
    try:
        with open(tmp_path, "wb") as f:
            shutil.copyfileobj(file.file, f)
    finally:
        file.file.close()

    # usa sua pipeline de preprocess/transcribe
    recognized_norm = transcrever_fala(tmp_path)

    if recognized_norm in ("incompreensivel", "erro", ""):
        return JSONResponse({
            "recognized": recognized_norm,
            "accepted": False,
            "levenshtein": None,
            "expected": expected
        })

    lev = None
    accepted = False
    if expected:
        lev = levenshtein(recognized_norm, expected)
        if recognized_norm == expected or lev <= 1:
            accepted = True

    # se aceito, para compatibilidade com seu main.c podemos devolver a palavra correta (expected)
    to_send = expected if accepted and expected else recognized_norm

    return JSONResponse({
        "recognized": recognized_norm,
        "accepted": accepted,
        "levenshtein": lev,
        "to_send": to_send,
        "expected": expected
    })

@app.post("/upload_audio_json")
async def upload_audio_json(payload: dict, x_api_key: str = Header(None)):
    """Recebe um JSON com o áudio em base64 e salva como arquivo WAV."""
    check_api_key(x_api_key)

    try:
        file_name = payload.get("file_name", "audio.wav")
        nivel = payload.get("nivel", 1)
        expected = payload.get("expected", "")
        file_data_b64 = payload.get("file_data", "")

        # Decodifica o base64 e salva como arquivo temporário
        tmp_path = os.path.join(UPLOAD_DIR, file_name)
        with open(tmp_path, "wb") as f:
            f.write(base64.b64decode(file_data_b64))

        # Transcreve e compara como no endpoint original
        recognized_norm = transcrever_fala(tmp_path)
        if recognized_norm in ("incompreensivel", "erro", ""):
            return JSONResponse({
                "recognized": recognized_norm,
                "accepted": False,
                "levenshtein": None,
                "expected": expected
            })

        lev = None
        accepted = False
        if expected:
            lev = levenshtein(recognized_norm, expected)
            if recognized_norm == expected or lev <= 1:
                accepted = True

        return JSONResponse({
            "recognized": recognized_norm,
            "accepted": accepted,
            "levenshtein": lev,
            "expected": expected
        })

    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))
    
# Tabelas compatíveis com IMA ADPCM (mesmas do C)
INDEX_TABLE = [-1, -1, -1, -1, 2, 4, 6, 8,
               -1, -1, -1, -1, 2, 4, 6, 8]

STEP_TABLE = [
     7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,
    34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,
   157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,
   724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,
  3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,
 15289,16818,18500,20350,22385,24623,27086,29794,32767
]

def decode_ima_adpcm(adpcm_bytes):
    """
    Decodifica stream IMA ADPCM (4-bit) -> lista de int16 PCM.
    Implementação compatível com o encoder C fornecido anteriormente.
    """
    pcm_samples = []
    prev_sample = 0
    index = 0

    for b in adpcm_bytes:
        # lower nibble = first sample (nibble1)
        nib1 = b & 0x0F
        nib2 = (b >> 4) & 0x0F

        for code in (nib1, nib2):
            step = STEP_TABLE[index]
            diffq = step >> 3
            if code & 4:
                diffq += step
            if code & 2:
                diffq += step >> 1
            if code & 1:
                diffq += step >> 2

            if code & 8:
                prev_sample -= diffq
            else:
                prev_sample += diffq

            if prev_sample > 32767:
                prev_sample = 32767
            elif prev_sample < -32768:
                prev_sample = -32768

            index += INDEX_TABLE[code]
            if index < 0: index = 0
            if index > 88: index = 88

            pcm_samples.append(int(prev_sample))

    return pcm_samples

def write_wav_from_pcm16(path, pcm_samples, sample_rate=8000):
    with wave.open(path, 'wb') as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)  # 16 bits
        wf.setframerate(sample_rate)
        # pack little-endian signed 16-bit
        frames = b''.join(struct.pack('<h', s) for s in pcm_samples)
        wf.writeframes(frames)

@app.post("/upload_audio_raw")
async def upload_audio_raw(nivel: int = Query(1, ge=1),
                           expected: str = Query(None),
                           request: Request = None,
                           x_api_key: str = Header(None)):
    """
    Recebe POST com corpo bruto contendo ADPCM (audio/adpcm).
    Query params: nivel, expected
    Header: x-api-key
    """
    check_api_key(x_api_key)

    content_type = request.headers.get("content-type", "")
    body = await request.body()

    if not body:
        raise HTTPException(status_code=400, detail="Empty body")

    ts = int(time.time() * 1000)
    tmp_adpcm_path = os.path.join(UPLOAD_DIR, f"upload_{ts}.adpcm")
    tmp_wav_path = os.path.join(UPLOAD_DIR, f"upload_{ts}.wav")

    # salva raw adpcm - opcional debug
    with open(tmp_adpcm_path, "wb") as f:
        f.write(body)

    if "audio/adpcm" in content_type:
        # decodifica ADPCM para PCM16
        try:
            pcm = decode_ima_adpcm(body)
            write_wav_from_pcm16(tmp_wav_path, pcm, sample_rate=8000)
        except Exception as e:
            raise HTTPException(status_code=500, detail=f"ADPCM decode error: {e}")
    else:
        # se não é adpcm, podemos tentar salvar como WAV direto (se cliente mandou WAV)
        try:
            with open(tmp_wav_path, "wb") as f:
                f.write(body)
        except Exception as e:
            raise HTTPException(status_code=400, detail=f"Unsupported content-type and failed to save: {e}")

    # agora use sua pipeline existente para transcrever
    recognized_norm = await asyncio.to_thread(transcrever_fala, tmp_wav_path)

    if recognized_norm in ("incompreensivel", "erro", ""):
        return JSONResponse({
            "recognized": recognized_norm,
            "accepted": False,
            "levenshtein": None,
            "expected": expected
        })

    lev = None
    accepted = False
    if expected:
        lev = levenshtein(recognized_norm, expected)
        if recognized_norm == expected or lev <= 1:
            accepted = True

    to_send = expected if accepted and expected else recognized_norm

    return JSONResponse({
        "recognized": recognized_norm,
        "accepted": accepted,
        "levenshtein": lev,
        "to_send": to_send,
        "expected": expected
    })

if __name__ == "__main__":
    uvicorn.run("server:app", host="0.0.0.0", port=8000, workers=1)