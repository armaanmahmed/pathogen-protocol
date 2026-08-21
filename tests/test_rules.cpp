// Tests for the pure rules in content.cpp. No window, audio or game loop.
#include "game.h"
#include <cmath>
#include <cstdio>

static int failures = 0;

static void check(bool ok, const char *what) {
    if (!ok) { printf("FAIL  %s\n", what); failures++; }
}

static void checkNear(float got, float want, float tol, const char *what) {
    if (fabsf(got - want) > tol) {
        printf("FAIL  %s (got %.3f, want %.3f)\n", what, got, want);
        failures++;
    }
}

// The color rule the tutorial teaches.
static void testColorRule() {
    check(effectiveness(W_LYSOZYME, T_GRAMPOS, nullptr) > 1.0f,
          "lysozyme is strong against gram-positive");
    check(effectiveness(W_LYSOZYME, T_GRAMNEG, nullptr) < 0.5f,
          "lysozyme is blocked by a gram-negative outer membrane");
    check(effectiveness(W_COMPLEMENT, T_GRAMNEG, nullptr) > 1.0f,
          "the complement beam drills gram-negative");
    check(effectiveness(W_OXBURST, T_CATALASE, nullptr) < 0.5f,
          "catalase defeats the oxidative burst");
    check(effectiveness(W_OXBURST, T_FUNGUS, nullptr) > 1.0f,
          "the oxidative burst burns fungus");
    check(effectiveness(W_EOSINOPHIL, T_HELMINTH, nullptr) > 1.0f,
          "eosinophils are built for worms");
    check(effectiveness(W_EOSINOPHIL, T_VIRUS, nullptr) < 0.5f,
          "eosinophils do nothing to viruses");
}

// Every notable multiplier must come with a reason string.
static void testReasonsExist() {
    const int traits[] = {T_GRAMPOS, T_GRAMNEG, T_CAPSULE, T_CATALASE, T_ACIDFAST,
                          T_VIRUS,   T_PROTOZOA, T_HELMINTH, T_FUNGUS,  T_HOST,
                          T_BOSS,    T_INERT};
    for (int w = 0; w < W_COUNT; w++)
        for (int t : traits) {
            const char *why = nullptr;
            float m = effectiveness(w, t, &why);
            check(m >= 0.0f && m <= 5.0f, "multiplier stays in a sane range");
            if (m < 0.6f || m > 1.4f)
                check(why != nullptr && why[0] != '\0',
                      "a notable multiplier explains itself");
        }
}

// Killing your own cells must never be rewarded, whatever you shoot them with.
static void testHostCellsAreNeverGoodTargets() {
    for (int w = 0; w < W_COUNT; w++)
        check(effectiveness(w, T_HOST, nullptr) <= 1.0f, "no weapon is strong against host tissue");
}

// Every organism must be killable by something, or its room cannot be cleared.
static void testEveryOrganismIsKillable() {
    for (int i = 0; i < O_COUNT; i++) {
        const OrgDef &d = ORGS[i];
        if (d.traits & T_HOST) continue;
        bool killable = false;
        for (int w = 0; w < W_COUNT; w++)
            if (effectiveness(w, d.traits, nullptr) >= 0.5f) killable = true;
        if (!killable) { printf("FAIL  %s has no effective weapon\n", d.name); failures++; }
    }
}

// Tables are indexed by enum; a missing row is silent, so check for empty strings.
static void testTablesAreFullyPopulated() {
    for (int i = 0; i < O_COUNT; i++) {
        const OrgDef &d = ORGS[i];
        check(d.name && d.name[0], "organism has a plain name");
        check(d.latin && d.latin[0], "organism has a binomial");
        check(d.threat && d.threat[0], "organism explains its threat");
        check(d.counter && d.counter[0], "organism explains its counter");
        check(d.science && d.science[0], "organism has codex science");
        check(d.hp > 0 && d.radius > 0, "organism has positive hp and radius");
    }
    for (int w = 0; w < W_COUNT; w++) {
        const WeaponDef &x = WEAPONS[w];
        check(x.name && x.name[0], "weapon has a name");
        check(x.shortName && x.shortName[0], "weapon has a rack label");
        check(x.tag && x.tag[0], "weapon has a firing-mode tag");
        check(x.mech && x.mech[0], "weapon explains itself");
        check(x.dmg > 0, "weapon does damage");
        check(weaponReach(w) > 40.0f, "weapon can reach past the player's own radius");
    }
    for (int u = 0; u < U_COUNT; u++) {
        check(UPGRADES[u].name && UPGRADES[u].name[0], "upgrade has a plain name");
        check(UPGRADES[u].real && UPGRADES[u].real[0], "upgrade names the real drug");
        check(UPGRADES[u].desc && UPGRADES[u].desc[0], "upgrade explains its effect");
    }
    // At least one weapon must be free, or a fresh save is unarmed.
    int free_ = 0;
    for (int w = 0; w < W_COUNT; w++)
        if (!UNLOCKS[w].kills && !UNLOCKS[w].bosses && !UNLOCKS[w].viaTutorial) free_++;
    check(free_ >= 1, "at least one weapon is standard issue");
}

// The HUD lob marker uses weaponReach(), so it must match the simulated throw.
static void testLobReachMatchesPhysics() {
    const WeaponDef &w = WEAPONS[W_IGG];
    float pos = 0, vel = w.speed;
    const float dt = 1.0f / 60.0f;
    for (float t = 0; t < w.life; t += dt) {
        vel *= 1.0f - LOB_DRAG * dt;
        pos += vel * dt;
    }
    // Closed form vs per-frame stepping: allow a few percent of discretization error.
    checkNear(weaponReach(W_IGG) - w.area * 0.5f, pos, pos * 0.05f,
              "lob reach matches simulated throw");
}

int main() {
    testColorRule();
    testReasonsExist();
    testHostCellsAreNeverGoodTargets();
    testEveryOrganismIsKillable();
    testTablesAreFullyPopulated();
    testLobReachMatchesPhysics();
    printf(failures ? "TESTS FAILED (%d)\n" : "all rule tests passed (%d failures)\n", failures);
    return failures ? 1 : 0;
}
