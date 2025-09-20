# python3 listen_serial.py COM7

import serial
import random
import time
import os
import wave
import struct
import sys
import unicodedata
import re
import speech_recognition as sr
from pydub import AudioSegment, effects, silence

# Configurações
porta_serial = sys.argv[1] if len(sys.argv) > 1 else 'COM7'
baudrate = 115200
SAMPLE_RATE = 8000
SAMPLE_WIDTH = 2

r = sr.Recognizer()

# ---------- Helpers ----------
def normalize_string(s: str) -> str:
    """
    Normaliza uma string:
    - lowercase
    - remove acentos (NFKD)
    - mantém apenas letras a-z
    - junta sequências de letras separadas (ex: "t e s t e" -> "teste")
    Retorna a forma *sem espaços* (porque a palavra esperada é única).
    """
    if not s:
        return ""
    s = s.lower().strip()
    s = unicodedata.normalize('NFKD', s)
    s = ''.join(ch for ch in s if not unicodedata.combining(ch))
    # mantém apenas letras e espaços
    s = re.sub(r'[^a-z\s]', '', s)
    # split + juntar sequências de tokens de 1 letra (ex.: "t e s t e" -> "teste")
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
    # como estamos lidando com palavras únicas, retornamos sem espaços
    return ''.join(merged)

def levenshtein(a: str, b: str) -> int:
    """Distância de Levenshtein (edit distance)."""
    if a == b:
        return 0
    n, m = len(a), len(b)
    if n == 0:
        return m
    if m == 0:
        return n
    # matrix (n+1) x (m+1)
    prev = list(range(m + 1))
    for i in range(1, n + 1):
        cur = [i] + [0] * m
        ai = a[i - 1]
        for j in range(1, m + 1):
            cost = 0 if ai == b[j - 1] else 1
            cur[j] = min(prev[j] + 1,      # deletion
                         cur[j - 1] + 1,   # insertion
                         prev[j - 1] + cost)  # substitution
        prev = cur
    return prev[m]

# ---------- Áudio / transcrição ----------
def preprocessar_audio(caminho_entrada):
    """Remove silêncios, normaliza e resample para 16000 Hz (melhora ASR)."""
    audio = AudioSegment.from_file(caminho_entrada, format="wav")
    audio = effects.normalize(audio)
    audio_chunks = silence.split_on_silence(
        audio,
        min_silence_len=700,
        silence_thresh=audio.dBFS - 14,
        keep_silence=300
    )
    if not audio_chunks:
        # nada cortado, apenas converte para 16k mono
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

def transcrever_fala(caminho_arquivo):
    """Faz a transcrição com Google Speech e retorna versão normalizada (lower, sem acento)."""
    caminho_processado = preprocessar_audio(caminho_arquivo)
    with sr.AudioFile(caminho_processado) as arq_audio:
        try:
            r.adjust_for_ambient_noise(arq_audio, duration=0.5)
            audio = r.record(arq_audio)
            texto = r.recognize_google(audio, language='pt-BR')
            print("[ASR] Original:", texto)
            texto_norm = normalize_string(texto)
            print("[ASR] Normalizado:", texto_norm)
            return texto_norm
        except sr.UnknownValueError:
            print("[ASR] Incompreensível")
            return "incompreensivel"
        except sr.RequestError as e:
            print(f"[ASR] Erro serviço: {e}")
            return "erro"

# ---------- Arquivos de palavras ----------
def carregar_palavras(nivel):
    """
    Carrega lista de palavras do arquivo correspondente ao nível.
    nivel=1 -> palavras_5.txt, nivel=2 -> palavras_6.txt etc.
    """
    base_dir = os.path.dirname(os.path.abspath(__file__))
    letras = nivel + 4
    caminho = os.path.join(base_dir, "..", "dataset", f"palavras_{letras}.txt")
    if not os.path.exists(caminho):
        print(f"[WARN] {caminho} não encontrado. Utilizando palavras_5.txt")
        caminho = os.path.join(base_dir, "..", "dataset", "palavras_5.txt")
    with open(caminho, "r", encoding="utf-8") as f:
        return [linha.strip() for linha in f if linha.strip()]

