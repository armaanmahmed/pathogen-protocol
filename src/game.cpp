#include "game.h"
#include "vecmath.h"
#include <algorithm>
#include <bitset>
#include <cstdio>
#include <cstdlib>
#include <fstream>

// ============================================================================
// Run flow, persistence, audio and the main loop.
// ============================================================================

float Game::frand(float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); }
int Game::irand(int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); }

// ---------- persistence ----------

static std::string savePath() {
    const char *home = getenv("HOME");
    return std::string(home ? home : ".") + "/.pathogen_protocol_save";
}

void Game::loadMeta() {
    if (smoke) return;
    std::ifstream f(savePath());
    // `trained` is a later addition; an older three-field save leaves it at 0.
    if (f) f >> meta.kills >> meta.bosses >> meta.victories >> meta.trained;
    // Guard against a truncated or hand-edited save.
    meta.kills = std::max(0, meta.kills);
    meta.bosses = std::max(0, meta.bosses);
    meta.victories = std::max(0, meta.victories);
    meta.trained = std::max(0, meta.trained);
}

// Write to a temp file and rename, so a crash cannot leave a half-written save.
void Game::saveMeta() {
    if (smoke) return;   // test runs never touch the save
    std::string tmp = savePath() + ".tmp";
    {
        std::ofstream f(tmp);
        if (!f) return;
        f << meta.kills << " " << meta.bosses << " " << meta.victories << " " << meta.trained << "\n";
        if (!f.good()) return;
    }
    std::rename(tmp.c_str(), savePath().c_str());
}

// Fold this run's kills into the career total and zero runKills, so
// careerKills() cannot double-count.
void Game::bankRun() {
    lastRunKills = runKills;
    meta.kills += runKills;
    runKills = 0;
    saveMeta();
    refreshUnlocks(false);
}

// ---------- audio ----------

// Builds a short procedural sound. LoadSoundFromWave copies the samples, so
// the Wave must not be UnloadWave'd.
static Sound makeTone(float freq, float dur, float vol, bool noise, float slide) {
    const int rate = 22050;
    int n = std::max(32, (int)(dur * rate));
    std::vector<short> data(n);
    unsigned int rs = 0x9E3779B9u;
    for (int i = 0; i < n; i++) {
        float t = (float)i / rate;
        float env = 1.0f - (float)i / (float)n;
        float s;
        if (noise) {
            rs = rs * 1664525u + 1013904223u;
            s = ((rs >> 16) & 0xFFFF) / 32768.0f - 1.0f;
        } else s = sinf(2.0f * PI * (freq + slide * t) * t) > 0 ? 0.5f : -0.5f;
        data[i] = (short)(s * env * env * vol * 32000.0f);
    }
    Wave w;
    w.frameCount = (unsigned int)n;
    w.sampleRate = rate;
    w.sampleSize = 16;
    w.channels = 1;
    w.data = data.data();
    return LoadSoundFromWave(w);
}

void Game::initAudio() {
    InitAudioDevice();
    audioOk = IsAudioDeviceReady();
    if (!audioOk) return;
    sfxShoot   = makeTone(880, 0.05f, 0.22f, false, -3000);
    sfxHit     = makeTone(320, 0.05f, 0.30f, true, 0);
    sfxResist  = makeTone(150, 0.09f, 0.28f, false, -400);
    sfxBurst   = makeTone(140, 0.30f, 0.55f, true, -100);
    sfxPick    = makeTone(660, 0.15f, 0.35f, false, 500);
    sfxHurt    = makeTone(180, 0.20f, 0.50f, false, -120);
    sfxBossDie = makeTone(90, 0.70f, 0.65f, true, -60);
    sfxBeam    = makeTone(520, 0.07f, 0.16f, false, 900);
    sfxNova    = makeTone(200, 0.35f, 0.60f, true, -260);
}

