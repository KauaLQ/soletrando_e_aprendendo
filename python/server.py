#!/usr/bin/env python3
# server.py - API HTTP para testar o main.c (Pico)
# Porta: 8000 (bind 0.0.0.0)
# Dependências: flask, werkzeug, speechrecognition, pydub

import os
import random
import time
import json
from pathlib import Path
from flask import Flask, request, jsonify, send_from_directory, abort, Response, render_template_string
import threading

from client import INDEX_HTML  # UI HTML/CSS (ver client.py)

# ASR / audio libs (copiado/adaptado do seu listen_serial.py)
import unicodedata, re
from pydub import AudioSegment, effects, silence
import speech_recognition as sr

BASE_DIR = Path(__file__).resolve().parent
UPLOAD_DIR = BASE_DIR / "uploads"
UPLOAD_DIR.mkdir(exist_ok=True)

r = sr.Recognizer()

app = Flask(__name__)

# sessões simples por nivel (map level -> state)
# Nota: chavear apenas por 'nivel' é simples e suficiente para testes locais.
sessions = {}  # ex: {1: {'raw_word':'Banana','expected':'banana', 'notified':False, 'result': None, ...}}

# ---------- utils (adaptadas do listen_serial.py) ----------
def normalize_string(s: str) -> str:
    if not s:
        return ""
    s = s.lower().strip()
    s = unicodedata.normalize('NFKD', s)
    s = ''.join(ch for ch in s if not unicodedata.combining(ch))
    s = re.sub(r'[^a-z\s]', '', s)
    tokens = s.split()
    merged = []
    i = 0
    while i < len(tokens):
        if len(tokens[i]) == 1:
            j = i
            seq = []
            while j < len(tokens) and len(tokens[j]) == 1:
                seq.append(tokens[j])
                j += 1
            if len(seq) > 1:
                merged.append(''.join(seq))
            else:
                merged.append(seq[0])
            i = j
        else:
            merged.append(tokens[i])
            i += 1
    return ''.join(merged)

def levenshtein(a: str, b: str) -> int:
    if a == b:
        return 0
    n, m = len(a), len(b)
    if n == 0: return m
    if m == 0: return n
    prev = list(range(m+1))
    for i in range(1, n+1):
        cur = [i] + [0]*m
        ai = a[i-1]
        for j in range(1, m+1):
            cost = 0 if ai == b[j-1] else 1
            cur[j] = min(prev[j] + 1, cur[j-1] + 1, prev[j-1] + cost)
        prev = cur
    return prev[m]

def carregar_palavras(nivel: int):
    letras = nivel + 4
    caminho = BASE_DIR.parent / "dataset" / f"palavras_{letras}.txt"
    if not caminho.exists():
        caminho = BASE_DIR.parent / "dataset" / "palavras_5.txt"
    with open(caminho, "r", encoding="utf-8") as f:
        return [l.strip() for l in f if l.strip()]

def preprocessar_audio(caminho_entrada: str) -> str:
    audio = AudioSegment.from_file(caminho_entrada, format="wav")
    audio = effects.normalize(audio)
    audio_chunks = silence.split_on_silence(
        audio,
        min_silence_len=700,
        silence_thresh=audio.dBFS - 14,
        keep_silence=300
    )
    if not audio_chunks:
        audio = audio.set_frame_rate(16000).set_channels(1)
        caminho_processado = caminho_entrada.replace(".wav", "_proc.wav")
        audio.export(caminho_processado, format="wav")
        return caminho_processado

    audio_final = AudioSegment.silent(duration=200)
    for chunk in audio_chunks:
        audio_final += chunk + AudioSegment.silent(duration=200)
    audio_final = audio_final.set_frame_rate(16000).set_channels(1)
    caminho_processado = caminho_entrada.replace(".wav", "_proc.wav")
    audio_final.export(caminho_processado, format="wav")
    return caminho_processado

def transcrever_fala(caminho_arquivo: str) -> str:
    caminho_processado = preprocessar_audio(caminho_arquivo)
    try:
        with sr.AudioFile(caminho_processado) as arq_audio:
            r.adjust_for_ambient_noise(arq_audio, duration=0.5)
            audio = r.record(arq_audio)
            texto = r.recognize_google(audio, language='pt-BR')
            texto_norm = normalize_string(texto)
            app.logger.info(f"[ASR] raw='{texto}' -> norm='{texto_norm}'")
            return texto_norm
    except sr.UnknownValueError:
        app.logger.info("[ASR] Incompreensível")
        return "incompreensivel"
    except sr.RequestError as e:
        app.logger.exception("[ASR] RequestError")
        return "erro"
    
def _background_process_and_set_result(path, nivel, path_proc):
    try:
        recognized_norm = transcrever_fala(path)
        app.logger.info(f"Transcrito (raw, bg): {recognized_norm}")

        if nivel in sessions and sessions[nivel].get('expected'):
            expected = sessions[nivel]['expected']
            if recognized_norm in ("incompreensivel", "erro", ""):
                to_send = recognized_norm
            else:
                lev = levenshtein(recognized_norm, expected)
                if recognized_norm == expected or lev <= 1:
                    to_send = expected
                else:
                    to_send = recognized_norm
            sessions[nivel]['result'] = to_send
            sessions[nivel]['updated_at'] = time.time()
        else:
            sessions[nivel]['result'] = recognized_norm
            sessions[nivel]['updated_at'] = time.time()
        try:
            os.remove(path)
            os.remove(path_proc)
        except:
            pass
    except Exception:
        app.logger.exception("Erro em background_process")

