# Validação desta etapa — 31/10/2026

- Firmware compilado com Pico SDK 2.3.1 para Pico W, inclusive com SSID fictício
  para validar a vinculação de todo o código Wi-Fi/lwIP/MQTT.
- 14 suítes C de host aprovadas; verificador de capturas aprovado.
- 9 testes Python de contrato, armazenamento e métricas aprovados, incluindo
  mensagens produzidas pela FSM C e pelos módulos MOT, RTI, PRM, PAL e MPU.
- Integração com Paho 2.1.0 e Mosquitto 2.0.22 aprovada: 1500 amostras MPU,
  resumo da sessão, eliminação de reenvios, recuperação após reiniciar o subscriber
  e registro de mensagem inválida. Broker de teste separado, em localhost.
- Conexão MQTT sem credenciais aceita por `192.168.1.215:1883` (CONNACK 0).
  Essa verificação não publicou dados no broker do notebook.
- O coletor confirma mensagens após commit no SQLite; falha de armazenamento
  simulada não produz ACK.

Não foi realizado ensaio na BitDogLab. SSID/senha ainda precisam ser preenchidos
no arquivo local de configuração e o firmware recompilado. Precisão temporal,
impacto do Wi-Fi e perdas em aquisição real continuam sujeitos à bancada descrita
em `docs/validacao-tecnica.md`. Os testes não validam uso clínico dos protocolos.

## Dashboard desktop — 31/10/2026

- PySide6 6.12.0, PyQtGraph 0.14.0; dependências verificadas com `pip check`.
- Compilação dos módulos Python aprovada (`compileall`).
- 17 testes Python aprovados, incluindo cinco testes de consulta/gráficos e três
  testes Qt de navegação, filtros, dados inválidos, troca de sessão e fechamento
  durante consulta. O teste MQTT opt-in foi ignorado nesta rodada; o transporte
  não foi alterado.
- SQLite aberto somente para leitura na GUI; teste de gravação concorrente
  confirmou que a consulta mantém um retrato consistente.
- Janela renderizada e conferida com dados simulados, em 1320×900 e 1000×720,
  usando Qt offscreen. Ainda falta avaliação de usabilidade com profissionais
  e teste da experiência completa com dados adquiridos pela BitDogLab.
