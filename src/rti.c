#include "rti.h"
#include "session.h"
#include "telemetry.h"
#include "display.h"
#include "matrix.h"
#include "pico/stdlib.h"
#include "pico/rand.h"
#include <stdio.h>

static rti_state_t state;
static rti_result_t result;
static rti_record_t current;
static bool training, saved, usb_connected;
static uint8_t trial, training_responses;
static uint64_t wait_started_us, interval_started_us;

static void clear_matrix(void) {
    npClear();
    npWrite();
}

static void export_result(void) {
    printf("# SESSION id=%016llx module=RTI\n", (unsigned long long)result.session_id);
    printf("# RTI_ADAPTADO_V1 interrupted=%u responses=%u anticipations=%u omissions=%u\n",
           result.interrupted, result.responses, result.anticipations, result.omissions);
    printf("rti,phase,trial,outcome,planned_wait_us,hold_us,onset_us,release_us,end_us,reaction_us\n");
    const char *names[] = {"response", "anticipation", "omission", "aborted"};
    for (unsigned int i = 0; i < result.record_count; ++i) {
        const rti_record_t *r = &result.records[i];
        char onset[24] = "", release[24] = "", reaction[24] = "";
        if (r->stimulus_shown) snprintf(onset, sizeof(onset), "%llu", (unsigned long long)r->onset_us);
        if (r->released) snprintf(release, sizeof(release), "%llu", (unsigned long long)r->release_us);
        if (r->outcome == RTI_RESPONSE)
            snprintf(reaction, sizeof(reaction), "%llu", (unsigned long long)(r->release_us - r->onset_us));
        printf("RTI,%s,%u,%s,%lu,%llu,%s,%s,%llu,%s\n", r->training ? "training" : "test",
            r->training ? i + 1 : i + 1 - RTI_TRAINING_TRIALS, names[r->outcome],
            (unsigned long)r->planned_wait_us, (unsigned long long)r->hold_us, onset,
            release, (unsigned long long)r->ended_us, reaction);
    }
    printf("# RTI_END records=%u\n", result.record_count);
}

static void show_results(void) {
    char responses[24], early[24], omitted[24], mean[32];
    snprintf(responses, sizeof(responses), "RESPOSTAS %u", result.responses);
    snprintf(early, sizeof(early), "ANTECIPACOES %u", result.anticipations);
    snprintf(omitted, sizeof(omitted), "OMISSOES %u", result.omissions);
    if (result.responses)
        snprintf(mean, sizeof(mean), "MEDIA %lu MS", (unsigned long)(result.response_time_us / result.responses / 1000));
    else snprintf(mean, sizeof(mean), "SEM RESPOSTAS");
    const char *lines[] = {"RTI ADAPTADO", result.interrupted ? "INTERROMPIDO" : "CONCLUIDO",
        responses, early, omitted, mean, "SALVO EM RAM", "A OU B MENU"};
    display_show_lines(lines, 8);
}

static void publish_result(void) {
    if (!telemetry_enabled()) return;
    for (unsigned i = 0; i < result.record_count; ++i) {
        const rti_record_t *r = &result.records[i];
        char onset[24] = "null", release[24] = "null", latency[24] = "null";
        if (r->stimulus_shown) snprintf(onset, sizeof(onset), "%llu", (unsigned long long)r->onset_us);
        if (r->released) snprintf(release, sizeof(release), "%llu", (unsigned long long)r->release_us);
        if (r->outcome == RTI_RESPONSE) snprintf(latency, sizeof(latency), "%llu", (unsigned long long)(r->release_us - r->onset_us));
        telemetry_emit(result.session_id, r->ended_us, "trial", true,
            "{\"module\":\"rti\",\"index\":%u,\"training\":%s,\"outcome\":%u,\"hold_us\":%llu,\"onset_us\":%s,\"release_us\":%s,\"ended_us\":%llu,\"reaction_us\":%s,\"planned_wait_us\":%lu}",
            i, r->training ? "true" : "false", r->outcome, (unsigned long long)r->hold_us, onset, release,
            (unsigned long long)r->ended_us, latency, (unsigned long)r->planned_wait_us);
    }
    telemetry_emit(result.session_id, time_us_64(), "result", true,
        "{\"module\":\"rti\",\"interrupted\":%s,\"correct\":%u,\"wrong\":%u,\"omissions\":%u,\"valid_time_us\":%llu,\"record_count\":%u,\"levels_completed\":%u,\"rounds_started\":%u,\"limit_reached\":%s}",
        result.interrupted ? "true" : "false", result.responses, result.anticipations, result.omissions,
        (unsigned long long)result.response_time_us, result.record_count, 0u, 0u, "false");
}

