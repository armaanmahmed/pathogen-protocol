#include "game.h"
#include <cmath>

static const char *BIOME_NAMES[3] = {"BLOODSTREAM", "LUNGS", "GUT"};

const char *biomeName(int b) { return BIOME_NAMES[(b < 0 || b > 2) ? 0 : b]; }

// Real lab stain colors; they double as the game's color code.
static const Color C_GRAMPOS  = {150, 90, 205, 255};
static const Color C_GRAMNEG  = {235, 105, 140, 255};
static const Color C_ACIDFAST = {225, 60, 95, 255};
static const Color C_VIRUS    = {90, 210, 130, 255};
static const Color C_PROTOZOA = {95, 195, 205, 255};
static const Color C_HELMINTH = {214, 184, 128, 255};
static const Color C_FUNGUS   = {200, 232, 236, 255};
static const Color C_HOSTRBC  = {198, 58, 62, 255};
static const Color C_HOSTCELL = {236, 196, 186, 255};
static const Color C_ALLY     = {150, 175, 200, 255};

// name, latin, traits, hp, radius, speed, contact, shape, behavior, color,
// threat, counter, science
const OrgDef ORGS[O_COUNT] = {
{"Red blood cell", "erythrocyte", T_HOST, 20, 15, 16, 0, SH_BICONCAVE, B_HOST_DRIFT, C_HOSTRBC,
 "Yours. Every one you lose shrinks your oxygen tank.",
 "FRIENDLY - do not shoot it.",
 "Erythrocytes carry oxygen on hemoglobin. They have no nucleus and cannot repair themselves, so "
 "the body can only replace them, slowly, from bone marrow. Destroying them faster than that is "
 "hemolytic anemia."},

{"Lung cell", "airway epithelium", T_HOST, 40, 20, 0, 0, SH_BLOB, B_STATIONARY, C_HOSTCELL,
 "Yours. Flu can only breed by breaking into one of these.",
 "FRIENDLY - keep them alive and the flu has nowhere to go.",
 "Airway epithelium lines your lungs. Influenza latches onto sialic acid on its surface and "
 "forces the cell to build new virus particles, which bud off its surface until the cell dies."},

{"Macrophage", "big eater cell", T_HOST, 90, 22, 60, 0, SH_BLOB, B_ALLY_HUNT, C_ALLY,
 "ALLY. It hunts germs for you - unless TB gets inside it.",
 "Let it fight. If TB touches it, it turns on you.",
 "Macrophages swallow pathogens and show the pieces to the rest of the immune system. "
 "Tuberculosis survives being swallowed and uses the macrophage as a hideout."},

{"Neutrophil", "first responder cell", T_HOST, 55, 16, 130, 0, SH_LOBED, B_ALLY_HUNT, C_ALLY,
 "ALLY. Fast, aggressive, dies quickly.",
 "Let it fight.",
 "The most common white blood cell. It kills with a burst of bleach-like chemicals and dies soon "
 "after - a pile of dead neutrophils is what pus is."},

{"Malaria parasite", "Plasmodium falciparum", T_PROTOZOA, 14, 8, 155, 6, SH_RINGFORM, B_SEEK_RBC, C_PROTOZOA,
 "Ignores you and races for your red blood cells.",
 "Shoot it BEFORE it reaches one. Any weapon works.",
 "The blood stage of malaria. It burrows into a red cell, eats the hemoglobin inside, and "
 "multiplies there where antibodies cannot reach it."},

{"Infected blood cell", "schizont", T_PROTOZOA, 30, 16, 12, 0, SH_BICONCAVE, B_INFECTED_BURST, {170, 60, 110, 255},
 "A red cell full of parasites, counting down to a burst.",
 "Kill it before the ring closes or it splits into four more.",
 "A real one releases 16 to 32 new parasites. In many malaria infections they all rupture in "
 "step, which is why the fever comes in regular waves rather than staying steady."},

{"Blood yeast", "Candida albicans", T_FUNGUS, 45, 13, 55, 8, SH_BUDDING, B_BUDDING, C_FUNGUS,
 "Drifts with the blood and buds off copies of itself.",
 "Kill it early, before it multiplies. [3] burns fungus.",
 "A yeast that normally lives harmlessly in the mouth and gut. If it reaches the blood it multiplies "
 "by budding: a daughter cell swells out of the parent and pinches off. Neutrophils are the main "
 "defense against it."},

{"Shapeshifter", "Trypanosoma brucei", T_PROTOZOA, 48, 12, 125, 10, SH_SPIRAL, B_WOBBLE_CHASE, C_PROTOZOA,
 "Flashes WHITE and goes nearly bulletproof for a moment.",
 "Wait for the white flash to end, then fire.",
 "The sleeping sickness parasite swaps its entire surface coat over and over, so your antibodies "
 "are always chasing a disguise it has already dropped. That is why there is no vaccine for it."},

{"Staph", "Staphylococcus aureus", T_GRAMPOS | T_CATALASE, 60, 13, 78, 12, SH_CLUSTER, B_CHASE, C_GRAMPOS,
 "Grape-like clumps that walk straight at you.",
 "PURPLE - use [1]. Never [3]; it eats peroxide for breakfast.",
 "Catalase-positive: it splits hydrogen peroxide, the chemical your cells use to kill bacteria, "
 "into water and oxygen. Game liberties: a healthy oxidative burst still beats catalase, and "
 "real staph chemically hardens its wall against lysozyme."},

{"E. coli", "Escherichia coli", T_GRAMNEG, 34, 11, 165, 9, SH_ROD, B_DART, C_GRAMNEG,
 "Darts at you in sudden bursts of speed.",
 "PINK - use [2]. Wall weapons bounce off.",
 "Its greasy outer membrane hides the cell wall from enzymes. Part of that membrane (LPS) is the "
 "toxin that sends people into septic shock when the bacteria die in bulk."},

{"Meningitis germ", "Neisseria meningitidis", T_GRAMNEG | T_CAPSULE, 55, 12, 95, 14, SH_DIPLOCOCCUS, B_CHASE, C_GRAMNEG,
 "Pink, paired, and wearing a slime shell that blocks damage.",
 "Pop the shell with [4], then finish with [2].",
 "People born without complement proteins get this infection again and again - proof that "
 "complement is the specific defense your body uses against Neisseria."},

{"Flu virus", "Influenza A", T_VIRUS, 12, 9, 100, 8, SH_SPIKED, B_SEEK_EPITHELIAL, C_VIRUS,
 "Beelines for your lung cells and breaks in.",
 "Intercept it in the air. It has no wall, so [1] and [2] are weak.",
 "The spikes are two tools: one grabs the cell, the other cuts new virus free after copying. A "
 "virus cannot reproduce alone - it has to hijack one of your cells, which dies making more "
 "virus."},

{"Pneumonia germ", "Streptococcus pneumoniae", T_GRAMPOS | T_CAPSULE, 65, 12, 74, 12, SH_DIPLOCOCCUS, B_CHASE, C_GRAMPOS,
 "Purple pairs in a slime shell. Shrugs off almost everything.",
 "Strip the shell with [4], then hit it with [1].",
 "The pneumonia vaccine teaches your body to make antibody against that exact slime shell. Real "
 "antibody coats the shell rather than stripping it, flagging the germ to be eaten."},

{"TB", "Mycobacterium tuberculosis", T_ACIDFAST | T_CATALASE, 95, 13, 46, 14, SH_ROD, B_CHASE, C_ACIDFAST,
 "Slow, armored, and shrugs off nearly every weapon.",
 "You need the Armor Breaker drug. Without it, this is a long fight.",
 "Its wall is coated in wax that blocks enzymes, drugs and disinfectants. It can also shut down "
 "into a dormant state instead of dying, which is why TB treatment runs six months."},

{"Sleeping TB", "dormant bacillus", T_ACIDFAST, 22, 9, 0, 0, SH_ROD, B_DORMANT, {150, 95, 80, 255},
 "Sits still doing nothing - then wakes back up as full TB.",
 "Kill it while it is asleep. It is weak right now.",
 "About a quarter of all people alive are carrying dormant TB. It reactivates whenever the "
 "immune system dips, which is why it is so hard to wipe out."},

{"Lung mold", "Aspergillus fumigatus", T_FUNGUS, 70, 14, 0, 10, SH_HYPHA, B_HYPHA_GROW, C_FUNGUS,
 "Does not chase you. It grows - and keeps growing.",
 "[3] burns fungus. Cut it back early or it fills the room.",
 "A mold that spreads as a branching network of threads rather than swimming. In people with weak "
 "immune systems it grows straight through blood vessel walls."},

{"Hospital superbug", "Klebsiella pneumoniae", T_GRAMNEG | T_CAPSULE, 80, 14, 62, 15, SH_ROD, B_CHASE, C_GRAMNEG,
 "Thick slime shell, tanky, hits hard.",
 "[4] to strip the shell, then [2].",
 "Its slime coat is so thick the mucus it causes is described as looking like currant jelly. It is "
 "one of the leading causes of drug-resistant hospital infections."},

{"Cholera", "Vibrio cholerae", T_GRAMNEG, 30, 10, 150, 8, SH_COMMA, B_KEEPAWAY_SHOOT, C_GRAMNEG,
 "Keeps its distance and spits toxin that drains your water.",
 "Dodge the blue blobs. PINK, so [2] kills it.",
 "Cholera never actually invades you. It sits in your gut and releases a toxin that jams your "
 "cells open, pouring out water. People die of dehydration, not of the bacteria."},

{"Rotavirus", "Rotavirus A", T_VIRUS, 26, 11, 190, 12, SH_WHEEL, B_BOUNCE, C_VIRUS,
 "Ricochets off the walls at high speed.",
 "Lead your shots. [3] handles viruses best.",
 "A wheel-shaped virus and the biggest cause of severe diarrhea in small children worldwide. It "
 "survives on surfaces for days, which is why it spreads so fast."},

{"Gut amoeba", "Entamoeba histolytica", T_PROTOZOA, 95, 18, 70, 18, SH_AMOEBA, B_AMOEBA_LUNGE, {120, 200, 170, 255},
 "Crawls slowly, then LUNGES. Watch the red line.",
 "Dash sideways during the wind-up. [5] hurts it most.",
 "It moves by pushing out blobs of itself and literally eats tissue, carving ulcers into the "
 "colon wall. Its name means tissue-dissolving."},

{"C. diff", "Clostridioides difficile", T_GRAMPOS, 50, 12, 88, 12, SH_ROD, B_CHASE, C_GRAMPOS,
 "Purple rod. Leaves a spore behind when it dies.",
 "[1] kills it - then kill the spore too.",
 "It takes over after antibiotics wipe out your normal gut bacteria. Its spores survive alcohol "
 "hand gel, which is why it spreads through hospitals so relentlessly."},

{"C. diff spore", "endospore", T_GRAMPOS | T_CAPSULE, 30, 8, 0, 0, SH_EGG, B_DORMANT, {180, 165, 140, 255},
 "An armored seed. Leave it and it grows back into C. diff.",
 "Tough shell, but it cannot fight back. Clean it up.",
 "A spore is a bacterium sealed into a survival pod. Alcohol gel does not kill these - soap and "
 "water wash them off, and bleach destroys them."},

{"Fluke egg", "Schistosoma ovum", T_HELMINTH, 18, 10, 0, 0, SH_EGG, B_EGG, {225, 205, 120, 255},
 "Sits there pulsing damage at anything nearby.",
 "Clear eggs fast. [5] shreds worm tissue.",
 "In this disease it is the immune reaction to the eggs, not the worm itself, that scars the "
 "liver, gut or bladder. The damage is friendly fire."},

{"Worm larva", "Ascaris larva", T_HELMINTH, 14, 8, 185, 8, SH_WORM, B_CHASE, C_HELMINTH,
 "Fast and fragile. Comes in numbers.",
 "TAN = worm. [5] is built for these.",
 "Baby roundworms migrate through the liver, up into the lungs, get coughed up, get swallowed, "
 "and finish growing up in the gut."},

{"BLOOD FLUKE PAIR", "Schistosoma mansoni", T_HELMINTH | T_BOSS, 700, 24, 95, 20, SH_WORM, B_WORMBOSS, {160, 125, 170, 255},
 "BOSS. Chases you and drops eggs in its wake.",
 "[5] is the anti-worm gun. Clear the eggs or they bury you.",
 "The male carries the female in a groove along his body. They stay locked together in your veins "
 "for years, shedding hundreds of eggs every day."},

{"TB GRANULOMA", "caseating granuloma", T_ACIDFAST | T_BOSS, 850, 46, 0, 22, SH_GRANULOMA, B_GRANULOMA_CORE, {215, 205, 175, 255},
 "BOSS. A living core behind a rotating wall of cells.",
 "Break the orbiting wall first, then hit the core.",
 "This is your immune system building a prison around TB it cannot kill. The wall traps the "
 "bacteria - and also shelters them from everything you throw at it."},

{"Foamy macrophage", "lipid-laden macrophage", T_HOST | T_BOSS, 75, 18, 0, 10, SH_BLOB, B_ORBIT_PARENT, {190, 180, 205, 255},
 "Part of the granuloma wall.",
 "Break through to reach the core.",
 "A macrophage so stuffed with fat that it looks foamy under a microscope. TB lives inside these "
 "cells while they form the wall meant to contain it."},

{"GIANT ROUNDWORM", "Ascaris lumbricoides", T_HELMINTH | T_BOSS, 1050, 28, 115, 24, SH_WORM, B_BURROWER, C_HELMINTH,
 "BOSS. Burrows, then erupts underneath you.",
 "Watch the red circle and MOVE. [5] hurts it most.",
 "Up to 35 cm long and living in hundreds of millions of people right now. A heavy infestation "
 "can physically block the intestine."},

{"Debris", "inert particle", T_INERT, 14, 12, 0, 0, SH_DEBRIS, B_STATIONARY, {150, 150, 160, 255},
 "Harmless junk. Target practice.",
 "Shoot it.",
 "Cellular debris - the leftovers of cells that have already died."},
};

