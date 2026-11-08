#include "prm.h"
#include "session.h"
#include "telemetry.h"
#include "patterns.h"
#include "display.h"
#include "pico/stdlib.h"
#include <stdio.h>

static prm_state_t state;
static prm_result_t result;
static bool training, saved, usb_connected;
static uint8_t deck[PATTERN_COUNT], slots[PRM_TEST_TRIALS];
static uint8_t count, encoding, trial, training_correct, selected, visited;
static uint64_t state_started_us, onset_us;

// Treino usa quatro IDs; avaliação usa outros doze, sem distrator já memorizado.
static uint8_t offset(void) { return training ? 0 : 2 * PRM_TRAINING_TRIALS; }
static uint8_t target(void) { return deck[offset() + count - 1 - trial]; }
static uint8_t novel(void) { return deck[offset() + count + trial]; }

static void export_result(void) {
    printf("# SESSION id=%016llx module=PRM\n", (unsigned long long)result.session_id);
    const char *names[] = {"correct", "wrong", "omission", "aborted"};
    printf("# PRM_ADAPTADO_V1 interrupted=%u correct=%u wrong=%u omissions=%u\n",
           result.interrupted, result.correct, result.wrong, result.omissions);
    printf("prm,phase,trial,target,novel,target_option,selected_option,outcome,onset_us,elapsed_us\n");
    for (uint8_t i = 0; i < result.record_count; ++i) {
        const prm_record_t *r = &result.records[i];
        char choice[4] = "";
        if (r->selected_option != PRM_NO_CHOICE) snprintf(choice, sizeof(choice), "%u", r->selected_option);
        printf("PRM,%s,%u,%u,%u,%u,%s,%s,%llu,%llu\n", r->training ? "training" : "test",
               r->training ? i + 1 : i + 1 - PRM_TRAINING_TRIALS,
               r->target, r->novel, r->target_option, choice, names[r->outcome],
               (unsigned long long)r->onset_us, (unsigned long long)r->elapsed_us);
    }
    printf("# PRM_END records=%u\n", result.record_count);
}

static void publish_result(void) {
    if (!telemetry_enabled()) return;
    for (unsigned i = 0; i < result.record_count; ++i) {
        const prm_record_t *r = &result.records[i];
        telemetry_emit(result.session_id, r->onset_us, "trial", true,
            "{\"module\":\"prm\",\"index\":%u,\"training\":%s,\"outcome\":%u,\"onset_us\":%llu,\"elapsed_us\":%llu,\"target\":%u,\"novel\":%u,\"target_option\":%u,\"selected_option\":%u}",
            i, r->training ? "true" : "false", r->outcome, (unsigned long long)r->onset_us, (unsigned long long)r->elapsed_us,
            r->target, r->novel, r->target_option, r->selected_option);
    }
    telemetry_emit(result.session_id, time_us_64(), "result", true,
        "{\"module\":\"prm\",\"interrupted\":%s,\"correct\":%u,\"wrong\":%u,\"omissions\":%u,\"valid_time_us\":%llu,\"record_count\":%u,\"levels_completed\":%u,\"rounds_started\":%u,\"limit_reached\":%s}",
        result.interrupted ? "true" : "false", result.correct, result.wrong, result.omissions,
        (unsigned long long)result.correct_time_us, result.record_count, 0u, 0u, "false");
}

static void finish(bool interrupted) {
    result.interrupted = interrupted;
    saved = true;
    state = PRM_RESULTS;
    patterns_clear();
    char correct[16], wrong[16], omitted[16], accuracy[24], mean[32];
    unsigned total = result.correct + result.wrong + result.omissions;
    snprintf(correct, sizeof(correct), "ACERTOS %u", result.correct);
    snprintf(wrong, sizeof(wrong), "ERROS %u", result.wrong);
    snprintf(omitted, sizeof(omitted), "OMISSOES %u", result.omissions);
    if (total) snprintf(accuracy, sizeof(accuracy), "ACURACIA %u PC", result.correct * 100u / total);
    else snprintf(accuracy, sizeof(accuracy), "SEM DADOS");
    if (result.correct) snprintf(mean, sizeof(mean), "MEDIA %lu MS", (unsigned long)(result.correct_time_us / result.correct / 1000));
    else snprintf(mean, sizeof(mean), "SEM ACERTOS");
    const char *lines[] = {"PRM ADAPTADO", interrupted ? "INTERROMPIDO" : "CONCLUIDO",
        correct, wrong, omitted, accuracy, mean, "A OU B MENU"};
    display_show_lines(lines, 8);
    publish_result();
    usb_connected = stdio_usb_connected();
    if (usb_connected) export_result();
}

static void show_encoding(void) {
    char progress[24];
    snprintf(progress, sizeof(progress), "PADRAO %u DE %u", encoding + 1, count);
    const char *lines[] = {training ? "TREINO PRM" : "PRM", "MEMORIZE", progress, "", "B ENCERRA"};
    display_show_lines(lines, 5);
    state_started_us = patterns_show(deck[offset() + encoding]);
    state = PRM_ENCODING;
}

static void start_block(void) {
    count = training ? PRM_TRAINING_TRIALS : PRM_TEST_TRIALS;
    encoding = trial = training_correct = 0;
    // Metade dos alvos em cada alternativa, em ordem sorteada.
    for (uint8_t i = 0; i < count; ++i) slots[i] = i % 2;
    patterns_shuffle(slots, count);
    show_encoding();
}

