# Dashboard desktop

Objetivo: consultar avaliações de forma organizada, com contexto da sessão,
resultados por teste e qualidade dos registros. Implementado com PySide6 e
PyQtGraph. O coletor MQTT integrado grava no SQLite e recebe as confirmações da placa.

## Instalar e abrir

Na raiz do projeto:

```sh
source desktop/.venv/bin/activate
python -m pip install -e 'desktop[gui]'
python -m tremo_desktop --db desktop/tremo.sqlite3 gui
```

Se o ambiente já existia, feche a janela e repita a instalação acima antes de
reabri-la. O projeto fixa a versão do PySide6 usada nos testes dos gráficos:
com Python 3.11, PySide6 6.12.0 e PyQtGraph 0.14.0, zoom e arrasto repetidos
puderam encerrar o processo com `none_dealloc` durante a pintura dos eixos.
Essa atualização não altera o SQLite.

Na migração de um ambiente do dashboard que usava PySide6 6.12.0, remova
primeiro os pacotes separados daquela versão com
`python -m pip uninstall -y PySide6-WebEngine PySide6-Pdf`. Depois da
instalação de `desktop[gui]`, confira as dependências com `python -m pip check`.

Para explorar sem placa, broker ou dados reais:

```sh
python -m tremo_desktop gui --demo
```

A demonstração usa três sessões fictícias em um banco temporário, removido ao
fechar normalmente. Um aviso permanece visível; ela não publica MQTT nem altera o
banco real. Há exemplos de conclusão, interrupção com perdas e sessão em andamento.

Para usar a placa, configure `TREMO_MQTT_HOST` e `TREMO_MQTT_PORT` e abra
a GUI com o banco de trabalho. A GUI abre a conexão MQTT e atualiza as consultas
a cada três segundos. O comando `collect` continua disponível para coleta sem janela.

## Uso

- **Visão Geral** mostra a sessão mais recente, o paciente registrado no início e os resultados mais recentes de cada módulo. A aceleração RMS usa o MPU 1 nos últimos 60 segundos de dados, após remover a média de cada eixo.
- **Configurações** permite cadastrar um identificador de até 16 caracteres (letras sem acento, dígitos, hífen ou sublinhado), selecionar a placa e associar, substituir ou remover o paciente. A confirmação da placa atualiza o banco. O firmware mantém a associação em RAM e anuncia seu estado ao reconectar. Após reiniciar a placa, associe o paciente novamente.
- Na placa, abra **Sessões** e pressione A para iniciar. O clique no joystick volta ao menu. B solicita o encerramento e A confirma. Os testes individuais e a avaliação completa reutilizam a sessão aberta.
- Em **Histórico**, Selecione uma sessão pela lista; filtre pelo estado ou pesquise o ID da sessão
  ou da placa. A busca consulta o banco inteiro, mostrando até 200 resultados
  mais recentes. IDs completos estão nos detalhes e podem ser copiados.
- Consulte estado, duração ativa, tentativas recebidas e integridade. Estado
  concluído não significa que todos os registros chegaram ao computador.
- Em **Resultados cognitivos**, veja acurácia de MOT/PRM/PAL, tempo médio das
  respostas válidas no RTI, erros/antecipações/omissões e níveis/rodadas do PAL.
  Treino é separado. Métricas inconsistentes ficam ocultas, com explicação.
- Em **Tremores**, compare MPU 1 e MPU 2. Escolha aceleração (g) ou velocidade
  angular (graus/s), amplie com a roda e restaure a escala em **Ajustar zoom**.
  Os eixos X são vinculados. Falhas, saltos de sequência e pausas deixam quebras
  nas curvas; não interpolamos dados ausentes. A aceleração inclui gravidade.
- Em **Detalhes da sessão**, veja configuração, horários de recebimento, todas as
  ocorrências de qualidade e tentativas filtradas por teste/treino/avaliação.
- **Abrir outro banco** consulta outro arquivo SQLite compatível, sem migração ou
  alteração. Banco ausente/vazio e erros de leitura têm telas próprias. A leitura
  dos gráficos é limitada às primeiras 10.000 amostras de cada sensor, com aviso
  quando o limite for atingido; isso cobre as sessões de 30/60 s atuais.

O histórico preserva o paciente registrado no início da sessão mesmo após trocar a
associação da placa. Sessões antigas sem identificador permanecem sem paciente.
Os detalhes discriminam as execuções repetidas e suas tentativas.

## Arquitetura e arquivos

`ui/window.py` coordena navegação. `ui/results.py` apresenta resultados e tentativas;
`ui/charts.py` apresenta sensores; `ui/components.py` e `ui/style.py` concentram a
apresentação. `services/dashboard.py` consulta o banco e prepara séries com lacunas.

O conceito central é separar o **loop de eventos da janela** do trabalho de leitura:
`ui/worker.py` roda em uma QThread e cria sua própria conexão SQLite somente de
leitura. Sinais devolvem os resultados à thread principal, responsável pelos widgets.
Cada atualização usa uma transação de leitura, evitando misturar instantes diferentes
enquanto o coletor grava. Pedidos antigos são descartados ao trocar sessão ou filtros.
Há no máximo uma consulta em execução; o fechamento aguarda sua conclusão sem
destruir uma thread ativa. Nenhuma consulta ou callback MQTT executa nos widgets.

A alteração em `storage/repository.py` acrescenta leitura, filtros e detalhes sem
mudar o esquema do banco. O comando `gui` é integrado ao CLI existente; dependências
gráficas são opcionais e não são exigidas pelos comandos de coleta e relatório.

## Validação e aceitação

```sh
QT_QPA_PLATFORM=offscreen python -m unittest discover -s desktop/tests -v
python -m compileall -q desktop/src
```

Critérios: abrir banco vazio/ausente sem criar dados; filtrar e trocar sessões;
excluir treino das métricas; exibir qualidade e resultados incompletos; separar as
unidades e sensores; preservar quebras dos gráficos; não bloquear navegação durante
consultas; permitir fechar durante uma consulta; recuperar após erro de leitura.

Os testes incluem concorrência com gravação SQLite e interação Qt sem monitor físico.
A conferência visual foi feita com a demonstração em janelas de 1320×900 e 1000×720.
A validação de usabilidade por profissionais e a bancada com dados reais continuam
pendentes. O firmware não é alterado nesta task.

Referências: [threads no Qt](https://doc.qt.io/qtforpython-6/PySide6/QtCore/QThread.html)
e [curvas PyQtGraph](https://pyqtgraph.readthedocs.io/en/latest/api_reference/graphicsItems/plotdataitem.html).