# ---------- Gravação via serial -> .wav ----------
def gravar_audio(ser):
    print("[INFO] Aguardando início da gravação pelo botão A...")
    ser.timeout = 0.1

    # aguarda primeiro byte do microfone
    while True:
        raw = ser.read()
        if raw:
            print("[INFO] Iniciando gravação...")
            break

    buffer = []
    sem_dados = 0
    LIMITE_ESPERA = 50  # aprox ~5s de silêncio

    while True:
        raw = ser.read()
        if raw:
            byte = raw[0]
            signed = int((byte - 128) * 256)
            buffer.append(signed)
            sem_dados = 0
        else:
            sem_dados += 1
            if sem_dados >= LIMITE_ESPERA:
                print("[INFO] Fim da captura detectado.")
                break

    print(f"[INFO] Captura finalizada. Amostras: {len(buffer)}")
    caminho_voz = os.path.join(os.path.dirname(os.path.abspath(__file__)), "voz.wav")
    with wave.open(caminho_voz, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(SAMPLE_WIDTH)
        wf.setframerate(SAMPLE_RATE)
        for s in buffer:
            wf.writeframes(struct.pack('<h', s))
    return caminho_voz

# ---------- Main ----------
def main():
    ser = serial.Serial(porta_serial, baudrate, timeout=1)
    time.sleep(2)
    print("[INFO] Aguardando requisição da Pico...")

    try:
        while True:
            if ser.in_waiting:
                linha = ser.readline().decode("utf-8").strip()
                if linha.startswith("pedir_palavra"):
                    partes = linha.split()
                    nivel = 1
                    if len(partes) > 1:
                        try:
                            nivel = int(partes[1])
                        except ValueError:
                            pass

                    palavras = carregar_palavras(nivel)
                    palavra = random.choice(palavras)
                    expected_norm = normalize_string(palavra)

                    # Envia a palavra NORMALIZADA — assim o buffer na Pico fica na mesma forma
                    # (lowercase, sem acentos). Se você preferir mostrar ao usuário a palavra com
                    # acentos no display, aí precisaríamos ajustar o main.c para comparar de forma
                    # case/diacritics-insensitive.
                    print(f"[INFO] Nível {nivel} | palavra escolhida: '{palavra}' -> '{expected_norm}'")
                    ser.write((expected_norm + "\n").encode("utf-8"))

                    # grava áudio enviado pela Pico
                    print("[INFO] Aguardando áudio da Pico...")
                    caminho = gravar_audio(ser)

                    # transcreve (já normalizado)
                    recognized_norm = transcrever_fala(caminho)

                    # se incompreensível ou erro, apenas encaminha isso
                    if recognized_norm in ("incompreensivel", "erro", ""):
                        to_send = recognized_norm
                        print(f"[INFO] Resultado ASR: {to_send}")
                    else:
                        # compara e permite pequenas diferenças (p.ex.: s <-> f)
                        lev = levenshtein(recognized_norm, expected_norm)
                        if recognized_norm == expected_norm or lev <= 1:
                            # Aceita pequeno erro: enviamos a palavra correta para a Pico
                            # (assim o main.c fará strstr e reconhecerá como correta).
                            print(f"[INFO] Aceito (lev={lev}). Enviando palavra correta: '{expected_norm}'")
                            to_send = expected_norm
                        else:
                            print(f"[INFO] Não aceito (lev={lev}). Enviando transcrição: '{recognized_norm}'")
                            to_send = recognized_norm

                    ser.write((to_send + "\n").encode("utf-8"))

    except KeyboardInterrupt:
        print("Encerrando...")
    finally:
        ser.close()

if __name__ == "__main__":
    main()