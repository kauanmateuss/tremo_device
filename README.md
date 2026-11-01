# Tremo Device — base de periféricos

Projeto em C com Pico SDK para a BitDogLab. Uma FSM gerencia o menu e as sessões de monitoramento dos dois MPUs,
com saída no monitor USB e no OLED. Veja o [guia da task FSM](docs/fsm-monitoramento.md)
para arquitetura, transições, compilação e critérios de aceitação.
O menu cognitivo oferece o [MOT adaptado](docs/mot-adaptado.md) e os testes RTI, PRM e PAL adaptados,
com protocolos experimentais, treinamento e registros em RAM.

## Organização

- `main.c`: inicialização da aplicação e loop principal.
- `app.h` / `app.c`: FSM, telas e menu.
- `session.h` / `session.c`: ID, configuração, duração ativa e pausas.
- `assessment.h` / `assessment.c`: encaminhamento dos quatro testes e pausa segura.
- `evaluation.h` / `evaluation.c`: sequência e resumo da bateria/avaliação completa.
- `controls.h` / `controls.c`: dois eixos do joystick, botões A/B, debounce e eventos.
- `mot.h` / `mot.c`: treinamento e avaliação MOT adaptada, resultados e exportação USB.
- `rti.h` / `rti.c`: RTI por soltura do botão A, temporização e resultados.
- `prm.h` / `prm.c`: memorização e reconhecimento entre duas alternativas.
- `pal.h` / `pal.c`: associação de formas a posições, rodadas e níveis.
- `patterns.h` / `patterns.c`: banco local de 16 formas 3×3 e embaralhamento.
- `monitoring.h` / `monitoring.c`: preparação, aquisição, conversões e resultados.
- `matrix.h` / `matrix.c`: buffer e envio para os 25 LEDs no GPIO 7.
- `display.h` / `display.c`: interface simples de texto para o OLED 128×64.
- `ssd1306.h`, `ssd1306_i2c.h` / `ssd1306_i2c.c`: driver do OLED.
- `mpu6050_i2c.h` / `mpu6050_i2c.c`: acesso aos dois MPU6050.

Os módulos pressupõem chamadas sequenciais em um único núcleo.

## Ligações previstas pelo código

| Periférico | Barramento | SDA | SCL | Endereço |
|---|---|---|---|---|
| OLED | I2C1 | GPIO 14 | GPIO 15 | 0x3C |
| MPU 1 | I2C0 | GPIO 0 | GPIO 1 | 0x68 |
| MPU 2 | I2C0 | GPIO 0 | GPIO 1 | 0x69 |

Os dois MPUs compartilham SDA/SCL. Configure AD0 em nível baixo no primeiro e
alto no segundo para obter endereços diferentes. Confira a ligação física antes
de usar: a configuração de software não altera o endereço do sensor.

## Fluxo atual

Ao ligar: inicializar periféricos → verificar sensores → boas-vindas → menu.
O joystick seleciona, A confirma e B volta. As opções são tremores, cognitiva,
completa e configurações/informações. Cognitiva abre o submenu MOT/RTI/PRM/PAL/bateria. MOT, RTI, PRM e PAL estão implementados;
a bateria executa os testes selecionados e a avaliação completa inclui os tremores.

Para monitorar: instruções → A preparar → calibração imóvel → sensores OK → A iniciar → aquisição.
Uma sessão dura 30 ou 60 segundos, conforme a configuração. B solicita interrupção; A confirma e B continua.
A coleta continua durante a confirmação. Uma falha de sensor encerra a sessão.

- Leitura com intervalo alvo de 20 ou 40 ms; OLED atualizado a cada 200 ms.
- Serial USB: CSV de aceleração (g) e giroscópio (graus/s), com timestamp.
- OLED: eixos XYZ em mg e graus/s para cada sensor.
- Resultados: duração, contagens válidas/falhas e motivo de término, em RAM e serial.
- A nova sessão apaga o resumo anterior; desligar também. Não há armazenamento
  permanente nem cálculo de frequência/intensidade dos tremores nesta etapa.

A aplicação utiliza um único núcleo e agendamento cooperativo. I2C e OLED são
síncronos; a preparação dos dois MPUs leva aproximadamente 400 ms. Os intervalos
são metas, não garantias de tempo real. A UART permanece desabilitada porque os
GPIOs 0/1 estão reservados ao I2C0. A matriz é inicializada apagada.

## MOT adaptado