// name, short, tag, mech, kind, dmg, cooldown, speed, spread, life, area, pellets, pierce
const WeaponDef WEAPONS[W_COUNT] = {
{"Lysozyme Stream", "LYSOZYME", "AUTO",
 "Hold to hose enzyme. Melts PURPLE bugs, useless on almost everything else.",
 WK_STREAM, 10, 0.075f, 620, 7, 1.1f, 0, 1, 0},

{"Complement Beam", "COMPLEMENT", "BEAM",
 "A continuous drill that pierces every target in a line. Overheats if you hold it.",
 WK_BEAM, 82, 0.0f, 520, 0, 0, 7, 1, 0},

{"Oxidative Burst", "OX BURST", "BLAST",
 "Unaimed point-blank shockwave. Wrecks fungus, spares your own cells, useless on catalase bugs.",
 WK_NOVA, 40, 1.15f, 0, 0, 0, 165, 1, 0},

{"IgG Tag Grenade", "IgG", "LOB",
 "Lobs an antibody cloud. Everything inside loses its shell and takes +60% damage.",
 WK_LOB, 8, 1.05f, 640, 0, 0.6f, 108, 1, 0},

{"Eosinophil Seekers", "EOSINOPHIL", "SEEKER",
 "Homing granules that hunt parasites and worms. They never chase your own cells.",
 WK_SEEKER, 19, 0.44f, 400, 12, 2.4f, 0, 2, 1},
};