static void finish(bool interrupted) {
    result.interrupted = interrupted;
    saved = true;
    state = RTI_RESULTS;
    clear_matrix();
    show_results();
    publish_result();
    usb_connected = stdio_usb_connected();
    if (usb_connected) export_result();
}

static void prepare_trial(void) {
    char title[16];
    snprintf(title, sizeof(title), "%s %u DE %u", training ? "TREINO" : "RTI", trial + 1,
             training ? RTI_TRAINING_TRIALS : RTI_TEST_TRIALS);
    const char *lines[] = {title, "SOLTE A", "DEPOIS SEGURE A", "AGUARDE VERDE", "SOLTE NO VERDE", "", "B ENCERRA"};
    clear_matrix();
    display_show_lines(lines, 7);
    state = RTI_WAIT_RELEASE; // Exige soltar o botão usado para iniciar a etapa.
}

static void start_wait(control_button_state_t button) {
    current = (rti_record_t){.training = training, .hold_us = button.edge_us};
    // Sorteio em passos de 1 ms. Feito antes do trecho cronometrado.
    const uint32_t wait_steps = (RTI_MAX_WAIT_US - RTI_MIN_WAIT_US) / 1000u + 1u;
    current.planned_wait_us = RTI_MIN_WAIT_US + (get_rand_32() % wait_steps) * 1000u;
    const char *lines[] = {training ? "TREINAMENTO" : "RTI", "SEGURE A", "AGUARDE VERDE", "SOLTE NO VERDE", "", "B ENCERRA"};
    display_show_lines(lines, 6);
    wait_started_us = time_us_64();
    state = RTI_FOREPERIOD;
}

static void present_stimulus(void) {
    npClear();
    npSetPixel(2, 2, 0, 20, 0);
    npWrite();
    current.onset_us = time_us_64(); // Referência após envio/latch; não medição óptica.
    current.stimulus_shown = true;
    state = RTI_STIMULUS;
}

static void record_trial(rti_outcome_t outcome, uint64_t now_us) {
    current.outcome = outcome;
    current.ended_us = now_us;
    result.records[result.record_count++] = current;
    if (training) {
        if (outcome == RTI_RESPONSE) ++training_responses;
    } else {
        if (outcome == RTI_RESPONSE) {
            ++result.responses;
            result.response_time_us += current.release_us - current.onset_us;
        } else if (outcome == RTI_ANTICIPATION) ++result.anticipations;
        else if (outcome == RTI_OMISSION) ++result.omissions;
    }
}

static void end_trial(rti_outcome_t outcome, uint64_t now_us) {
    record_trial(outcome, now_us);
    clear_matrix();
    const char *feedback = outcome == RTI_RESPONSE ? "RESPOSTA OK" :
                          outcome == RTI_ANTICIPATION ? "SOLTOU ANTES" : "SEM RESPOSTA";
    const char *lines[] = {training ? "TREINAMENTO" : "RTI", training ? feedback : "AGUARDE", "", "SOLTE A"};
    display_show_lines(lines, 4);
    interval_started_us = time_us_64();
    state = RTI_INTERVAL;
}

void rti_begin(void) {
    result = (rti_result_t){.session_id = session_current()->id};
    saved = false;
    training = true;
    trial = training_responses = 0;
    clear_matrix();
    const char *lines[] = {"RTI ADAPTADO", "SEGURE A", "ESPERE O VERDE", "SOLTE A RAPIDO", "", "A TREINAR", "B ENCERRAR", "3 TENTATIVAS"};
    display_show_lines(lines, 8);
    state = RTI_INSTRUCTIONS;
}