# ---------- Endpoints API ----------

@app.route("/pedir_palavra", methods=["GET"])
def pedir_palavra():
    try:
        nivel = int(request.args.get("nivel", "1"))
    except:
        nivel = 1
    palavras = carregar_palavras(nivel)
    palavra = random.choice(palavras)
    expected = normalize_string(palavra)
    sessions[nivel] = {
        'raw_word': palavra,
        'expected': expected,
        'notified': False,
        'result': None,
        'updated_at': time.time()
    }
    app.logger.info(f"/pedir_palavra nivel={nivel} -> {palavra} -> {expected}")
    return Response(expected, status=200, mimetype="text/plain")

@app.route("/notify_audio", methods=["POST", "GET"])
def notify_audio():
    # aceita JSON ou query param
    nivel = None
    if request.method == "GET":
        try:
            nivel = int(request.args.get("nivel", "1"))
        except:
            nivel = 1
    else:
        data = request.get_json(silent=True) or request.form or request.args
        try:
            nivel = int(data.get("nivel", 1))
        except:
            nivel = 1
    if nivel not in sessions:
        return jsonify({"ok": False, "error": "nenhuma sessão para esse nivel"}), 400
    sessions[nivel]['notified'] = True
    sessions[nivel]['updated_at'] = time.time()
    app.logger.info(f"/notify_audio nivel={nivel}")
    return jsonify({"ok": True})

@app.route("/upload_audio", methods=["POST"])
def upload_audio():
    # Recebe multipart form: file + optional nivel
    if 'file' not in request.files:
        return jsonify({"ok": False, "error": "arquivo não foi enviado (campo 'file')"}), 400
    file = request.files['file']
    try:
        nivel = int(request.args.get("nivel", request.form.get("nivel", 1)))
    except:
        nivel = 1

    filename = f"voz_n{nivel}_{int(time.time())}.wav"
    path = UPLOAD_DIR / filename
    filename_proc = f"voz_n{nivel}_{int(time.time())}_raw_proc.wav"
    path_proc = UPLOAD_DIR / filename_proc
    file.save(path)
    app.logger.info(f"Arquivo salvo: {path}")

    # processa e transcreve
    recognized_norm = transcrever_fala(str(path))
    app.logger.info(f"Transcrito: {recognized_norm}")
    try:
        os.remove(path)
        os.remove(path_proc)
    except:
        pass

    # decide resposta
    if nivel in sessions and sessions[nivel].get('expected'):
        expected = sessions[nivel]['expected']
        if recognized_norm in ("incompreensivel", "erro", ""):
            to_send = recognized_norm
        else:
            lev = levenshtein(recognized_norm, expected)
            if recognized_norm == expected or lev <= 1:
                to_send = expected
            else:
                to_send = recognized_norm
        sessions[nivel]['result'] = to_send
        sessions[nivel]['updated_at'] = time.time()
    else:
        # sem sessão prévia: só retorna a transcrição
        to_send = recognized_norm

    return jsonify({"ok": True, "result": to_send})

@app.route("/resultado", methods=["GET"])
def resultado():
    try:
        nivel = int(request.args.get("nivel", "1"))
    except:
        nivel = 1
    s = sessions.get(nivel)
    # se não há sessão ou ainda não tem result, devolve um corpo simples "processing"
    if not s or s.get('result') is None:
        return Response("processing", status=200, mimetype="text/plain")
    # quando há resultado, devolve ele
    return Response(s['result'], status=200, mimetype="text/plain")

@app.route("/upload_audio_raw", methods=["POST"])
def upload_audio_raw():
    try:
        nivel = int(request.args.get("nivel", request.form.get("nivel", 1)))
    except:
        nivel = 1

    data = request.get_data()
    if not data:
        return jsonify({"ok": False, "error": "no data"}), 400

    filename = f"voz_n{nivel}_{int(time.time())}_raw.wav"
    path = UPLOAD_DIR / filename
    filename_proc = f"voz_n{nivel}_{int(time.time())}_raw_proc.wav"
    path_proc = UPLOAD_DIR / filename_proc
    with open(path, "wb") as f:
        f.write(data)
    app.logger.info(f"Arquivo raw salvo: {path}")

    # dispara processamento em background
    t = threading.Thread(target=_background_process_and_set_result, args=(str(path), nivel, str(path_proc)))
    t.daemon = True
    t.start()

    # responde imediatamente SEM CORPO; o cliente deve fazer polling em /resultado
    return '', 202

@app.route("/stream")
def stream():
    def event_stream():
        last_snapshot = None
        while True:
            # Captura um snapshot simples das sessões
            snapshot = {
                k: {
                    "raw_word": v.get("raw_word"),
                    "expected": v.get("expected"),
                    "result": v.get("result")
                } for k, v in sessions.items()
            }

            if snapshot != last_snapshot:
                # envia update apenas quando mudar
                yield f"data: {json.dumps(snapshot)}\n\n"
                last_snapshot = snapshot

            time.sleep(0.5)  # não pesar CPU
    return Response(event_stream(), mimetype="text/event-stream")

@app.route("/", methods=["GET"])
def index():
    simplified = {k: {'raw_word': v['raw_word'], 'expected': v['expected'], 'result': v.get('result')} for k,v in sessions.items()}
    return render_template_string(INDEX_HTML, sessions=simplified)

if __name__ == "__main__":
    # Rodar em 0.0.0.0:8000 para ser acessível pela Pico na sua rede
    # rode com run apenas em ambiente de teste/desenvolvimento
    app.run(host="0.0.0.0", port=8000, debug=False)