# Task 13 — validação técnica do protótipo

## Estado e execução

A lógica foi verificada com periféricos simulados no PC e compilação para Pico W.
**A bancada física, usabilidade com participantes e repetibilidade permanecem pendentes.**
Não há validação clínica nem equivalência de escores ao CANTAB. Anotar versão do
firmware/commit, identificação da placa e sensores, taxas, posição de montagem e
condições de cada ensaio. Repetir após mudanças no transporte, temporização ou hardware.

```sh
cmake --build build -j 4
python3 tests/run_host_tests.py
python3 tests/test_capture_validator.py
```

Gravar `build/tremo_device.uf2` via BOOTSEL. Abrir a USB serial antes da sessão e
salvar toda a saída em arquivo UTF-8. A UART está desabilitada. Para a verificação
quantitativa de uma captura MPU sem pausa:

```sh
python3 tools/validate_capture.py captura.csv --period-ms 40 --max-jitter-us 10000
```

Saída JSON e código 0 indicam aprovação **dos registros observados**, não certificam
uma sessão completa. O programa detecta campos truncados, amostras inválidas,
lacunas/duplicatas, ordem temporal, ausência de um sensor e desvio entre leituras
consecutivas. Conferir também `RESULTADO`/`AQUISICAO`: a captura deve ir do início
ao término e ter as contagens esperadas. Uma cauda perdida não é detectável apenas
pelas linhas recebidas. O limiar de 10 ms é uma meta inicial de engenharia, revisável
antes de testes com participantes; não é parâmetro clínico.

## Matriz de aceitação em bancada

| Ensaio | Procedimento | Aceitação inicial | Estado |
|---|---|---|---|
| Identidade e falha | Desligar, retirar um MPU, ligar; depois simular falha durante coleta | Não iniciar sem dois sensores; encerrar em erro sem reutilizar leitura antiga | Pendente |
| Calibração | Apoiar ambos, executar três vezes; repetir movimentando um sensor | Aprovar apoio estável e rejeitar movimento; gyro parado próximo de zero (meta média por eixo <1 dps); gravidade preservada | Pendente |
| Amostragem | 3 sessões de 30 s em 25 Hz e 3 em 50 Hz, USB aberta, sem pausa | 750/1500 leituras por sensor; erros, intervalos perdidos e descartes zero; jitter dentro de 10 ms | Pendente |
| Saturação da fila | Coletar sem leitor USB por mais que a capacidade | Contador de descartes cresce; interface continua respondendo; nenhuma amostra falsa | Pendente |
| Pausa | Clicar joystick durante tremores e durante estímulo/rodada de cada teste | Sem leituras durante pausa; duração ativa exclui pausa; testes aguardam fronteira segura; A retoma, B encerra | Pendente |
| RTI | Comparar borda elétrica do botão e emissão óptica do LED com timestamps em ≥30 respostas | Registrar erro médio/máximo; meta inicial erro absoluto ≤10 ms; validar antecipação e omissão | Pendente |
| Interface | Percorrer todos os testes e configurações na placa | Texto legível, formas distinguíveis, cursor e posição sem ambiguidade, nenhum corte | Pendente |
| Bateria | Selecionar subconjunto, executar com MPU antes e depois | Mesmo ID em todas as etapas; só uma ativa; tempo MPU independente da duração cognitiva | Pendente |
| Encerramento | B em treino, resposta, pausa e entre etapas; falha MPU | Preservar registros concluídos, marcar interrupção/erro e etapas pendentes | Pendente |
| Repetibilidade | Repetir entradas controladas e mesmo protocolo | Contagens e transições consistentes; documentar variação temporal | Pendente |

Se a meta de 50 Hz falhar com o OLED ativo, registrar a falha e ajustar a arquitetura
antes de declarar essa taxa validada (por exemplo, reduzir tempo de desenho ou adotar
FIFO/DRDY). A fila de software não recupera conversões não lidas do MPU. Os sensores
não estão sincronizados fisicamente; `skew_us` mede apenas separação das leituras.
MOT/PRM/PAL medem decisão+navegação e debounce; não comparar suas latências diretamente
com o tempo de soltura do RTI. Pausas de PRM só ocorrem fora do bloco de memória.

## Registro por ensaio

Copiar: data/hora, operador, commit, hardware, configuração, sessão hexadecimal,
procedimento, valores observados, aprovado/reprovado, evidências (captura/traço/foto)
e ação corretiva. Nenhum resultado físico foi preenchido automaticamente.

## Próxima etapa: dados

A task 11 será realizada com MQTT/Mosquitto, seguida de recepção e SQLite na pasta
`desktop/`. Esta etapa forneceu IDs, registros, fila e indicadores; ainda não há
conexão Wi-Fi/MQTT, persistência, confirmação de entrega ou dashboard. A validação
de perdas/reconexão deverá ser repetida após implementar esse transporte.
