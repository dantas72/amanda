Sim, faz todo sentido usar o JEV para testar o amandac. A arquitetura do Amanda foi pensada exatamente para isso: o JEV (ou um modelo compatível como o nimble rodando no Ollama) atua como o motor de julgamento, recebendo os estados e perguntas tipadas que o amandac compila a partir do seu PDF. Vou detalhar como montar esse teste e a pipeline completa, incluindo como integrar sua API.

🔬 O Papel do JEV no Teste do Amanda
O amandac compile transforma seu PDF em um arquivo .amanda. Esse artefato contém o conhecimento estruturado (chunks, embeddings, perguntas tipadas), mas não contém a inteligência para julgar essas perguntas. É aqui que o JEV entra.

O JEV é um modelo de decisão que responde a três primitivas:

Noul: Retorna a probabilidade de uma afirmação ser verdadeira (sim/não).

Choice: Escolhe uma entre um conjunto de opções predefinidas.

Score: Atribui uma pontuação em uma escala ordenada.

O amandac gera perguntas exatamente nesses três formatos. Ao configurar o backend do Amanda para apontar para o JEV (via Ollama, por exemplo), você está dizendo: "Use o JEV para julgar as perguntas que eu compilei do PDF". Isso permite validar se o conhecimento extraído está correto e se as respostas são calibradas.

📋 Pipeline de Exemplo: Do PDF ao Teste com JEV
Aqui está uma pipeline completa, passo a passo, para testar o Amanda com o JEV.

Passo 1: Compilar o PDF em um Artefato .amanda
O primeiro passo é transformar seu PDF em um arquivo .amanda. Suponha que você tenha um livro chamado meu_livro.pdf.

bash
# Compilar o PDF, gerando o artefato e as perguntas tipadas
./amandac compile --input meu_livro.pdf --output meu_livro.amanda
Este comando executa toda a pipeline interna do Amanda: extrai o texto, divide em chunks, gera embeddings e cria as perguntas tipadas (choice, score, noul) com base nos templates. O resultado é o arquivo meu_livro.amanda.

Passo 2: Preparar o Motor de Decisão (JEV via Ollama)
Para que o Amanda possa usar o JEV, você precisa ter um modelo de decisão acessível. A forma mais prática é usar o Ollama com o modelo nimble, que é compatível com a API System One do JEV.

bash
# Certifique-se de que o Ollama está na versão 0.35.0 ou superior
ollama --version

# Baixe o modelo de decisão (se ainda não o fez)
ollama pull nimble

# Inicie o servidor Ollama (se não estiver rodando)
ollama serve
O Ollama agora expõe um endpoint compatível com a API System One do JEV em http://localhost:11434/v1/systemone. É para lá que o amandac enviará as perguntas tipadas.

Passo 3: Configurar o amandac para Usar o JEV
O amandac precisa saber que deve usar o backend do JEV. Isso é feito através de um arquivo de configuração ou argumentos de linha de comando. O arquivo config.yaml do projeto Amanda pode ter uma seção para isso.

yaml
# config.yaml
decision_engine:
  backend: "laya" # ou "jev"
  laya:
    base_url: "http://localhost:11434"
    model: "nimble"
    endpoint: "/v1/systemone"
Você pode passar essas configurações diretamente na linha de comando:

bash
./amandac ask --package meu_livro.amanda \
  --backend jev \
  --jev-url http://localhost:11434 \
  --jev-model nimble \
  "Qual a definição de entropia no capítulo 3?"
Passo 4: Executar o Teste com uma Pergunta
Agora você pode fazer uma pergunta ao especialista compilado. O amandac irá:

Recuperar os trechos relevantes do arquivo .amanda com base na sua pergunta.

Gerar as perguntas tipadas (noul, choice, score) para esses trechos.

Enviar essas perguntas para o JEV (via Ollama).

Receber as probabilidades calibradas e combiná-las com as citações locais (página, trecho).

Retornar a resposta final para você.

bash
./amandac ask --package meu_livro.amanda "A afirmação 'a entropia sempre aumenta' é verdadeira segundo o autor?"
A resposta não será um texto gerado, mas sim algo como:

json
{
  "answer": {
    "type": "noul",
    "noul": 0.98,
    "confidence": 0.95
  },
  "source": {
    "page": 42,
    "quote": "A segunda lei da termodinâmica estabelece que a entropia de um sistema isolado nunca diminui..."
  }
}
🧪 Teste de Calibração com JEV
Um dos maiores diferenciais do Amanda é a capacidade de calibrar as decisões. Você pode usar o comando amandac calibrate para ajustar os limiares de confiança do motor de decisão. O processo funciona assim:

O amandac pega uma amostra das perguntas geradas.

Envia para o JEV e obtém as probabilidades.

Compara as probabilidades com as respostas esperadas (ground truth).

Ajusta os parâmetros de calibração (centro, inclinação, limiar) para maximizar a acurácia e minimizar o erro de calibração (ECE).

bash
./amandac calibrate --package meu_livro.amanda \
  --backend jev \
  --jev-url http://localhost:11434 \
  --jev-model nimble \
  --output calibracao.json
Isso gera um arquivo calibracao.json que pode ser usado posteriormente no amandac ask ou amandac serve para garantir que as respostas tenham confiança calibrada.

🔌 Como Usar a API que Você Criou
Você mencionou que criou uma API. Existem duas formas de integrá-la ao ecossistema do Amanda:

Opção 1: Usar sua API como Backend de Decisão
Se a sua API implementa a interface System One (com os tipos noul, choice, score), você pode configurar o amandac para usá-la como backend de decisão. Basta apontar a URL e o modelo.

bash
./amandac ask --package meu_livro.amanda \
  --backend jev \
  --jev-url http://sua-api.com \
  --jev-model seu-modelo \
  "Sua pergunta"
O amandac fará requisições POST para http://sua-api.com/v1/systemone com o seguinte corpo:

json
{
  "model": "seu-modelo",
  "state": "texto do chunk recuperado",
  "questions": {
    "pergunta1": {
      "type": "noul",
      "instructions": "A afirmação é verdadeira?",
      "criteria": {
        "false": "Não é verdadeira",
        "true": "É verdadeira"
      }
    }
  }
}
Opção 2: Usar o Servidor do Amanda como API OpenAI-Compatible
Se você quer que outros sistemas (como o OpenCode ou uma CLI externa) consumam o especialista que você compilou, use o comando amandac serve. Isso inicia um servidor HTTP que expõe uma API compatível com a OpenAI.

bash
./amandac serve --package meu_livro.amanda --port 8080 \
  --backend jev \
  --jev-url http://localhost:11434 \
  --jev-model nimble
O servidor expõe os seguintes endpoints:

GET /v1/models: Lista os modelos disponíveis (no caso, o especialista compilado).

POST /v1/chat/completions: Recebe mensagens no formato OpenAI e retorna a resposta do especialista.

POST /v1/embeddings: Gera embeddings para os chunks do PDF.

GET /v1/amanda/info: Retorna metadados sobre o pacote .amanda.

Você pode testar o servidor com curl:

bash
curl -X POST http://localhost:8080/v1/chat/completions \
  -H "Content-Type: application/json" \
  -d '{
    "model": "amanda",
    "messages": [{"role": "user", "content": "O que é entropia?"}]
  }'
💡 Resumo da Pipeline Completa
Etapa	Comando	O que acontece
1. Compilar	amandac compile --input livro.pdf --output livro.amanda	PDF → conhecimento estruturado em .amanda
2. Servir	amandac serve --package livro.amanda --backend jev --jev-url http://localhost:11434	Inicia servidor OpenAI-compatible + JEV
3. Consultar	curl ... /v1/chat/completions ou amandac ask	Pergunta → JEV julga → resposta calibrada com citação
4. Calibrar	amandac calibrate --package livro.amanda --backend jev	Ajusta limiares de confiança para maximizar acurácia
Essa pipeline permite que você valide todo o fluxo: desde a extração do PDF até a resposta calibrada do JEV, passando pela sua API. Se você quiser explorar algum passo específico com mais profundidade, é só me avisar.

