# Sensores e avaliações

## Sensores e calibração

Os dois MPUs compartilham o I²C0 (SDA no GPIO 0, SCL no GPIO 1). Ligue AD0 em nível baixo no primeiro sensor e alto no segundo para obter os endereços `0x68` e `0x69`. O OLED usa I²C1 (GPIOs 14 e 15, endereço `0x3C`).

O firmware identifica o MPU-6050 ou o MPU-6500, configura as faixas de ±2 g e ±250 °/s e coleta a 25 ou 50 Hz. O filtro do sensor fica próximo de 10 ou 20 Hz, respectivamente. A calibração requer os sensores imóveis e usa 50 leituras para estimar o desvio do giroscópio. A aceleração inclui a gravidade. Em caso de falha, o monitor serial USB informa o endereço e a etapa da verificação.

## Protocolos cognitivos

| Teste | Tarefa | Medidas |
|---|---|---|
| [MOT](mot-adaptado.md) | Mover o cursor até o alvo e confirmar com A. São três alvos de treino e dez de avaliação. | Acertos, erros, omissões e tempo dos acertos. |
| RTI | Manter A pressionado e soltá-lo quando o LED ficar verde. Após três respostas válidas de treino, são dez tentativas. | Tempo das respostas válidas, antecipações e omissões. O limite após o estímulo é de 2 s. |
| PRM | Memorizar seis formas e reconhecer cada uma entre duas alternativas após o treino. | Acertos, erros, omissões e tempo dos acertos. O prazo de 15 s inclui a navegação entre alternativas. |
| PAL | Aprender associações entre formas e cantos da matriz em níveis de dois, três e quatro pares. Três rodadas malsucedidas encerram o nível e a avaliação. | Níveis concluídos, rodadas, acertos, erros e omissões. |

O treino fica separado das métricas. As tarefas são adaptações experimentais. Os resultados descrevem o desempenho no dispositivo e a validação clínica permanece pendente.

## Sessão e sequência

Ao abrir uma sessão, a placa registra seu identificador, o paciente associado e a configuração vigente. Cada execução recebe um número dentro da sessão. A bateria percorre os módulos habilitados na ordem MOT, RTI, PRM e PAL. A avaliação completa acrescenta a coleta de tremores antes ou depois da bateria, conforme a configuração. A coleta com os MPUs durante os testes cognitivos também é configurável.

O clique no joystick solicita pausa durante a coleta. Nos testes cognitivos, a pausa ocorre em uma fronteira da tarefa. Após uma avaliação, a sessão permanece aberta para outra execução. Para encerrá-la, abra **Sessões**, pressione B e confirme com A.

Consulte o [roteiro de validação](validacao-tecnica.md) para conferir tentativas, temporização e aquisição na placa.