// Close the device even if it never came up ready.
void Game::shutdownAudio() {
    if (audioOk) {
        Sound *all[] = {&sfxShoot, &sfxHit, &sfxResist, &sfxBurst, &sfxPick,
                        &sfxHurt, &sfxBossDie, &sfxBeam, &sfxNova};
        for (Sound *s : all) UnloadSound(*s);
        audioOk = false;
    }
    CloseAudioDevice();
}

void Game::play(Sound &s, float vol) {
    if (!audioOk || smoke) return;
    SetSoundVolume(s, vol);
    PlaySound(s);
}

void Game::say(Vector2 pos, const char *text, Color c) {
    FloatText f;
    f.pos = pos;
    f.text = text;
    f.color = c;
    f.life = 1.6f;
    ftexts.push_back(f);
}

// ---------- spawning ----------

Vector2 Game::randomEdgePos() {
    int side = irand(0, 3);
    float x = frand(ARENA_MARGIN + 30, SCREEN_W - ARENA_MARGIN - 30);
    float y = frand(ARENA_MARGIN + 30, SCREEN_H - ARENA_MARGIN - 30);
    if (side == 0) y = ARENA_MARGIN + 30;
    else if (side == 1) y = SCREEN_H - ARENA_MARGIN - 30;
    else if (side == 2) x = ARENA_MARGIN + 30;
    else x = SCREEN_W - ARENA_MARGIN - 30;
    return {x, y};
}

// Everything that depends on species. Also used when an enemy changes species
// mid-life (TB taking over a macrophage).
void Game::becomeType(Enemy &e, OrgId t) {
    const OrgDef &d = ORGS[t];
    e.type = t;
    e.hp = e.maxHp = d.hp;
    e.radius = d.radius;
    e.hostile = !(d.traits & T_HOST) || (t == O_GRANULOMA_SHIELD);
    e.capsuleIntact = (d.traits & T_CAPSULE) != 0;
    e.capsuleHp = e.capsuleIntact ? d.hp * 0.5f : 0;
    e.resistTo = 0;
    e.trail.clear();

    // A strain that has evolved resistance keeps it as long as pressure lasts.
    if (!(d.traits & (T_HOST | T_INERT | T_BOSS)))
        for (int w = 0; w < W_COUNT; w++)
            if (pressure[w] >= 100.0f) e.resistTo |= (1 << w);

    seenOrg[t] = true;
}

Enemy &Game::spawnEnemy(OrgId t, Vector2 pos) {
    Enemy e;
    e.id = nextId++;
    e.pos = pos;
    e.seed = irand(0, 1000);
    becomeType(e, t);

    switch (t) {
        case O_RBC:          e.vel = vpolar(frand(0, 2 * PI), frand(8, 20)); break;
        case O_INFECTED_RBC: e.t1 = 6.0f; break;
        case O_ROTA:         e.vel = vpolar(frand(0, 2 * PI), ORGS[t].speed); break;
        case O_TB_LATENT:    e.t1 = 9.0f; break;
        case O_CDIFF_SPORE:  e.t1 = 11.0f; break;
        case O_SCHISTO_EGG:  e.t1 = 13.0f; e.t2 = 2.0f; break;
        case O_ASPERGILLUS:  e.t1 = 6.0f; e.angle = frand(0, 2 * PI); break;
        case O_VIBRIO:       e.t1 = frand(0.5f, 2.0f); break;
        case O_CANDIDA:      e.t1 = BUD_PERIOD; break;
        case O_ENTAMOEBA:    e.t1 = frand(1.0f, 3.0f); break;
        default: break;
    }
    newborns.push_back(e);
    return newborns.back();
}

// Spawns are staged and merged at frame boundaries, so code can spawn while
// iterating the enemy list.
void Game::flushSpawns() {
    if (newborns.empty()) return;
    enemies.insert(enemies.end(), newborns.begin(), newborns.end());
    newborns.clear();
}

void Game::queueSpawn(OrgId t, Vector2 pos) {
    PendingSpawn ps;
    ps.type = t;
    ps.pos = pos;
    ps.timer = 0.8f;
    pending.push_back(ps);
}

