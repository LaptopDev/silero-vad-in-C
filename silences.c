#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    double gap;
    double prev_end;
    double start;
} Gap;

int cmp_gap_desc(const void *a, const void *b) {
    const Gap *A = a, *B = b;
    if (A->gap < B->gap) return 1;
    if (A->gap > B->gap) return -1;
    return 0;
}

int cmp_gap_asc(const void *a, const void *b) {
    const Gap *A = a, *B = b;
    if (A->gap < B->gap) return -1;
    if (A->gap > B->gap) return 1;
    return 0;
}

int main(int argc, char **argv) {
    int mode = 0;              // 0 = time, 1 = desc, 2 = asc
    const char *fname = NULL;

    if (argc == 2) {
        fname = argv[1];
    } else if (argc == 3) {
        if (!strcmp(argv[1], "--long"))
            mode = 1;
        else if (!strcmp(argv[1], "--short"))
            mode = 2;
        else {
            fprintf(stderr,
                    "Usage: %s [--sort|--rsort] <timestamps.txt>\n", argv[0]);
            return 1;
        }
        fname = argv[2];
    } else {
        fprintf(stderr,
                "Usage: %s [--sort|--rsort] <timestamps.txt>\n", argv[0]);
        return 1;
    }

    FILE *f = fopen(fname, "r");
    if (!f) {
        perror("fopen");
        return 1;
    }

    char line[256];
    double prev_end = 0.0;
    int first = 1;

    size_t capacity = 1024, n = 0;
    Gap *gaps = malloc(capacity * sizeof(Gap));
    if (!gaps) {
        perror("malloc");
        return 1;
    }

    while (fgets(line, sizeof(line), f)) {
        double s, e;
        char *p = strstr(line, "from ");
        if (!p) continue;
        if (sscanf(p, "from %lf s to %lf s", &s, &e) == 2) {
            if (!first) {
                if (n >= capacity) {
                    capacity *= 2;
                    gaps = realloc(gaps, capacity * sizeof(Gap));
                    if (!gaps) {
                        perror("realloc");
                        return 1;
                    }
                }
                gaps[n].gap = s - prev_end;
                gaps[n].prev_end = prev_end;
                gaps[n].start = s;
                n++;
            }
            prev_end = e;
            first = 0;
        }
    }
    fclose(f);

    if (mode == 1)
        qsort(gaps, n, sizeof(Gap), cmp_gap_desc);
    else if (mode == 2)
        qsort(gaps, n, sizeof(Gap), cmp_gap_asc);
    // mode 0 = no sorting → chronological

    for (size_t i = 0; i < n; i++)
        printf("%.3f %.3f %.3f\n",
               gaps[i].gap, gaps[i].prev_end, gaps[i].start);

    free(gaps);
    return 0;
}
