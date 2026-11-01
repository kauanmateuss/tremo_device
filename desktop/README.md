# Coletor e armazenamento desktop

Fluxo: BitDogLab/Pico W → Wi-Fi/MQTT → Mosquitto → Paho MQTT → SQLite.
A aplicação inclui recepção, armazenamento, consultas de métricas e
[dashboard PySide6/PyQtGraph](DASHBOARD.md). Para explorar a interface, instale
`python -m pip install -e 'desktop[gui]'` e execute
`python -m tremo_desktop gui --demo`. Para dados reais, use `--db ARQUIVO gui`.

## Executar

Na raiz do repositório, com Python 3.11 ou superior:

```sh
python3 -m venv desktop/.venv
source desktop/.venv/bin/activate
python -m pip install -e desktop
python -m tremo_desktop --db desktop/tremo.sqlite3 collect --host 192.168.1.215 --port 1883
```

Aguarde **Coletor pronto** antes de iniciar uma avaliação na placa. Ctrl+C encerra
o coletor. O banco fica no arquivo indicado, ignorado pelo Git.
Se o broker exigir autenticação, defina `TREMO_MQTT_USER` e
`TREMO_MQTT_PASSWORD` no ambiente antes de iniciar; não passe senha como argumento
nem a registre no Git. O código não carrega `.env` automaticamente.
`TREMO_MQTT_HOST`, `TREMO_MQTT_PORT` e `TREMO_MQTT_CLIENT_ID` também são opcionais.

Use um `--client-id` estável para recuperar a assinatura persistente após reiniciar.
Dois coletores simultâneos precisam de IDs diferentes. Reutilize o mesmo banco
com o mesmo ID: mudar/remover o banco não faz o broker reenviar dados já confirmados.
Mantenha o broker disponível e com persistência e limites de fila adequados.
Veja a [configuração do firmware e os limites de entrega](../docs/mqtt.md).

Consultas em outro terminal, com o ambiente virtual ativado:

```sh
python -m tremo_desktop --db desktop/tremo.sqlite3 sessions
python -m tremo_desktop --db desktop/tremo.sqlite3 report ID_DA_PLACA ID_DA_SESSAO
```

Os IDs vêm da primeira consulta. Estes comandos são ferramentas de inspeção;
a apresentação visual está disponível pelo comando `gui`.

## Organização e garantias

- `transport/protocol.py`: valida versão, IDs, tópico, tipos, limites e tempos.
- `transport/mqtt.py`: Paho com QoS 1, assinatura persistente, reconexão e ACK manual.
- `services/ingestion.py`: valida e persiste antes de autorizar confirmação.
- `storage/repository.py`: SQLite, esquema versionado, transações e consultas.

O coletor roda como processo independente; callbacks e banco usam uma única thread.
Não há widgets nos callbacks. A GUI consulta o banco em uma thread de leitura,
sem executar recepção de rede na thread gráfica. SQLite usa WAL para
permitir leitura durante a coleta, com `synchronous=FULL` para os commits.

`events` preserva o JSON aceito; `sessions`, `trials`, `samples` e `results` guardam
as informações estruturadas. A transação inclui evento e registros derivados.
A chave placa/sessão/sequência elimina reenvios; conteúdo conflitante é rejeitado.
Mensagens inválidas ficam em `rejected_messages` com motivo e hash, sem guardar o
payload bruto. O ACK só ocorre depois do commit da mensagem ou de sua rejeição.
Erro de disco/banco encerra o coletor sem ACK: corrija a causa e reinicie.

A sessão guarda horário UTC de recepção separado do relógio monotônico da placa.
Não se associa automaticamente um registro a uma pessoa pelo horário ou por IP;
o vínculo entre participante e sessão ficará para uma task específica.

## Dados preparados para o dashboard

`Repository.overview()` oferece configuração, estado, tempos/pausas, resumo por
módulo e qualidade. Médias e acurácia são calculadas com os registros de avaliação,
sem treino, e só são exibidas quando tentativas e resumo conferem. RTI separa
respostas, antecipações e omissões; PAL mantém níveis e rodadas. MPU informa
cobertura, perdas, falhas e calibração. `chart_samples()` retorna leituras com
unidades g e graus/s, preservando NULL para leituras inválidas.

A qualidade indica início/fim ausentes, lacunas de sequência, descartes e divergência
entre registros e resumos. Sessão interrompida é distinta de sessão concluída;
`quality.complete` descreve integridade dos dados, não conclusão da avaliação.
Ainda não calculamos frequência/intensidade clínica do tremor ou escores normativos.

Disponível no dashboard:

1. Visão geral de sessões com estado, duração e qualidade em destaque.
2. Cartões de desempenho por teste, incluindo treino separado, erros e omissões.
3. Gráficos dos dois MPUs com unidades, eixo temporal e indicação de lacunas.
4. Detalhes das tentativas disponíveis sob demanda, sem ocupar a tela principal.
5. Identificação clara de protocolo experimental adaptado, sem diagnóstico automático.

## Testar e aceitar a integração

```sh
python -m unittest discover -s desktop/tests -v
TREMO_TEST_MQTT=1 python -m unittest discover -s desktop/tests -p test_mqtt_integration.py -v
```

O segundo comando exige Mosquitto instalado e inicia um broker temporário somente
em localhost. Se o perfil `/etc/apparmor.d/mosquitto` estiver presente, o teste não inicia
processos automaticamente e exige um broker de testes previamente iniciado: acrescente `TREMO_TEST_PORT=PORTA_LOCAL`.
Não use um broker de produção para este teste: ele publica dados simulados no tópico
`0123456789abcdef`. O teste não altera arquivos de configuração do Mosquitto.

Critérios automatizados: JSON real dos quatro testes e MPU aceito; treino excluído
das métricas; duplicatas sem novos registros; conflitos rejeitados; gravação antes
do ACK; falha de armazenamento sem ACK; coleta recuperada depois de reiniciar o
subscriber; amostras preservadas com unidades e identificação de perdas.

A validação física de Wi-Fi, aquisição e temporização continua pendente. O teste
local usa firmware C com periféricos simulados, Paho e Mosquitto reais.

Referência de transporte: [Paho MQTT Python](https://eclipse.dev/paho/files/paho.mqtt.python/html/client.html).
