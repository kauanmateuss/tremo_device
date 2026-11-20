# Resultados e coleta no desktop

## Paciente e histórico

A associação de paciente é confirmada pela placa e mantida em RAM. Após reiniciar o dispositivo, associe o paciente novamente. O histórico conserva o identificador registrado na abertura de cada sessão. Sessões antigas sem identificação continuam disponíveis.

Em **Histórico**, filtre por paciente, dispositivo, data ou estado. Os detalhes discriminam as execuções de cada teste e suas tentativas. **Comparar** apresenta duas sessões lado a lado e, para módulos repetidos, usa a última execução com resumo.

## Leitura dos resultados

- **Cognição.** MOT, PRM e PAL apresentam acurácia e tempo médio dos acertos. RTI mostra o tempo médio das respostas válidas, antecipações e omissões. PAL também informa níveis e rodadas. O treino fica separado. Quando o resumo diverge das tentativas, o dashboard indica a inconsistência e oculta a métrica.
- **Movimento.** Na visão geral, a aceleração RMS usa até 60 segundos de amostras recentes de cada sensor, descontando a média por eixo. Nos detalhes, escolha aceleração (g) ou velocidade angular (°/s). A aceleração inclui gravidade. Falhas e lacunas interrompem o traçado. O gráfico detalhado apresenta até 10.000 amostras por sensor.
- **Qualidade.** Confira cobertura, calibração, intervalos perdidos, descartes e falhas de leitura. Uma sessão pode ter sido encerrada na placa mesmo que parte dos registros tenha se perdido no transporte.

## Coleta por terminal

Com o [ambiente do desktop](../desktop/README.md) ativo, substitua o endereço de exemplo pelo do broker. Use este modo para coletar sem abrir a janela.

```sh
python -m tremo_desktop --db desktop/tremo.sqlite3 collect --host broker.exemplo --port 1883
```

Aguarde **Coletor pronto** antes de iniciar a avaliação. Se o broker exigir autenticação, defina `TREMO_MQTT_USER` e `TREMO_MQTT_PASSWORD` no ambiente. Use Ctrl+C para encerrar. Para listar sessões e consultar uma delas pelos identificadores retornados:

```sh
python -m tremo_desktop --db desktop/tremo.sqlite3 sessions
python -m tremo_desktop --db desktop/tremo.sqlite3 report ID_DA_PLACA ID_DA_SESSAO
```

O coletor valida a mensagem e grava o evento e seus registros derivados em uma transação SQLite antes de confirmar a entrega. Placa, sessão e sequência identificam reenvios. Um erro de armazenamento interrompe a coleta. Para retomar a assinatura persistente, reutilize o banco e o identificador de cliente. Consulte os [limites de entrega MQTT](mqtt.md).

## Validação

Na raiz do repositório, com o ambiente virtual ativo:

```sh
QT_QPA_PLATFORM=offscreen python -m unittest discover -s desktop/tests -v
```

O teste opcional com broker requer Mosquitto e `TREMO_TEST_MQTT=1`. Em sistemas com perfil AppArmor do Mosquitto, prepare um broker exclusivo para teste em localhost e informe `TREMO_TEST_PORT`. O teste publica dados simulados. Consulte o [registro de validação do desktop](validacao-desktop.md).
