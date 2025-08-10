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

# Configurações
porta_serial = sys.argv[1] if len(sys.argv) > 1 else 'COM6'
baudrate = 115200
SAMPLE_RATE = 8000
SAMPLE_WIDTH = 2

# Caminho de palavras
base_dir = os.path.dirname(os.path.abspath(__file__))
caminho_palavras = os.path.join(base_dir, "..", "dataset", "palavras.txt")

# Carrega palavras
with open(caminho_palavras, "r", encoding="utf-8") as f:
    palavras = [linha.strip() for linha in f if linha.strip()]

# Reconhecedor de voz
r = sr.Recognizer()

def transcrever_fala(caminho_arquivo):
    with sr.AudioFile(caminho_arquivo) as arq_audio:
        print("Ajustando ruído...")
        r.adjust_for_ambient_noise(arq_audio)
        print("Escutando...")
        audio = r.listen(arq_audio)

        print("Processando...")
        try:
            texto = r.recognize_google(audio, language='pt-BR')
            print("Você disse:", texto)
            return texto
        except sr.UnknownValueError:
            print("Não entendi o que você disse.")
            return "Incompreensivel"
        except sr.RequestError as e:
            print(f"Erro no serviço de voz: {e}")
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
    LIMITE_ESPERA = 20  # 2 segundos de silêncio

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

                    # ser.write("Transcrevendo...\n".encode("utf-8"))
                    resultado = transcrever_fala(caminho)

                    print(f"[INFO] Enviando transcrição: {resultado}")
                    ser.write((resultado + "\n").encode("utf-8"))

    except KeyboardInterrupt:
        print("Encerrando...")
    finally:
        ser.close()

if __name__ == "__main__":
    main()