Enemy *Game::findEnemy(int id) {
    if (id < 0) return nullptr;
    for (auto &e : enemies)
        if (e.id == id && e.alive) return &e;
    for (auto &e : newborns)
        if (e.id == id && e.alive) return &e;
    return nullptr;
}

// ---------- run flow ----------

void Game::resetWorld() {
    enemies.clear();
    newborns.clear();
    projs.clear();
    parts.clear();
    shocks.clear();
    fields.clear();
    ftexts.clear();
    pending.clear();
    obstacles.clear();
    beamOn = beamLock = false;
    beamHeat = beamTick = 0;
    pendingDeath = false;
    wrongWeaponT = 0;
}

void Game::startRun() {
    biome = std::max(0, std::min(2, startBiome));
    roomInBiome = 0;
    runKills = 0;
    php = phpMax = 100;
    o2 = o2Max = 100;
    hydration = 100;
    weapon = W_LYSOZYME;
    resistantStrains = 0;
    unlockBannerW = -1;
    unlockBannerT = 0;
    for (int i = 0; i < U_COUNT; i++) haveUpg[i] = false;
    for (int i = 0; i < W_COUNT; i++) pressure[i] = 0;
    refreshUnlocks(false);
    // Check the SEEN bit, not `trained == 0`: weapon bits are set mid-tutorial.
    inTutorial = (!(meta.trained & TRAINED_SEEN) && !skipTutorial && biome == 0);
    if (inTutorial) loadTutorial();
    else loadRoom();
    state = ST_PLAY;
}

void Game::loadRoom() {
    resetWorld();
    wave = 0;
    waveGap = 0;
    if (inTutorial) {   // training must not cost hull or breed resistance
        php = phpMax;
        runKills = 0;
        resistantStrains = 0;
        for (int i = 0; i < W_COUNT; i++) pressure[i] = 0;
    }
    inTutorial = false;
    ppos = {SCREEN_W / 2.0f, SCREEN_H / 2.0f};
    reticle = ppos;
    pvel = {0, 0};
    invuln = 1.2f;
    bool bossRoom = (roomInBiome == 3);
    wavesInRoom = bossRoom ? 1 : 3;
    flowDir = (biome == 0) ? vpolar(frand(0, 2 * PI), 1.0f) : Vector2{0, 0};

    if (!bossRoom) {
        for (int i = 0, n = irand(2, 3); i < n; i++) {
            Obstacle o;
            for (int tries = 0; tries < 20; tries++) {
                o.pos = {frand(ARENA_MARGIN + 120, SCREEN_W - ARENA_MARGIN - 120),
                         frand(ARENA_MARGIN + 120, SCREEN_H - ARENA_MARGIN - 120)};
                o.radius = frand(45, 70);
                if (vdist(o.pos, ppos) > 200) break;
            }
            obstacles.push_back(o);
        }
    }

    // host cells and allies
    if (biome == 0) {
        for (int i = 0; i < 10; i++) {
            Vector2 p = {frand(ARENA_MARGIN + 60, SCREEN_W - ARENA_MARGIN - 60),
                         frand(ARENA_MARGIN + 60, SCREEN_H - ARENA_MARGIN - 60)};
            if (vdist(p, ppos) > 120) spawnEnemy(O_RBC, p);
        }
        if (roomInBiome >= 1) spawnEnemy(O_NEUTROPHIL, vadd(ppos, vpolar(frand(0, 2 * PI), 90)));
    } else if (biome == 1) {
        for (int i = 0; i < 4; i++) {
            float x = ARENA_MARGIN + 110 + i * (SCREEN_W - 2 * ARENA_MARGIN - 220) / 3.0f;
            spawnEnemy(O_EPITHELIAL, {x, ARENA_MARGIN + 55});
            spawnEnemy(O_EPITHELIAL, {x, SCREEN_H - ARENA_MARGIN - 55});
        }
        spawnEnemy(O_MACROPHAGE, vadd(ppos, vpolar(frand(0, 2 * PI), 100)));
    } else {
        spawnEnemy(O_NEUTROPHIL, vadd(ppos, vpolar(frand(0, 2 * PI), 90)));
    }

    if (bossRoom) {
        OrgId b = (biome == 0) ? O_BOSS_SCHISTO : (biome == 1) ? O_BOSS_GRANULOMA : O_BOSS_ASCARIS;
        Enemy &boss = spawnEnemy(b, {SCREEN_W / 2.0f, ARENA_MARGIN + 150});
        if (b == O_BOSS_GRANULOMA) {
            boss.pos = {SCREEN_W / 2.0f, SCREEN_H / 2.0f};
            int bossId = boss.id;
            Vector2 bp = boss.pos;
            for (int i = 0; i < 6; i++) {
                Enemy &s = spawnEnemy(O_GRANULOMA_SHIELD, bp);
                s.parentId = bossId;
                s.t1 = i * (2 * PI / 6.0f);
            }
        }
    } else spawnWaveNow();
    flushSpawns();
}

