#ifndef GS_BLEP_H
#define GS_BLEP_H

#define GS_BLEP_NS   16         // отсчётов поправки на фронт
#define GS_BLEP_MASK 31

typedef struct {
    int   index, left;
    float buf[GS_BLEP_MASK + 1];
    float last;                 // последний уровень
} gs_blep_t;

void  gs_blep_add(gs_blep_t *b, float offset, float amp);   // фронт: offset 0..1 - где внутри отсчёта, amp - прошлый минус новый
float gs_blep_run(gs_blep_t *b, float in);

#endif