Cognitiva → MOT → instruções → três alvos de treino → A iniciar → dez alvos → resultados.
O joystick move o cursor azul em quatro direções até o alvo verde; A seleciona e
B encerra. Na sobreposição, o LED fica ciano. O prazo de cada alvo é de 10 s.
Treino exige três acertos e fica separado das métricas. Não há aquisição MPU durante
este fluxo. A média usa somente acertos avaliativos; acurácia inclui omissões como
não acertos. Os registros ficam em RAM até iniciar outro MOT ou desligar e são
exportados via USB ao finalizar/reconectar. Não são escores validados do CANTAB.
Consulte o guia do MOT para protocolo, limitações de temporização e testes na placa.

## RTI adaptado

Cognitiva → RTI → A treinar → três respostas válidas → A iniciar → dez tentativas.
Em cada tentativa, solte A e depois pressione e mantenha pressionado. Após uma
espera sorteada de 1–3 s, o LED central fica verde: solte A antes de 2 s.
Soltar antes do verde registra antecipação; atingir 2 s sem resposta válida registra
omissão. B encerra e mostra os resultados. O treino deve ser repetido se houver erro
e não entra na média. OLED apresenta contagens e média apenas das respostas válidas.

A FSM principal delega ao módulo `rti`; `controls` fornece a borda de soltura validada
por debounce de 30 ms, preservando seu timestamp inicial. Durante o trecho cronometrado
não há atualização do OLED, exportação serial nem aquisição MPU. A referência do
estímulo é registrada após enviar os LEDs; polling de 5 ms e latência física ainda
precisam de validação na placa. Este protocolo mede somente reação por soltura,
sem tempo de movimento ou escores equivalentes ao [RTI CANTAB](https://cambridgecognition.com/reaction-time-rti/).

Os registros `RTI,...` em CSV usam microssegundos, separam treino/teste e deixam
tempos inexistentes vazios. São enviados por USB ao finalizar/reconectar e ficam
em RAM até iniciar outro RTI ou desligar; a transmissão não tem confirmação de entrega.
Para aceitar a task na placa: gravar o UF2 via BOOTSEL, concluir treino e dez tentativas,
provocar antecipação e omissão, conferir contagens/CSV e testar B durante a espera
e após o verde. Os testes no PC cobrem esses fluxos e bordas temporais; não substituem
a medição física da temporização.

## PRM adaptado

Cognitiva → PRM → A treinar → memorizar duas formas → reconhecer as duas → A iniciar.
Na avaliação são seis formas: cada uma aparece por 2 s, com intervalo apagado de
0,5 s. Após mais 1 s, os pares são perguntados na ordem inversa. Esquerda mostra
opção 1, direita opção 2; veja ambas e confirme com A. O limite de 15 s começa
na primeira alternativa e não reinicia ao alternar. B encerra em qualquer etapa.
O treino exige dois acertos; se necessário, A repete. Treino e avaliação usam
formas distintas, e os distratores não pertencem à sequência memorizada.

Resultados: acertos, erros, omissões, acurácia e média de tempo dos acertos;
treino separado. CSV `PRM,...` contém IDs de padrões do banco v1, alternativa correta,
escolha (0/1; vazia sem resposta), desfecho e tempos em microssegundos. RAM e
exportação ao concluir/reconectar USB seguem as mesmas limitações do RTI.
A latência inclui navegação, atualização do OLED e debounce: não é reação pura.
A apresentação alternada e as formas simples são adaptações locais do conceito
[PRM](https://cambridgecognition.com/pattern-recognition-memory-prm/), sem escores
clínicos ou reconhecimento tardio. Não há coleta MPU durante o teste.

Aceitação na placa: concluir treino e seis escolhas, provocar erro/omissão,
interromper durante memorização e escolha e conferir OLED/CSV. Os testes no PC
verificam esses fluxos, padrões distintos, balanceamento e limite de tempo.

## PAL adaptado

Cognitiva → PAL → A treinar → dois pares corretos → A iniciar → níveis de 2, 3 e 4 pares.
Cada associação mostra uma forma verde central e seu canto azul durante 2 s,
com intervalo apagado de 0,5 s. Após mais 1 s, as formas são consultadas em ordem
sorteada: mova o cursor branco entre os quatro cantos pelo joystick e confirme A.
A forma continua visível e nenhuma cor indica o canto correto. Cada escolha tem
15 s; movimentos não reiniciam o prazo. B encerra em qualquer etapa.

O treino exige acertar os dois pares; A repete se necessário. Na avaliação, acertar
todos os pares de uma rodada libera o próximo nível, iniciado por A. Havendo erro
ou omissão, A reapresenta as mesmas associações. Após três rodadas malsucedidas no
nível, o teste encerra com `LIMITE ATINGIDO`. Níveis novos usam outras formas e
posições sorteadas. As quatro posições são opções mesmo nos níveis de 2 e 3 pares.

OLED mostra níveis concluídos, acertos, erros, omissões e rodadas iniciadas. CSV
`PAL,...` registra fase, número de pares, rodada, tentativa, ID da forma, posição
correta/escolhida, desfecho e tempos em microssegundos. Posições: 0 superior esquerda,
1 superior direita, 2 inferior esquerda, 3 inferior direita; escolha vazia significa
sem resposta. Treino não entra nas métricas. Até 29 registros ficam em RAM e são
exportados após encerrar/reconectar USB, sem armazenamento permanente ou confirmação
de entrega. Repetir treino substitui seus registros anteriores; iniciar outro PAL
limpa a sessão. Interrupção durante uma escolha registra `aborted`, sem contar erro.

A latência inclui decisão, movimento e debounce; precisa ser medida fisicamente.
O banco e a apresentação são adaptações experimentais do conceito
[PAL](https://cambridgecognition.com/paired-associates-learning-pal/), sem equivalência
clínica ao CANTAB. A coleta MPU é sequencial e a pausa aguarda a fronteira da rodada.

Aceitação na placa: concluir os três níveis, provocar erro e conferir reapresentação
das mesmas associações, atingir o limite de três rodadas, testar omissão e B durante
memorização/escolha. Conferir contagens e posições no CSV. Os testes no PC incluem
o caminho máximo de 29 registros e verificam que treino não entra nas métricas.

## Sessões e configuração

Cada execução recebe um ID de 64 bits (nonce do boot + contador). Esse ID acompanha
os resultados e o cabeçalho serial `SESSION`; não contém dados do paciente.
Configurações permite escolher 30/60 s e 25/50 Hz para tremores; A altera e B volta.
A configuração fica em RAM e é copiada ao iniciar a sessão.
O clique do joystick (GPIO 22) solicita pausa. A retoma e B encerra. Em MOT/RTI,
a pausa aguarda o intervalo entre tentativas. Em PRM, aguarda o término do bloco;
em PAL, o término da rodada. Assim, exposição, retenção e resposta não são congeladas.
O monitoramento pausa imediatamente e exclui esse período da duração ativa.

## Aquisição e calibração MPU

O preparo verifica WHO_AM_I, configura e relê os registradores: ±2 g, ±250 graus/s,
25 Hz com DLPF de 10 Hz ou 50 Hz com DLPF de 20 Hz. Referência: mapa de registradores
[InvenSense](https://forum.arduino.cc/uploads/short-url/irIuOhPAskO6xY40ifvcP0HyV3H.pdf).
Se as boas-vindas mostram `MPU1 FALHA` ou `MPU2 FALHA`, a calibração ainda
não começou. Abra o monitor serial USB e procure as linhas `# MPU 0x68:`
ou `# MPU 0x69:`. O firmware conserva o erro do último preparo por endereço:
falha de comunicação no reset, escrita/leitura de registrador, valor inesperado
(com valor lido, esperado e máscara) ou falha na primeira amostra. Em
**Monitoramento de tremores**, use **A TENTAR** na tela de falha para repetir.

A calibração usa 50 leituras por sensor após estabilização de 300 ms; exige apoio
imóvel, verifica amplitude/variação e calcula apenas o bias do giroscópio. Não é
calibração de escala em seis posições. A aceleração conserva a gravidade. B cancela;
movimento permite repetir. Os limiares são experimentais e exigem verificação física.

A agenda de amostragem mantém prazos fixos e conta os intervalos perdidos, sem
inventar leituras atrasadas. Timestamp indica o início da leitura I2C, não o instante
da conversão interna. Os dois MPUs são lidos sequencialmente. O resumo inclui
`missed_slots`, `buffer_drops`, `max_lateness_ms`, duração I2C e separação entre sensores.
A fila de 256 leituras em RAM é consumida pela USB (até duas por ciclo). Sem consumidor,
a fila cheia descarta novas leituras e incrementa o contador. A próxima sessão limpa
a fila; não há garantia de retenção de uma sessão inteira. MQTT utilizará essa API
em uma etapa posterior. A atualização síncrona do OLED pode causar atrasos mensuráveis.

## Bateria e avaliação completa

Em Configurações, selecione MOT/RTI/PRM/PAL (ao menos um) e MPU antes/depois.
Cima/baixo escolhe a primeira linha da janela; A altera e B volta. A ordem cognitiva
é MOT → RTI → PRM → PAL, omitindo os desmarcados. A bateria usa apenas os testes;
a avaliação completa acrescenta tremores. Cada etapa mantém instruções e treino.
Nos resultados da etapa, A avança e B encerra toda a sequência. Após a última etapa,
A abre o resumo final. Limite de tentativas do PAL fica registrado como LIMITE;
não é uma falha do hardware. Falha de sensor encerra a sequência com erro.

Uma sessão compartilha ID e configuração entre todas as etapas. O resumo registra
concluídas, interrompidas e pendentes. Os registros individuais permanecem nos seus
módulos até uma nova execução do respectivo teste/reset; o resumo é substituído na
próxima bateria. Não há execução simultânea dos MPUs e testes cognitivos.
A duração configurada dos tremores começa no início da aquisição, mesmo quando
outros testes já consumiram tempo na mesma sessão.

## Contratos das funções

- `npInit` retorna `bool`; repetir com o mesmo pino não reserva outra máquina PIO.
- `npSetPixel` usa coordenadas 0–4 a partir do canto superior esquerdo.
- `npSetLED` aceita índices de 0 a 24; índices inválidos são ignorados.
- `npClear` limpa o buffer. `npWrite` envia o buffer para a matriz.
- `display_draw_text` altera o buffer; `display_render` envia ao OLED.
- `clear_display` e `display_show_lines` já atualizam a tela.
- O texto usa caracteres de 8×8; `y` é alinhado para baixo a múltiplos de 8.
  São exibidas até oito linhas. Textos longos são cortados na borda direita.
  A fonte atual contém letras, números, sinal negativo e ponto decimal, sem acentos.
- `mpu6050_reset`, `mpu6050_set_accel_range` e `mpu6050_read_raw` retornam
  `false` em falha. As transferências I2C dos MPUs têm timeout de 10 ms por chamada.
- `mpu6050_get_accel_range` retorna 0–3, ou `MPU6050_RANGE_ERROR` em falha.
- A API opcional de bitmap retorna `bool` em `ssd1306_init_bm`; confira esse retorno.
  Inicialize cada estrutura uma vez e libere `ram_buffer` com `free` ao descartá-la.
  O bitmap deve conter pelo menos `width * height / 8` bytes.

O driver OLED ainda não propaga erros I2C para a aplicação. Sua inicialização
não confirma a presença física do display.

## Compilação e validação na placa

```sh
cmake -S . -B build
cmake --build build -j 4
python3 tests/run_host_tests.py
```

Requer Pico SDK, CMake e compilador ARM configurados. O firmware é gerado em
`build/tremo_device.uf2`. A compilação não valida as ligações físicas.

Próximas tarefas simples:

1. Acender separadamente vermelho, verde e azul na matriz e conferir as cores.
2. Exibir oito linhas no OLED e conferir o corte de textos longos.
3. Variar aceleração e rotação de cada MPU no Wokwi e conferir serial e OLED.
4. Testar as transições e falhas usando os critérios do guia FSM.
5. Medir os intervalos pelos timestamps e acrescentar calibração e filtragem
   antes da análise de movimento.

Referências técnicas: [Pico SDK](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html)
e [registradores MPU6050](https://invensense.tdk.com/wp-content/uploads/2015/02/MPU-6000-Register-Map.pdf).

## Pendências e próxima etapa

Tasks 05, 10 e 12 têm implementação de firmware. Task 13 possui testes automatizados,
[protocolo de bancada](docs/validacao-tecnica.md) e verificador de capturas; medições
físicas, usabilidade e repetibilidade ainda pendentes. Task 11 agora inclui
[publicação MQTT no firmware](docs/mqtt.md) e
[coletor Python com SQLite](desktop/README.md). O
[dashboard desktop PySide6/PyQtGraph](desktop/DASHBOARD.md) apresenta sessões, métricas
cognitivas, gráficos dos dois MPUs e qualidade da coleta. Usar branches `feature/...` para novas tasks.
