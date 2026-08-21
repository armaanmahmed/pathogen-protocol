#include "game.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

// Strict integer parse: rejects garbage and out-of-range values, unlike atoi.
bool parseInt(const char *s, int lo, int hi, int &out) {
    char *end = nullptr;
    long v = strtol(s, &end, 10);
    if (!end || *end != '\0' || end == s) return false;
    if (v < lo || v > hi) return false;
    out = (int)v;
    return true;
}

void usage() {
    printf("pathogen_protocol [options]\n"
           "  --smoke            run headless with an autopilot, print a report, exit\n"
           "  --frames N         smoke-run length in frames at 60fps (default 3600)\n"
           "  --start-biome N    0 bloodstream, 1 lungs, 2 gut\n"
           "  --seed N           fix the RNG so a run is reproducible\n"
           "  --skip-tutorial    jump straight into a run\n"
           "  --tutorial         force the tutorial, even under --smoke\n");
}

}   // namespace

int main(int argc, char **argv) {
    Game g;
    bool smoke = false;
    int v = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        auto value = [&](const char *name) -> const char * {
            if (i + 1 >= argc) { fprintf(stderr, "%s needs a value\n", name); exit(2); }
            return argv[++i];
        };
        if (!strcmp(a, "--smoke")) smoke = true;
        else if (!strcmp(a, "--skip-tutorial")) g.skipTutorial = true;
        else if (!strcmp(a, "--tutorial")) g.forceTutorial = true;
        else if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(); return 0; }
        else if (!strcmp(a, "--frames")) {
            if (!parseInt(value(a), 1, 100000000, v)) { fprintf(stderr, "bad --frames\n"); return 2; }
            g.smokeFrames = v;
        } else if (!strcmp(a, "--start-biome")) {
            if (!parseInt(value(a), 0, 2, v)) { fprintf(stderr, "bad --start-biome (want 0-2)\n"); return 2; }
            g.startBiome = v;
        } else if (!strcmp(a, "--seed")) {
            if (!parseInt(value(a), 1, 2000000000, v)) { fprintf(stderr, "bad --seed\n"); return 2; }
            g.seed = (unsigned)v;
        } else {
            fprintf(stderr, "unknown option: %s\n", a);
            usage();
            return 2;
        }
    }
    return g.run(smoke);
}
