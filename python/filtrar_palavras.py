# Caminho para o arquivo original
arquivo = "dataset/palavras.txt"

# Tamanho desejado
tamanho_desejado = 5

# Lê o arquivo e filtra
with open(arquivo, "r", encoding="utf-8") as f:
    palavras = f.read().split()

resultado = [p for p in palavras if len(p) == tamanho_desejado]

# Gera o novo arquivo
saida = f"dataset/palavras_{tamanho_desejado}.txt"
with open(saida, "w", encoding="utf-8") as f:
    # Cada palavra em uma linha
    f.write("\n".join(resultado))

print(f"Arquivo gerado: {saida}")