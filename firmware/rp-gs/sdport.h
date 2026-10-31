// SD на плате: SPI1 + DMA. На макете карты нет: ножки GP28-31 заняты шиной, порт не включаем (BOARD_HAS_SD)
#ifndef SDPORT_H
#define SDPORT_H

void sdport_init(void);         // ножки, DMA, порт в sd_card
int sdport_present(void);       // датчик карты в слоте

#endif
