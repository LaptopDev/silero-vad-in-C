// 1-create_tag.c  --  ultra-fast gap annotator for VAD timestamp lists
#include <stdio.h>
#include <stdlib.h>

#define MICRO_PAUSE 0.15   /* merge threshold (s) */
#define LONG_BREAK  5.0    /* break threshold (s) */

int main(int argc, char **argv) {
    FILE *f = (argc > 1) ? fopen(argv[1], "r") : stdin;
    if (!f) { perror("open"); return 1; }

    double prev_start = 0.0, prev_end = 0.0;
    int have_prev = 0;
    char line[128];

    while (fgets(line, sizeof(line), f)) {
        double s, e;
        if (sscanf(line, "%lf to %lf", &s, &e) != 2) continue;

        if (have_prev) {
            double gap = s - prev_end;
            const char *tag =
                (gap < MICRO_PAUSE) ? "(merge)"
                : (gap > LONG_BREAK) ? "(break)"
                : "(candidate)";
            printf("gap = %.3f %s\n", gap, tag);
        }

        printf("%.3f to %.3f\n", s, e);
        prev_start = s;
        prev_end = e;
        have_prev = 1;
    }

    if (f != stdin) fclose(f);
    return 0;
}
