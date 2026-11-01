#include "mot.h"
#include "session.h"
#include "telemetry.h"
#include "display.h"
#include "matrix.h"
#include "pico/stdlib.h"
#include <stdio.h>

// Protocolo local v1: sequência fixa para tornar a primeira implementação reproduzível.
// Não representa o protocolo ou os escores normativos do CANTAB.
static const uint8_t targets[MOT_MAX_RECORDS][2] = {
    {1, 2}, {2, 1}, {3, 2}, // Treinamento: alvos próximos ao centro.
    {0, 0}, {4, 4}, {4, 0}, {0, 4}, {2, 0},
    {0, 2}, {4, 2}, {2, 4}, {1, 1}, {3, 3}
};
static mot_state_t state;
static mot_result_t result;
static bool training, saved, usb_connected;
static uint8_t trial, training_correct, cursor_x, cursor_y;
static uint64_t onset_us, interval_started_us;

static unsigned int target_index(void) {
    return training ? trial : MOT_TRAINING_TRIALS + trial;
}

static void clear_matrix(void) {
    npClear();
    npWrite();
}

static void draw_matrix(void) {
    unsigned int index = target_index();
    npClear();
    npSetPixel(targets[index][0], targets[index][1], 0, 20, 0);
    bool on_target = cursor_x == targets[index][0] && cursor_y == targets[index][1];
    npSetPixel(cursor_x, cursor_y, 0, on_target ? 20 : 0, 20);
    npWrite();
}

static void show_results(void) {
    char correct[16], wrong[16], missed[16], accuracy[24], latency[32];
    unsigned int total = result.correct + result.wrong + result.omissions;
    snprintf(correct, sizeof(correct), "ACERTOS %u", result.correct);
    snprintf(wrong, sizeof(wrong), "ERROS %u", result.wrong);
    snprintf(missed, sizeof(missed), "OMISSOES %u", result.omissions);
    if (total) snprintf(accuracy, sizeof(accuracy), "ACURACIA %u PC", result.correct * 100u / total);
    else snprintf(accuracy, sizeof(accuracy), "SEM DADOS");
    if (result.correct)
        snprintf(latency, sizeof(latency), "MEDIA %lu MS", (unsigned long)(result.correct_time_us / result.correct / 1000));
    else snprintf(latency, sizeof(latency), "SEM ACERTOS");
    const char *lines[] = {"MOT ADAPTADO", result.interrupted ? "INTERROMPIDO" : "CONCLUIDO",
        correct, wrong, missed, accuracy, latency, "A OU B MENU"};
    display_show_lines(lines, 8);
}

static void export_result(void) {
    printf("# SESSION id=%016llx module=MOT\n", (unsigned long long)result.session_id);
    // Exportação apenas após encerrar: nenhuma escrita USB no intervalo cronometrado.
    printf("# MOT_ADAPTADO_V1 interrupted=%u correct=%u wrong=%u omissions=%u\n",
           result.interrupted, result.correct, result.wrong, result.omissions);
    printf("mot,phase,trial,target_x,target_y,cursor_x,cursor_y,outcome,onset_us,elapsed_us\n");
    for (unsigned int i = 0; i < result.record_count; ++i) {
        const mot_record_t *r = &result.records[i];
        const char *outcomes[] = {"correct", "wrong", "omission", "aborted"};
        printf("MOT,%s,%u,%u,%u,%u,%u,%s,%llu,%llu\n", r->training ? "training" : "test",
               r->training ? i + 1 : i + 1 - MOT_TRAINING_TRIALS,
               r->target_x, r->target_y, r->cursor_x, r->cursor_y, outcomes[r->outcome],
               (unsigned long long)r->onset_us, (unsigned long long)r->elapsed_us);
    }
    printf("# MOT_END records=%u\n", result.record_count);
}

static void publish_result(void) {
    if (!telemetry_enabled()) return;
    for (unsigned i = 0; i < result.record_count; ++i) {
        const mot_record_t *r = &result.records[i];
        telemetry_emit(result.session_id, r->onset_us, "trial", true,
            "{\"module\":\"mot\",\"index\":%u,\"training\":%s,\"outcome\":%u,\"onset_us\":%llu,\"elapsed_us\":%llu,\"target\":[%u,%u],\"selected\":[%u,%u]}",
            i, r->training ? "true" : "false", r->outcome, (unsigned long long)r->onset_us, (unsigned long long)r->elapsed_us,
            r->target_x, r->target_y, r->cursor_x, r->cursor_y);
    }
    telemetry_emit(result.session_id, time_us_64(), "result", true,
        "{\"module\":\"mot\",\"interrupted\":%s,\"correct\":%u,\"wrong\":%u,\"omissions\":%u,\"valid_time_us\":%llu,\"record_count\":%u,\"levels_completed\":%u,\"rounds_started\":%u,\"limit_reached\":%s}",
        result.interrupted ? "true" : "false", result.correct, result.wrong, result.omissions,
        (unsigned long long)result.correct_time_us, result.record_count, 0u, 0u, "false");
}

static void finish(bool interrupted) {
    result.interrupted = interrupted;
    saved = true;
    state = MOT_RESULTS;
    clear_matrix();
    show_results();
    publish_result();
    usb_connected = stdio_usb_connected();
    if (usb_connected) export_result();
}

