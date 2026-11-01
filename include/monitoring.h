#ifndef MONITORING_H
#define MONITORING_H

// Inicializa o barramento e os dois MPUs. Chame após inicializar serial e display.
void monitoring_init(void);

// Executa um ciclo de leitura e saída, aguardando o intervalo alvo de 40 ms.
// Chame continuamente no loop principal, após monitoring_init().
void monitoring_update(void);

#endif
