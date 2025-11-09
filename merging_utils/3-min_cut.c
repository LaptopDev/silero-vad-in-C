// 3-min_cut.c  --  pick largest (candidate) gaps ≤ MAX_SEG, flush on (break)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SEG   30.0   /* max segment length (s) before forcing cut */
#define BREAK_MIN 5.0    /* treat as hard break if >= this */

int main(int argc, char **argv) {
    FILE *f = (argc > 1) ? fopen(argv[1], "r") : stdin;
    if (!f) { perror("open"); return 1; }

    char line[128], tag[32];
    double s = 0.0, e = 0.0;
    double seg_start = 0.0, seg_end = 0.0;
    double prev_end = 0.0;
    double best_gap = 0.0, best_gap_pos = 0.0;
    double gap = 0.0;
    int have_seg = 0;

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "gap =", 5) == 0) {
            tag[0] = 0;
            if (sscanf(line, "gap = %lf %31s", &gap, tag) != 2)
                continue;

            if (strstr(tag, "(candidate)")) {
                if (gap > best_gap) {
                    best_gap = gap;
                    best_gap_pos = prev_end;
                }
            } else if (strstr(tag, "(break)")) {
                if (have_seg)
                    printf("%.3f to %.3f\n", seg_start, prev_end);
                seg_start = prev_end + gap;
                have_seg = 0;
                best_gap = 0;
                best_gap_pos = 0;
            }
        } else if (sscanf(line, "%lf to %lf", &s, &e) == 2) {
            if (!have_seg) {
                seg_start = s;
                have_seg = 1;
            }
            seg_end = e;
            prev_end = e;

            double dur = seg_end - seg_start;
            if (dur >= MAX_SEG && best_gap_pos > seg_start) {
                printf("%.3f to %.3f\n", seg_start, best_gap_pos);
                seg_start = best_gap_pos;
                best_gap = 0;
                best_gap_pos = 0;
            }
        }
    }

    if (have_seg)
        printf("%.3f to %.3f\n", seg_start, seg_end);

    if (f != stdin) fclose(f);
    return 0;
}
