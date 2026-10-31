// Настройки во флеше RP (doc/trinity-config.md, п.46). Сохранение - только по команде: сам не пишет
#ifndef CFGSTORE_H
#define CFGSTORE_H

int cfgstore_load(void);        // 1 - прочитано, 0 - нет или испорчено: остаются умолчания
int cfgstore_save(void);        // ядро 0. На время записи ядро 1 стоит, прерывания запрещены

#endif