void Game::spawnWaveNow() {
    int r = roomInBiome, w = wave;
    auto add = [&](OrgId t, int n) {
        for (int i = 0; i < n; i++) queueSpawn(t, randomEdgePos());
    };
    if (biome == 0) {
        add(O_MEROZOITE, 3 + r + w);
        add(O_STAPH, 2 + (r + w) / 2);
        if (r >= 1 || w >= 1) add(O_ECOLI, 2 + (r + w) / 2);
        if (r >= 1 && w >= 1) add(O_TRYPANOSOME, 1 + (r + w) / 3);
        if (r >= 2) add(O_MENINGO, 1 + w / 2);
        if (r >= 2 && w >= 1) add(O_CANDIDA, 1);
    } else if (biome == 1) {
        add(O_FLU, 3 + r + w);
        add(O_PNEUMO, 2 + (r + w) / 2);
        if (r >= 1) add(O_TB, 1 + (r + w) / 3);
        if (r >= 1 && w >= 1) add(O_KLEBSIELLA, 1 + w / 2);
        if (r >= 2) add(O_ASPERGILLUS, 1 + w / 2);
    } else {
        add(O_VIBRIO, 3 + r + w);
        add(O_ROTA, 2 + (r + w) / 2);
        add(O_CDIFF, 2 + (r + w) / 2);
        if (r >= 1) add(O_ENTAMOEBA, 1 + (r + w) / 3);
        if (r >= 2) add(O_ASCARIS_LARVA, 2 + w);
    }
}

// ---------- tutorial ----------

void Game::loadTutorial() {
    resetWorld();
    biome = 0;
    roomInBiome = 0;
    wave = 0;
    inTutorial = true;
    tutStage = 0;
    tutTimer = 0;
    tutMoved = 0;
    tutStageFresh = true;
    flowDir = {0, 0};
    ppos = {SCREEN_W / 2.0f, SCREEN_H / 2.0f};
    reticle = ppos;
    pvel = {0, 0};
    php = phpMax = 100;
    invuln = 2.0f;
}

