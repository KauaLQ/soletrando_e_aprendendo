# client.py
# Conteúdo: string INDEX_HTML com HTML/CSS para o UI de testes (render_template_string do Flask pode usar)
# Salve este arquivo no mesmo diretório do server.py e, no server, faça: from client import INDEX_HTML

INDEX_HTML = r"""
<!doctype html>
<html lang="pt-BR">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <title>Pico Game — UI</title>
  <link href="https://fonts.googleapis.com/css2?family=Montserrat:wght@300;600;800&display=swap" rel="stylesheet">
  <script src="https://kit.fontawesome.com/a264ca8e95.js" crossorigin="anonymous"></script>
  <style>
    :root{
      --pink:#ff2676;
      --muted:#f7f7f8;
      --card:#ffffff;
      --shadow: 0 8px 18px rgba(20,20,40,0.08);
      --accent:#76e08a;
      --glass: rgba(255,255,255,0.7);
      font-family: 'Montserrat', system-ui, -apple-system, "Segoe UI", Roboto, "Helvetica Neue", Arial;
    }
    *{box-sizing:border-box}
    body{margin:0;background:linear-gradient(180deg,#fafafa 0%, #fff 100%);color:#222}
    header{background:#fff;padding:18px 36px;display:flex;align-items:center;justify-content:space-between;box-shadow:0 4px 12px rgba(10,10,30,0.04);position:sticky;top:0;z-index:10}
    input[type="file"] {background: #fff; border: 1px solid #ddd; padding: 6px; border-radius: 6px; cursor: pointer;}
    input[type="number"] {width:60px;padding:6px;border:1px solid #ddd;border-radius:6px}
    i{margin-right:6px}
    .nav{display:flex;gap:20px;align-items:center}
    .logo{font-weight:800;color:#111}
    .btn-contact{background:linear-gradient(90deg,var(--pink),#ff4a95);color:#fff;padding:10px 16px;border-radius:26px;border:none;cursor:pointer}

    .hero{padding:56px 36px;background:linear-gradient(90deg, rgba(255,255,255,0.5), rgba(255,255,255,0.2));display:flex;gap:40px;align-items:center}
    .hero-left{flex:1}
    .hero h1{font-size:48px;margin:0 0 18px;line-height:1.02}
    .hero p{margin:0 0 20px;color:#444}
    .cta{display:flex;gap:12px}
    .btn{padding:12px 18px;border-radius:24px;border:none;cursor:pointer;text-decoration:none}
    .btn-primary{background:var(--pink);color:white}
    .btn-ghost{background:#fff;border:1px solid #eee}

    .card-wrap{padding:28px;margin:24px 36px;background:var(--card);box-shadow:var(--shadow);border-radius:12px}
    .features{display:flex;gap:16px;flex-wrap:wrap}
    .feature{flex:1;min-width:180px;background:linear-gradient(180deg,#fff,#fbfbff);padding:18px;border-radius:10px;box-shadow:0 6px 14px rgba(0,0,0,0.03)}
    .feature h4{margin:0 0 8px}

    .file-input {
      padding: 8px;
      border: 2px solid #ff2676;
      border-radius: 24px;
      background: #fff;
      color: #444;
      cursor: pointer;
    }

    .file-input::file-selector-button {
      background: #ff2676;
      color: #fff;
      border: none;
      padding: 8px 14px;
      margin-right: 10px;
      border-radius: 20px;
      cursor: pointer;
      font-weight: 600;
      transition: 0.2s;
    }

    .file-input::file-selector-button:hover {
      background: #ff4a95;
    }

    .pricing{display:flex;gap:18px;padding:36px}
    .plan{background:var(--card);padding:20px;border-radius:12px;flex:1;box-shadow:var(--shadow)}
    .plan h3{margin-top:0}
    .plan .enroll{display:inline-block;margin-top:12px;padding:10px 14px;border-radius:20px;background:var(--pink);color:#fff;text-decoration:none}

    footer{padding:32px;text-align:center;color:#666;background:#fff;box-shadow:24px 4px 12px 8px rgba(10,10,30,0.04)}

    /* small screens */
    @media (max-width:800px){
      .hero{flex-direction:column;align-items:flex-start}
      .pricing{flex-direction:column}
    }

    /* small utilities */
    ul.sessions{list-style:none;padding:0;margin:0}
    ul.sessions li{padding:8px 0;border-bottom:1px dashed #eee}
    .meta{font-size:13px;color:#666}
  </style>

  <script>
  const evtSource = new EventSource("/stream");

  evtSource.onmessage = function(event) {
      const data = JSON.parse(event.data);

      const ul = document.querySelector("ul.sessions");
      ul.innerHTML = "";

      if (Object.keys(data).length === 0) {
          ul.innerHTML = "<li>Nenhuma sessão ativa</li>";
          return;
      }

      for (const nivel in data) {
          const s = data[nivel];
          const li = document.createElement("li");
          li.innerHTML =
              `<strong>Nivel ${nivel}</strong> — 
              <span class="meta">palavra: ${s.raw_word || '—'} — resultado: ${s.result || '—'}</span>`;
          ul.appendChild(li);
      }
  };
  </script>
</head>
<body>
  <header>
    <div class="logo">Pico Game</div>
    <nav class="nav">
      <div class="meta">Início</div>
      <div class="meta">Sobre</div>
      <button class="btn-contact">CONTATE-NOS</button>
    </nav>
  </header>

  <section class="hero">
    <div class="hero-left">
      <h1>Soletrando e Aprendendo</h1>
      <p>Teste aqui os endpoints "pedir palavras", "enviar áudio" e visualize os resultados das sessões do Pico.</p>
      <div class="cta">
        <a class="btn btn-ghost" href="#sessions">Sessões</a>
        <a class="btn btn-primary" href="#upload">Enviar áudio</a>
      </div>
    </div>
    <div class="hero-right" style="width:360px;">
      <div style="background:linear-gradient(180deg, #fff, #fff);border-radius:12px;padding:18px;box-shadow:var(--shadow)">
        <h4 style="margin:0 0 8px">Sessões Ativas</h4>
        <ul class="sessions">
        {% for n,s in sessions.items() %}
          <li><strong>Nivel {{n}}</strong> — <span class="meta">palavra: {{s.raw_word or '—'}} — resultado: {{s.result or '—'}}</span></li>
        {% else %}
          <li>Nenhuma sessão ativa</li>
        {% endfor %}
        </ul>
      </div>
    </div>
  </section>

  <main>
    <div class="card-wrap">
      <h2>Ambiente Único de Aprendizagem</h2>
      <div class="features">
        <div class="feature"><h4>Sistema de Níveis</h4><p class="meta">Experimente a detecção de voz e testes baseados em níveis.</p></div>
        <div class="feature"><h4>Análise Inteligente</h4><p class="meta">Processamento local de áudio com normalização e remoção de silêncio.</p></div>
        <div class="feature"><h4>Ambiente Amigável</h4><p class="meta">Interface intuitiva para enviar arquivos .wav e ver resultados.</p></div>
      </div>
    </div>

    <section style="padding:20px 36px;" id="upload">
      <h3>Upload de áudio</h3>
      <form action="/upload_audio?nivel=1" method="post" enctype="multipart/form-data">
        <input type="file" name="file" id="file" accept=".wav" class="file-input"/>
        <button class="btn btn-ghost" style="background:#eee;margin-left:8px">ENVIAR</button>
      </form>

      <h3 style="margin-top:28px">Pedir palavra</h3>
      <form action="/pedir_palavra" method="get">
        <label for="nivel"><i class="fa-solid fa-up-down"></i>Nível (1-3):</label>
        <input type="number" min="1" max="3" name="nivel" value="1" />
        <button class="btn btn-ghost" style="background:#eee;margin-left:8px">PEDIR</button>
      </form>

      <h3 style="margin-top:28px">Notificar áudio pronto</h3>
      <form action="/notify_audio" method="post">
        <label for="nivel"><i class="fa-solid fa-up-down"></i>Nível (1-3):</label>
        <input type="number" min="1" max="3" name="nivel" value="1" />
        <button class="btn btn-ghost" style="background:#eee;margin-left:8px">NOTIFICAR</button>
      </form>

    </section>

  </main>

  <footer>
    Feito com ❤️ — Pico Game
  </footer>
</body>
</html>
"""