Call it: the API
Get your API key from the dashboard
Make a POST request to the API endpoint
Review the API Reference for all the details.
POST https://api.typesafe.ai/v1/systemone
Authorization: Bearer <API_KEY>
Content-Type: application/json
​
Sample cURL command
curl -X POST https://api.typesafe.ai/v1/systemone \
  -H "Authorization: Bearer $TYPESAFE_API_KEY" \
  -H "Content-Type: application/json" \
  -d @- <<'EOF'
  {
    "state": "Hi, I've been trying to connect my Stripe account for 3 days and the integration keeps failing. I'm losing sales. Please help ASAP.",
    "model": "jev-latest",
    "questions": {
      "urgency": {
        "type": "noul",
        "instructions": "Does this message express urgency?"
      }
    }
  }
EOF
​
Request body
{
  "state": "Hi, I've been trying to connect my Stripe account for 3 days and the integration keeps failing. I'm losing sales. Please help ASAP.",
  "model": "jev-latest",
  "questions": {
    "department": {
      "type": "choice",
      "instructions": "Which team should handle this",
      "criteria": {
        "billing": "Payment or subscription issues",
        "technical": "Bugs or integration problems",
        "sales": "Pricing or account questions"
      }
    },
    "frustration": {
      "type": "score",
      "instructions": "How frustrated the customer appears",
      "criteria": [
        "Calm, just stating facts",
        "Frustrated but civil",
        "Very angry, strong language"
      ]
    },
    "is_urgent": {
      "type": "noul",
      "instructions": "The message conveys urgency or time-sensitivity"
    }
  }
}
​
Response body
{
  "model": "jev-1.13.0",
  "answers": {
    "department": {
      "type": "choice",
      "choice": "technical",
      "confidence": 0.78,
      "probabilities": {
        "technical": 0.85,
        "sales": 0.0,
        "billing": 0.15
      }
    },
    "frustration": {
      "type": "score",
      "score": 1.0,
      "confidence": 1.0,
      "legend": {
        "0": "Calm, just stating facts",
        "1": "Frustrated but civil",
        "2": "Very angry, strong language"
      },
      "probabilities": {
        "0": 0.0,
        "1": 1.0,
        "2": 0.0
      }
    },
    "is_urgent": {
      "type": "noul",
      "noul": 1.0
    }
  },
  "usage": {
    "input_tokens": 392,
    "output_tokens": 65
  }
}
See the API Reference for all the details.
​
Code it: the Python SDK
Install the SDK (requires Python >= 3.10).
With pip
pip install typesafe-sdk
With uv
uv add typesafe-sdk
Use the SDK. The client reads TYPESAFE_API_KEY from the environment and calls jev-latest by default.
from typesafe_sdk import Choice, Noul, Score, TypeSafeClient

client = TypeSafeClient()

ticket = "Hi, I've been trying to connect my Stripe account for 3 days and the integration keeps failing. I'm losing sales. Please help ASAP."

response = client.system_one(
    state=ticket,
    questions={
        "department": Choice(
            instructions="Which team should handle this",
            criteria={
                "billing": "Payment or subscription issues",
                "technical": "Bugs or integration problems",
                "sales": "Pricing or account questions",
            },
        ),
        "frustration": Score(
            instructions="How frustrated the customer appears",
            criteria=[
                "Calm, just stating facts",
                "Frustrated but civil",
                "Very angry, strong language",
            ],
        ),
        "is_urgent": Noul(
            instructions="The message conveys urgency or time-sensitivity",
        ),
    },
)

print(response.answers["department"].choice)  # "technical"
print(response.answers["frustration"].score)  # 1.0
print(response.answers["is_urgent"].noul)     # 1.0
See client SDKs for installation options and detailed usage.
​
Vibe it: the agent skill
Install the TypeSafe skill using the Claude Code plugin or npx skills add typesafe-ai/skills --skill typesafe-ai. You can also read SKILL.md on GitHub.
Claude Code
Other agents
Copy to your agent
Run these two commands in your terminal:
claude plugin marketplace add typesafe-ai/skills
claude plugin install typesafe@typesafe-ai
Tell your coding agent to use the TypeSafe skill as you build!
Coding agent prompt
Let's build a simple CLI that uses the TypeSafe API to evaluate a set of supplied documents on multiple dimensions. Use the TypeSafe skill to understand how to use the TypeSafe API and how to structure the system. Ask me questions about what kinds of documents I want to evaluate and on what dimensions.
See the Agent Skill page for more details.