// Each stage teaches one idea and advances only when it is done.
void Game::updateTutorial(float dt) {
    tutTimer += dt;
    const bool entered = tutStageFresh;
    tutStageFresh = false;
    int hostiles = 0;
    for (auto &e : enemies)
        if (e.alive && (e.hostile || (ORGS[e.type].traits & T_INERT))) hostiles++;

    auto advance = [&](int next) {
        tutStage = next;
        tutTimer = 0;
        tutStageFresh = true;
    };

    switch (tutStage) {
        case 0:
            tutMoved += vlen(pvel) * dt;
            if (tutMoved > 500) advance(1);
            break;
        case 1:
            if (entered)
                for (int i = 0; i < 3; i++)
                    spawnEnemy(O_DEBRIS, {SCREEN_W / 2.0f - 200 + i * 200.0f, 200});
            if (hostiles == 0 && tutTimer > 0.5f) advance(2);
            break;
        case 2:   // gram-positive: lysozyme works
            if (entered)
                for (int i = 0; i < 3; i++)
                    spawnEnemy(O_STAPH, {SCREEN_W / 2.0f - 220 + i * 220.0f, 170});
            if (hostiles == 0 && tutTimer > 0.5f) advance(3);
            break;
        case 3:   // gram-negative: lysozyme fails, the beam works
            if (entered) {
                meta.trained |= (1 << W_COMPLEMENT);   // issued when taught
                refreshUnlocks(true);
                for (int i = 0; i < 3; i++)
                    spawnEnemy(O_ECOLI, {SCREEN_W / 2.0f - 220 + i * 220.0f, 170});
            }
            if (hostiles == 0 && tutTimer > 0.5f) advance(4);
            break;
        case 4:   // capsule: strip it with an IgG cloud first
            if (entered) {
                meta.trained |= (1 << W_IGG);
                refreshUnlocks(true);
                for (int i = 0; i < 2; i++)
                    spawnEnemy(O_PNEUMO, {SCREEN_W / 2.0f - 130 + i * 260.0f, 180});
            }
            if (hostiles == 0 && tutTimer > 0.5f) advance(5);
            break;
        case 5:
            if (entered) {
                meta.trained |= (1 << W_COMPLEMENT) | (1 << W_IGG) | TRAINED_SEEN;
                saveMeta();
            }
            // loadRoom() clears inTutorial and keys its reset off it; do not clear it first.
            if (tutTimer > 2.6f) loadRoom();
            break;
        default: break;
    }
}

// ---------- upgrades ----------

void Game::pickUpgradeChoices() {
    std::vector<int> pool;
    for (int i = 0; i < U_COUNT; i++)
        if (!haveUpg[i] && i != U_ALBUMIN) pool.push_back(i);
    std::shuffle(pool.begin(), pool.end(), rng);

    // Always offer the entering biome's key drug (e.g. isoniazid for TB).
    const int keyDrug[3] = {U_ARTEMISININ, U_ISONIAZID, U_ORS};
    int key = keyDrug[std::max(0, std::min(2, biome))];
    if (!haveUpg[key]) {
        pool.erase(std::remove(pool.begin(), pool.end(), key), pool.end());
        pool.insert(pool.begin(), key);
    }
    pool.push_back(U_ALBUMIN);
    for (int i = 0; i < 3; i++) upChoice[i] = pool[std::min((size_t)i, pool.size() - 1)];
}

void Game::applyUpgrade(int u) {
    if (u != U_ALBUMIN) haveUpg[u] = true;
    if (u == U_ORS) hydration = 100;
    if (u == U_EPO) { o2Max = 100; o2 = 100; }
    if (u == U_ALBUMIN) { phpMax += 15; php = std::min(phpMax, php + 40); }
    play(sfxPick);
}

void Game::onRoomCleared() {
    if (roomInBiome == 3) {
        // >= so an out-of-range biome still ends the run.
        if (biome >= 2) {
            meta.victories++;
            bankRun();
            state = ST_VICTORY;
            return;
        }
        biome++;
        roomInBiome = 0;
    } else roomInBiome++;
    pickUpgradeChoices();
    state = ST_UPGRADE;
}

// ---------- weapon unlocks ----------

// Includes the run in flight, so unlocks can fire mid-run.
int Game::careerKills() const { return meta.kills + runKills; }

bool Game::unlockProgress(int w, int &have, int &need) const {
    const WeaponUnlock &u = UNLOCKS[w];
    if (u.bosses > 0) { have = meta.bosses; need = u.bosses; return true; }
    if (u.kills > 0) { have = careerKills(); need = u.kills; return true; }
    return false;
}