static void start_trial(void) {
    char title[16];
    snprintf(title, sizeof(title), "%s %u DE %u", training ? "TREINO" : "MOT",
             trial + 1, training ? MOT_TRAINING_TRIALS : MOT_TEST_TRIALS);
    const char *lines[] = {title, "ALVO VERDE", "CURSOR AZUL", "CIANO SOBREPOE", "MOVA JOYSTICK", "A SELECIONA", "B ENCERRA", "LIMITE 10 S"};
    display_show_lines(lines, 8); // Desenha OLED ANTES de apresentar o alvo.
    cursor_x = cursor_y = 2;
    draw_matrix();
    // Referência de software após o envio/latch dos LEDs; requer validação física.
    onset_us = time_us_64();
    state = MOT_STIMULUS;
}

static void record_trial(mot_outcome_t outcome, uint64_t event_us) {
    unsigned int index = target_index();
    mot_record_t *r = &result.records[result.record_count++];
    *r = (mot_record_t){.training = training, .target_x = targets[index][0],
        .target_y = targets[index][1], .cursor_x = cursor_x, .cursor_y = cursor_y,
        .outcome = outcome, .onset_us = onset_us, .elapsed_us = event_us - onset_us};
    if (training) {
        if (outcome == MOT_CORRECT) ++training_correct;
    } else {
        if (outcome == MOT_CORRECT) { ++result.correct; result.correct_time_us += r->elapsed_us; }
        else if (outcome == MOT_WRONG) ++result.wrong;
        else if (outcome == MOT_OMISSION) ++result.omissions;
    }
    clear_matrix();
}

static void end_trial(mot_outcome_t outcome, uint64_t event_us) {
    record_trial(outcome, event_us);
    const char *feedback = outcome == MOT_CORRECT ? "ACERTOU" : outcome == MOT_WRONG ? "FORA DO ALVO" : "TEMPO ESGOTADO";
    const char *lines[] = {training ? "TREINAMENTO" : "MOT", training ? feedback : "AGUARDE", "", "SOLTE CONTROLES"};
    display_show_lines(lines, 4);
    interval_started_us = time_us_64();
    state = MOT_INTERVAL;
}

void mot_begin(void) {
    result = (mot_result_t){.session_id = session_current()->id};
    saved = false;
    training = true;
    trial = training_correct = 0;
    state = MOT_INSTRUCTIONS;
    clear_matrix();
    const char *lines[] = {"MOT ADAPTADO", "ALVO VERDE", "CURSOR AZUL", "MOVA ATE O ALVO", "A SELECIONA", "B ENCERRA", "A PARA TREINAR", "3 TENTATIVAS"};
    display_show_lines(lines, 8);
}

bool mot_update(control_event_t event, uint64_t event_us) {
    if (state == MOT_RESULTS)
        return event == CONTROL_CONFIRM || event == CONTROL_BACK;
    if (event == CONTROL_BACK) {
        if (state == MOT_STIMULUS) record_trial(MOT_ABORTED, event_us);
        finish(true);
        return false;
    }
    switch (state) {
    case MOT_INSTRUCTIONS:
        if (event == CONTROL_CONFIRM) start_trial();
        break;
    case MOT_STIMULUS:
        // No limite, a omissão tem prioridade sobre a confirmação.
        if (event_us - onset_us >= MOT_TIMEOUT_US) end_trial(MOT_OMISSION, event_us);
        else if (event == CONTROL_CONFIRM) {
            unsigned int index = target_index();
            end_trial(cursor_x == targets[index][0] && cursor_y == targets[index][1] ? MOT_CORRECT : MOT_WRONG, event_us);
        } else {
            uint8_t x = cursor_x, y = cursor_y;
            if (event == CONTROL_LEFT && cursor_x > 0) --cursor_x;
            else if (event == CONTROL_RIGHT && cursor_x < 4) ++cursor_x;
            else if (event == CONTROL_UP && cursor_y > 0) --cursor_y;
            else if (event == CONTROL_DOWN && cursor_y < 4) ++cursor_y;
            if (x != cursor_x || y != cursor_y) draw_matrix();
        }
        break;
    case MOT_INTERVAL:
        if (event_us - interval_started_us < MOT_INTERVAL_US) break;
        ++trial;
        if (trial < (training ? MOT_TRAINING_TRIALS : MOT_TEST_TRIALS)) start_trial();
        else if (!training) finish(false);
        else if (training_correct == MOT_TRAINING_TRIALS) {
            state = MOT_READY;
            const char *lines[] = {"TREINO OK", "TESTE 10 ALVOS", "", "A INICIAR", "B ENCERRAR"};
            display_show_lines(lines, 5);
        } else {
            state = MOT_TRAINING_FAILED;
            const char *lines[] = {"REPETIR TREINO", "ACERTE 3 ALVOS", "", "A REPETIR", "B ENCERRAR"};
            display_show_lines(lines, 5);
        }
        break;
    case MOT_READY:
        if (event == CONTROL_CONFIRM) { training = false; trial = 0; start_trial(); }
        break;
    case MOT_TRAINING_FAILED:
        if (event == CONTROL_CONFIRM) {
            result = (mot_result_t){.session_id = session_current()->id}; // Retém somente a última rodada de treino.
            trial = training_correct = 0;
            start_trial();
        }
        break;
    case MOT_RESULTS:
        break;
    }
    return false;
}

void mot_serial_update(void) {
    bool connected = stdio_usb_connected();
    if (connected && !usb_connected && saved) export_result();
    usb_connected = connected;
}

mot_state_t mot_get_state(void) { return state; }
const mot_result_t *mot_result(void) { return &result; }

void mot_resume_after_pause(uint64_t delta_us) { interval_started_us += delta_us; }
