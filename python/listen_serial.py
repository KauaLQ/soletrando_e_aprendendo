# python3 sistema_principal.py COM6

import serial
import random
import time
import os
import wave
import struct
import sys
import numpy as np
import speech_recognition as sr
import re
from pydub import AudioSegment, effects, silence

# Configurações
porta_serial = sys.argv[1] if len(sys.argv) > 1 else 'COM6'
baudrate = 115200
SAMPLE_RATE = 8000
SAMPLE_WIDTH = 2

# Caminho de palavras
base_dir = os.path.dirname(os.path.abspath(__file__))
caminho_palavras = os.path.join(base_dir, "..", "dataset", "palavras_5.txt")

# Carrega palavras
with open(caminho_palavras, "r", encoding="utf-8") as f:
    palavras = [linha.strip() for linha in f if linha.strip()]

# Reconhecedor de voz
r = sr.Recognizer()

def preprocessar_audio(caminho_entrada):
    """Remove silêncios, normaliza volume e ajusta taxa de amostragem para melhorar transcrição."""
    audio = AudioSegment.from_file(caminho_entrada, format="wav")

    # Normaliza volume
    audio = effects.normalize(audio)

    # Remove silêncios muito longos (mais de 700 ms abaixo de -40 dBFS)
    audio_chunks = silence.split_on_silence(
        audio,
        min_silence_len=700,
        silence_thresh=audio.dBFS - 14,
        keep_silence=300  # mantém um pouco para não cortar palavras
    )
    if not audio_chunks:
        return caminho_entrada  # Não conseguiu cortar nada, usa o original

    audio_final = AudioSegment.silent(duration=200)
    for chunk in audio_chunks:
        audio_final += chunk + AudioSegment.silent(duration=200)

    # Converte para mono 16 kHz para melhorar o Google API
    audio_final = audio_final.set_frame_rate(16000).set_channels(1)

    caminho_processado = caminho_entrada.replace(".wav", "_proc.wav")
    audio_final.export(caminho_processado, format="wav")
    return caminho_processado

def transcrever_fala(caminho_arquivo):
    """Transcreve o áudio usando Google Speech apenas."""
    caminho_processado = preprocessar_audio(caminho_arquivo)

    with sr.AudioFile(caminho_processado) as arq_audio:
        print("Ajustando ruído...")
        r.adjust_for_ambient_noise(arq_audio, duration=0.5)
        print("Escutando...")
        audio = r.record(arq_audio)

        print("Processando com Google Speech...")
        try:
            texto = r.recognize_google(audio, language='pt-BR')
            print("Você disse (original):", texto)

            # Remove espaços entre letras (ex.: "t e m e r" -> "temer")
            texto_processado = re.sub(r"(?:^| )([a-zA-Z])(?: (?=[a-zA-Z]))", r"\1", texto.replace(" ", " ")).replace(" ", "")
            
            print("Você disse (processado):", texto_processado)
            return texto_processado

        except sr.UnknownValueError:
            print("[AVISO] O Google não entendeu o que foi dito.")
            return "Incompreensivel"
        except sr.RequestError as e:
            print(f"[ERRO] Serviço de voz indisponível ou falhou: {e}")
            return "Erro"

def gravar_audio(ser):
    print("[INFO] Aguardando início da gravação pelo botão A...")
    ser.timeout = 0.1

    # Aguarda os primeiros dados do microfone
    while True:
        raw = ser.read()
        if raw:
            print("[INFO] Iniciando gravação...")
            break

    buffer = []
    sem_dados = 0
    LIMITE_ESPERA = 50  # ~5 segundos de silêncio antes de encerrar

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

    print(f"[INFO] Captura finalizada. Total de amostras: {len(buffer)}")

    caminho_voz = os.path.join(base_dir, "voz.wav")
    with wave.open(caminho_voz, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(SAMPLE_WIDTH)
        wf.setframerate(SAMPLE_RATE)
        for s in buffer:
            wf.writeframes(struct.pack('<h', s))

    return caminho_voz

def main():
    ser = serial.Serial(porta_serial, baudrate, timeout=1)
    time.sleep(2)

    print("Aguardando requisição da Pico...")

    try:
        while True:
            if ser.in_waiting:
                linha = ser.readline().decode("utf-8").strip()
                if linha == "pedir_palavra":
                    palavra = random.choice(palavras)
                    print(f"[INFO] Enviando palavra: {palavra}")
                    ser.write((palavra + "\n").encode("utf-8"))

                    print("[INFO] Aguardando áudio da Pico...")
                    caminho = gravar_audio(ser)

                    resultado = transcrever_fala(caminho)

                    print(f"[INFO] Enviando transcrição: {resultado}")
                    ser.write((resultado + "\n").encode("utf-8"))

    except KeyboardInterrupt:
        print("Encerrando...")
    finally:
        ser.close()

if __name__ == "__main__":
    main()