void Game::refreshUnlocks(bool announce) {
    for (int w = 0; w < W_COUNT; w++) {
        const WeaponUnlock &u = UNLOCKS[w];
        bool ok = (!u.kills && !u.bosses && !u.viaTutorial)
               || (u.viaTutorial && (meta.trained & (1 << w)))
               || (u.kills && careerKills() >= u.kills)
               || (u.bosses && meta.bosses >= u.bosses);
        if (ok && !weaponUnlocked[w] && announce) {
            unlockBannerW = w;
            unlockBannerT = 4.5f;
            play(sfxPick, 1.0f);
        }
        weaponUnlocked[w] = ok;
    }
    if (!weaponUnlocked[weapon])
        for (int w = 0; w < W_COUNT; w++)
            if (weaponUnlocked[w]) { weapon = (WeaponId)w; break; }
}

// Selecting a locked weapon reports what it needs rather than doing nothing.
void Game::selectWeapon(int w) {
    if (w < 0 || w >= W_COUNT) return;
    if (weaponUnlocked[w]) { weapon = (WeaponId)w; return; }
    int have = 0, need = 0;
    if (unlockProgress(w, have, need))
        say(vadd(ppos, {0, -36}),
            TextFormat("%s LOCKED - %s (%d/%d)", WEAPONS[w].shortName, UNLOCKS[w].how, have, need),
            Color{255, 160, 110, 255});
    else
        say(vadd(ppos, {0, -36}), TextFormat("%s LOCKED - %s", WEAPONS[w].shortName, UNLOCKS[w].how),
            Color{255, 160, 110, 255});
    play(sfxResist, 0.7f);
}

void Game::cycleWeapon(int dir) {
    for (int i = 1; i <= W_COUNT; i++) {
        int cand = ((weapon + dir * i) % W_COUNT + W_COUNT) % W_COUNT;
        if (weaponUnlocked[cand]) { weapon = (WeaponId)cand; return; }
    }
}

// ---------- headless smoke run ----------

// No window, input or audio: the autopilot plays the game.
int Game::runSmoke() {
    skipTutorial = !forceTutorial;
    startRun();

    int unlockSeen = 0;
    auto reportUnlocks = [&](int f) {
        int now = 0;
        for (int w = 0; w < W_COUNT; w++)
            if (weaponUnlocked[w]) now |= 1 << w;
        for (int w = 0; w < W_COUNT; w++)
            if ((now & ~unlockSeen) & (1 << w))
                printf("  f%-6d UNLOCKED %-11s (career kills=%d bosses=%d)\n", f,
                       WEAPONS[w].shortName, careerKills(), meta.bosses);
        unlockSeen = now;
    };
    reportUnlocks(0);

    const float dt = 1.0f / 60.0f;

    if (inTutorial) {
        int last = -1;
        for (int f = 0; f < 7200 && inTutorial; f++) {
            if (tutStage != last) {
                last = tutStage;
                printf("  tutorial stage %d reached at f%d\n", tutStage, f);
            }
            update(dt);
            reportUnlocks(f);
        }
        printf("  tutorial %s (stage %d)\n", inTutorial ? "HUNG" : "completed", tutStage);
        if (inTutorial) return 1;
    }

    int frames = 0, deaths = 0, lastB = -1, lastR = -1;
    int sinceRoomChange = 0, stalls = 0;
    for (; frames < smokeFrames && state != ST_VICTORY; frames++) {
        // A room that never clears is a soft-lock.
        if (++sinceRoomChange > 9000) {
            stalls++;
            printf("  f%-6d STALL: %s %s has not cleared in 150s (%zu enemies alive)\n", frames,
                   biomeName(biome), roomInBiome == 3 ? "boss" : "site", enemies.size());
            for (auto &e : enemies)
                if (e.alive && e.hostile) printf("           stuck: %s\n", ORGS[e.type].name);
            break;
        }
        if (biome != lastB || roomInBiome != lastR) {
            lastB = biome;
            lastR = roomInBiome;
            sinceRoomChange = 0;
            printf("  f%-6d %-11s %-6s hull=%3d o2max=%3d kills=%d resistant=%d\n", frames,
                   biomeName(biome), roomInBiome == 3 ? "BOSS" : "site", (int)php, (int)o2Max,
                   runKills, resistantStrains);
        }
        reportUnlocks(frames);
        if (state == ST_PLAY) update(dt);
        else if (state == ST_UPGRADE) {
            applyUpgrade(upChoice[0]);
            loadRoom();
            state = ST_PLAY;
        } else {
            deaths++;
            printf("  f%-6d DIED in %s %s\n", frames, biomeName(biome),
                   roomInBiome == 3 ? "boss fight" : TextFormat("site %d", roomInBiome + 1));
            gtime += dt;
            startRun();
            lastB = lastR = -1;
        }
    }

    int cataloged = 0;
    for (int i = 0; i < O_COUNT; i++) if (seenOrg[i]) cataloged++;
    printf("deaths=%d bosses=%d resistant=%d codex=%d/%d stalls=%d\n", deaths, meta.bosses,
           resistantStrains, cataloged, O_COUNT, stalls);
    printf("SMOKE %s frames=%d state=%d biome=%d room=%d hull=%d career=%d unlocked=%d/%d\n",
           stalls ? "FAIL" : "OK", frames, (int)state, biome, roomInBiome, (int)php,
           careerKills(), (int)std::bitset<32>(unlockSeen).count(), W_COUNT);
    return stalls ? 1 : 0;
}

