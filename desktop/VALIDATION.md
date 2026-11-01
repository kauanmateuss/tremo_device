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