bool rti_update(control_event_t event, control_button_state_t button, uint64_t now_us) {
    if (state == RTI_RESULTS) return event == CONTROL_CONFIRM || event == CONTROL_BACK;
    if (event == CONTROL_BACK) {
        if (state == RTI_FOREPERIOD || state == RTI_STIMULUS) record_trial(RTI_ABORTED, now_us);
        finish(true);
        return false;
    }
    switch (state) {
    case RTI_INSTRUCTIONS:
        if (event == CONTROL_CONFIRM) prepare_trial();
        break;
    case RTI_WAIT_RELEASE:
        if (!button.pressed && button.settled) state = RTI_WAIT_HOLD;
        break;
    case RTI_WAIT_HOLD:
        if (event == CONTROL_CONFIRM && button.pressed && button.settled) start_wait(button);
        break;
    case RTI_FOREPERIOD:
        // Só avança o prazo com CONTROL_NONE, que traz o estado atual do botão.
        // Eventos enfileirados e solturas são tratados primeiro.
        if (event == CONTROL_CONFIRM_RELEASE) {
            current.released = true;
            current.release_us = button.edge_us;
            end_trial(RTI_ANTICIPATION, now_us);
        } else if (event == CONTROL_NONE && button.pressed && button.settled && now_us - wait_started_us >= current.planned_wait_us) {
            present_stimulus();
        }
        break;
    case RTI_STIMULUS:
        if (event == CONTROL_CONFIRM_RELEASE) {
            current.released = true;
            current.release_us = button.edge_us;
            if (button.edge_us < current.onset_us) end_trial(RTI_ANTICIPATION, now_us);
            else if (button.edge_us - current.onset_us < RTI_RESPONSE_LIMIT_US) end_trial(RTI_RESPONSE, now_us);
            else end_trial(RTI_OMISSION, now_us);
        } else if (event == CONTROL_NONE && now_us - current.onset_us >= RTI_RESPONSE_LIMIT_US &&
                   (button.settled || now_us - current.onset_us >= RTI_RESPONSE_LIMIT_US + RTI_DEBOUNCE_GRACE_US)) {
            end_trial(RTI_OMISSION, now_us);
        }
        break;
    case RTI_INTERVAL:
        if (now_us - interval_started_us < RTI_INTERVAL_US) break;
        ++trial;
        if (trial < (training ? RTI_TRAINING_TRIALS : RTI_TEST_TRIALS)) prepare_trial();
        else if (!training) finish(false);
        else if (training_responses == RTI_TRAINING_TRIALS) {
            state = RTI_READY;
            const char *lines[] = {"TREINO OK", "TESTE 10 ALVOS", "", "A INICIAR", "B ENCERRAR"};
            display_show_lines(lines, 5);
        } else {
            state = RTI_TRAINING_FAILED;
            const char *lines[] = {"REPETIR TREINO", "3 RESPOSTAS OK", "", "A REPETIR", "B ENCERRAR"};
            display_show_lines(lines, 5);
        }
        break;
    case RTI_READY:
        if (event == CONTROL_CONFIRM) { training = false; trial = 0; prepare_trial(); }
        break;
    case RTI_TRAINING_FAILED:
        if (event == CONTROL_CONFIRM) {
            result = (rti_result_t){.session_id = session_current()->id};
            trial = training_responses = 0;
            prepare_trial();
        }
        break;
    case RTI_RESULTS:
        break;
    }
    return false;
}

void rti_serial_update(void) {
    bool connected = stdio_usb_connected();
    if (connected && !usb_connected && saved) export_result();
    usb_connected = connected;
}

rti_state_t rti_get_state(void) { return state; }
const rti_result_t *rti_result(void) { return &result; }

void rti_resume_after_pause(uint64_t delta_us) { interval_started_us += delta_us; }