// ---------- main loop ----------

int Game::run(bool smokeMode) {
    smoke = smokeMode;
    if (seed) rng.seed(seed);
    startBiome = std::max(0, std::min(2, startBiome));
    loadMeta();
    if (smoke) return runSmoke();

    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(SCREEN_W, SCREEN_H, "PATHOGEN PROTOCOL");
    SetTargetFPS(60);
    initAudio();

    while (!WindowShouldClose()) {
        float dt = std::min(GetFrameTime(), 1.0f / 20.0f);

        if (IsKeyPressed(KEY_TAB)) {
            if (state == ST_CODEX) state = codexReturn;
            else { codexReturn = state; state = ST_CODEX; codexPage = 0; }
        }

        switch (state) {
            case ST_TITLE:
                gtime += dt;
                if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) startRun();
                break;
            case ST_PLAY:
                if (inTutorial && IsKeyPressed(KEY_K)) {
                    // Seen, but skipping forfeits any weapons the stages already issued.
                    meta.trained = TRAINED_SEEN;
                    saveMeta();
                    loadRoom();
                }
                update(dt);
                break;
            case ST_CODEX: {
                gtime += dt;
                // Clamped here, not in draw(), so the page number is real state.
                int seenCount = 0;
                for (int i = 0; i < O_COUNT; i++)
                    if (seenOrg[i] && i != O_DEBRIS) seenCount++;
                int pages = std::max(1, (seenCount + 2) / 3);
                if (IsKeyPressed(KEY_RIGHT)) codexPage++;
                if (IsKeyPressed(KEY_LEFT)) codexPage--;
                codexPage = std::max(0, std::min(codexPage, pages - 1));
                break;
            }
            case ST_UPGRADE: {
                gtime += dt;
                int picked = -1;
                for (int i = 0; i < 3; i++) {
                    if (IsKeyPressed(KEY_ONE + i)) picked = i;
                    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                        CheckCollisionPointRec(GetMousePosition(), upgradeCardRect(i))) picked = i;
                }
                if (picked >= 0) {
                    applyUpgrade(upChoice[picked]);
                    loadRoom();
                    state = ST_PLAY;
                }
                break;
            }
            case ST_GAMEOVER:
            case ST_VICTORY:
                gtime += dt;
                if (IsKeyPressed(KEY_ENTER)) state = ST_TITLE;
                break;
        }
        draw();
    }

    saveMeta();
    shutdownAudio();
    CloseWindow();
    return 0;
}
