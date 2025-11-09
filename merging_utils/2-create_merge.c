// 2-create_merge.c  --  merge contiguous spans separated by "(merge)" gaps
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    FILE *f = (argc > 1) ? fopen(argv[1], "r") : stdin;
    if (!f) { perror("open"); return 1; }

    char line[128];
    double curr_start = 0.0, curr_end = 0.0;
    double next_start = 0.0, next_end = 0.0;
    int have_curr = 0;
    int merge_next = 0;

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "gap =", 5) == 0) {
            // gap line
            if (strstr(line, "(merge)")) {
                merge_next = 1;
            } else if (strstr(line, "(candidate)") || strstr(line, "(break)")) {
                // flush current merged segment before printing candidate/break
                if (have_curr) {
                    printf("%.3f to %.3f\n", curr_start, curr_end);
                    have_curr = 0;
                }
                fputs(line, stdout); // keep the tag line
            } else {
                // unknown tag -> just print
                if (have_curr) {
                    printf("%.3f to %.3f\n", curr_start, curr_end);
                    have_curr = 0;
                }
                fputs(line, stdout);
            }
        } else if (sscanf(line, "%lf to %lf", &next_start, &next_end) == 2) {
            // timestamp line
            if (!have_curr) {
                curr_start = next_start;
                curr_end = next_end;
                have_curr = 1;
            } else if (merge_next) {
                // extend current segment
                curr_end = next_end;
                merge_next = 0;
            } else {
                // flush previous segment before starting new one
                printf("%.3f to %.3f\n", curr_start, curr_end);
                curr_start = next_start;
                curr_end = next_end;
            }
        }
    }

    if (have_curr)
        printf("%.3f to %.3f\n", curr_start, curr_end);

    if (f != stdin) fclose(f);
    return 0;
}