// kills, bosses, viaTutorial, how
// Order must match WeaponId: Lysozyme, Complement, OxBurst, IgG, Eosinophil.
const WeaponUnlock UNLOCKS[W_COUNT] = {
{0,   0, false, "standard issue"},
{30,  0, true,  "finish training, or 30 career kills"},
{200, 0, false, "200 career kills"},
{80,  0, true,  "finish training, or 80 career kills"},
{0,   1, false, "defeat any boss"},
};

const UpgradeDef UPGRADES[U_COUNT] = {
{"Antimalarial",     "artemisinin",         "Double damage to malaria parasites and infected blood cells."},
{"Flu Blocker",      "oseltamivir",         "Hijacked lung cells release 2 fewer virus particles."},
{"Armor Breaker",    "isoniazid",           "Stops TB building its waxy armor. TB takes full damage."},
{"Rehydration",      "oral rehydration salts", "Cholera toxin can no longer drain your water."},
{"Blood Booster",    "erythropoietin (EPO)", "Rebuilds lost oxygen capacity back to full."},
{"Adrenaline",       "epinephrine",         "+12% move speed and a faster dash."},
{"Antiviral Shield", "interferon",          "Lung cells resist infection 4s longer; viruses take +40%."},
{"Fever",            "pyrogen",             "+22% fire rate, but oxygen recovers 25% slower."},
{"Hull Patch",       "albumin",             "+15 max hull and heal 40. Can be taken again."},
{"Heavy Antibiotic", "vancomycin",          "+80% damage to PURPLE bugs."},
};

