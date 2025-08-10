import serial
import random
import time
import os

base_dir = os.path.dirname(os.path.abspath(__file__))
caminho_palavras = os.path.join(base_dir, "..", "dataset", "palavras.txt")

with open(caminho_palavras, "r", encoding="utf-8") as f:
    palavras = [linha.strip() for linha in f if linha.strip()]

porta_serial = 'COM6'  # Altere conforme necessário
baudrate = 115200

ser = serial.Serial(porta_serial, baudrate, timeout=1)
time.sleep(2)

print("Aguardando requisição da Pico...")

try:
    while True:
        if ser.in_waiting:
            linha = ser.readline().decode("utf-8").strip()
            if linha == "pedir_palavra":
                palavra = random.choice(palavras)
                print(f"Enviando: {palavra}")
                ser.write((palavra + "\n").encode("utf-8"))
except KeyboardInterrupt:
    print("Encerrando...")
finally:
    ser.close()