static uint64_t draw_choice(void) {
    const char *lines[] = {training ? "TREINO PRM" : "PRM", "QUAL VOCE VIU?",
        selected ? "OPCAO 2" : "OPCAO 1", "ESQ 1 DIR 2", visited == 3 ? "A CONFIRMA" : "VEJA AS 2",
        "LIMITE 15 S", "B ENCERRA"};
    display_show_lines(lines, 7);
    return patterns_show(selected == slots[trial] ? target() : novel());
}

static void start_choice(void) {
    selected = 0;
    visited = 1;
    onset_us = draw_choice();
    state = PRM_CHOICE;
}

static void record_choice(prm_outcome_t outcome, uint64_t event_us) {
    prm_record_t *r = &result.records[result.record_count++];
    *r = (prm_record_t){.training = training, .target = target(), .novel = novel(),
        .target_option = slots[trial], .selected_option = outcome == PRM_CORRECT || outcome == PRM_WRONG ? selected : PRM_NO_CHOICE,
        .outcome = outcome, .onset_us = onset_us, .elapsed_us = event_us - onset_us};
    if (training) {
        if (outcome == PRM_CORRECT) ++training_correct;
    } else if (outcome == PRM_CORRECT) { ++result.correct; result.correct_time_us += r->elapsed_us; }
    else if (outcome == PRM_WRONG) ++result.wrong;
    else if (outcome == PRM_OMISSION) ++result.omissions;
}

static void end_choice(prm_outcome_t outcome, uint64_t event_us) {
    record_choice(outcome, event_us);
    patterns_clear();
    const char *feedback = outcome == PRM_CORRECT ? "ACERTOU" : outcome == PRM_WRONG ? "ERROU" : "TEMPO ESGOTADO";
    const char *lines[] = {training ? "TREINO PRM" : "PRM", training ? feedback : "AGUARDE", "SOLTE CONTROLES"};
    display_show_lines(lines, 3);
    state_started_us = time_us_64();
    state = PRM_INTERVAL;
}

void prm_begin(void) {
    result = (prm_result_t){.session_id = session_current()->id};
    saved = false;
    training = true;
    for (uint8_t i = 0; i < PATTERN_COUNT; ++i) deck[i] = i;
    patterns_shuffle(deck, PATTERN_COUNT);
    patterns_clear();
    const char *lines[] = {"PRM ADAPTADO", "MEMORIZE FORMAS", "DEPOIS ESCOLHA", "QUAL JA VIU", "ESQ 1 DIR 2", "A CONFIRMA", "B ENCERRA", "A TREINAR"};
    display_show_lines(lines, 8);
    state = PRM_INSTRUCTIONS;
}

bool prm_update(control_event_t event, uint64_t event_us) {
    return prm_update_captured(event, event_us, event_us);
}

bool prm_update_captured(control_event_t event, uint64_t event_us, uint64_t now_us) {
    if (state == PRM_RESULTS) return event == CONTROL_CONFIRM || event == CONTROL_BACK;
    if (event == CONTROL_BACK) {
        if (state == PRM_CHOICE) record_choice(PRM_ABORTED, now_us);
        finish(true);
        return false;
    }
    switch (state) {
    case PRM_INSTRUCTIONS:
        if (event == CONTROL_CONFIRM) start_block();
        break;
    case PRM_ENCODING:
        if (now_us - state_started_us >= PRM_ENCODING_US) {
            state_started_us = patterns_clear(); state = PRM_GAP;
        }
        break;
    case PRM_GAP:
        if (now_us - state_started_us >= PRM_GAP_US) {
            if (++encoding < count) show_encoding();
            else { state_started_us = time_us_64(); state = PRM_RETENTION; }
        }
        break;
    case PRM_RETENTION:
        if (now_us - state_started_us >= PRM_RETENTION_US) start_choice();
        break;
    case PRM_CHOICE:
        if (event != CONTROL_NONE && event_us < onset_us) break;
        if (event == CONTROL_NONE) event_us = now_us;
        if (event_us - onset_us >= PRM_TIMEOUT_US) end_choice(PRM_OMISSION, event_us);
        else if (event == CONTROL_CONFIRM && visited == 3) end_choice(selected == slots[trial] ? PRM_CORRECT : PRM_WRONG, event_us);
        else if (event == CONTROL_LEFT || event == CONTROL_RIGHT) {
            uint8_t next = event == CONTROL_RIGHT;
            if (next != selected) { selected = next; visited |= 1u << selected; draw_choice(); }
        }
        break;
    case PRM_INTERVAL:
        if (now_us - state_started_us < PRM_INTERVAL_US) break;
        if (++trial < count) start_choice();
        else if (!training) finish(false);
        else {
            bool passed = training_correct == count;
            const char *lines[] = {passed ? "TREINO OK" : "REPETIR TREINO", "2 ACERTOS", "", passed ? "A INICIAR" : "A REPETIR", "B ENCERRA"};
            display_show_lines(lines, 5);
            state = passed ? PRM_READY : PRM_TRAINING_FAILED;
        }
        break;
    case PRM_READY:
        if (event == CONTROL_CONFIRM) { training = false; start_block(); }
        break;
    case PRM_TRAINING_FAILED:
        if (event == CONTROL_CONFIRM) { result = (prm_result_t){.session_id = session_current()->id}; start_block(); }
        break;
    case PRM_RESULTS:
        break;
    }
    return false;
}

void prm_serial_update(void) {
    bool connected = stdio_usb_connected();
    if (saved && connected && !usb_connected) export_result();
    usb_connected = connected;
}
prm_state_t prm_get_state(void) { return state; }
const prm_result_t *prm_result(void) { return &result; }
