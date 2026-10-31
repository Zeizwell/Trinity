// Вырезает из pt2_tables.c две таблицы, нужные стенду: весь файл тянет за собой интерфейс pt2-clone
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    static char line[4096];
    if (argc < 3) return 1;
    FILE *in = fopen(argv[1], "r"), *out = fopen(argv[2], "w");
    if (!in || !out) return 1;
    int copy = 0;
    fputs("#include <stdint.h>\n", out);
    while (fgets(line, sizeof line, in)) {
        if (strstr(line, "const uint8_t vibratoTable[") || strstr(line, "const int16_t periodTable[")) copy = 1;
        if (copy) fputs(line, out);
        if (copy && strstr(line, "};")) copy = 0;
    }
    fclose(in), fclose(out);
    return 0;
}