float effectiveness(int weapon, int traits, const char **reason) {
    const char *why = nullptr;
    float m = 1.0f;

    if (traits & T_INERT) { if (reason) *reason = nullptr; return 1.0f; }

    // Host cells first: every weapon is weak against them, and the HUD must
    // never recommend shooting the patient.
    if (traits & T_HOST) {
        if (reason) *reason = "your own cell";
        return 0.5f;
    }

    switch (weapon) {
        case W_LYSOZYME:
            // Cleaves the cell wall - bare in purple bugs, hidden in pink ones.
            if (traits & T_GRAMPOS) { m = 2.0f; why = "bare cell wall"; }
            else if (traits & T_GRAMNEG) { m = 0.35f; why = "outer membrane blocks it"; }
            else if (traits & T_ACIDFAST) { m = 0.30f; why = "waxy armor"; }
            else if (traits & T_VIRUS) { m = 0.45f; why = "no cell wall to cut"; }
            else if (traits & T_HELMINTH) { m = 0.30f; why = "tough worm skin"; }
            else if (traits & T_PROTOZOA) { m = 0.75f; why = nullptr; }
            break;
        case W_COMPLEMENT:
            // Drills holes in outer membranes; a thick wall or a slime shell stops it.
            if (traits & T_CAPSULE) { m = 0.20f; why = "slime shell blocks it"; }
            else if (traits & T_GRAMNEG) { m = 2.2f; why = "drills the outer membrane"; }
            else if (traits & T_GRAMPOS) { m = 0.30f; why = "wall too thick to drill"; }
            else if (traits & T_ACIDFAST) { m = 0.35f; why = "waxy armor"; }
            else if (traits & T_HELMINTH) { m = 0.40f; why = "too big to pop"; }
            break;
        case W_OXBURST:
            // Peroxide. Catalase-positive organisms simply digest it.
            if (traits & T_CATALASE) { m = 0.25f; why = "it eats peroxide"; }
            else if (traits & T_FUNGUS) { m = 1.8f; why = "burns fungus"; }
            else if (traits & T_VIRUS) { m = 1.5f; why = "shreds the virus coat"; }
            else m = 1.4f;
            break;
        case W_IGG:
            // Barely damages anything; its job is stripping shells and marking targets.
            if (traits & T_CAPSULE) { m = 1.0f; why = "eating the shell"; }
            break;
        case W_EOSINOPHIL:
            // The body's dedicated anti-parasite weapon, and nothing else.
            if (traits & T_HELMINTH) { m = 2.6f; why = "built for worms"; }
            else if (traits & T_PROTOZOA) { m = 1.5f; why = "counts as a parasite"; }
            else if (traits & T_VIRUS) { m = 0.40f; why = "does nothing to viruses"; }
            else m = 0.55f, why = "not a parasite";
            break;
        default: break;
    }
    if (reason) *reason = why;
    return m;
}

float weaponReach(int weapon) {
    const WeaponDef &w = WEAPONS[weapon];
    switch (w.kind) {
        case WK_BEAM:   return w.speed;
        case WK_NOVA:   return w.area;
        // Drag makes speed * life overstate the throw; integrate it, plus half the cloud.
        case WK_LOB:    return w.speed * (1.0f - expf(-LOB_DRAG * w.life)) / LOB_DRAG + w.area * 0.5f;
        default:        return w.speed * w.life;
    }
}
