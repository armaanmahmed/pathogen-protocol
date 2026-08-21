#include "game.h"
#include "vecmath.h"
#include <algorithm>

// ============================================================================
// Simulation: input, weapons, enemy behavior, damage. Draws nothing, so it
// runs headless under --smoke.
// ============================================================================

// ---------- shared queries (also used by the HUD) ----------

const Enemy *Game::nearestHostile(Vector2 from, float *distOut) const {
    const Enemy *best = nullptr;
    float bd = 1e9f;
    for (const auto &e : enemies) {
        if (!e.alive || !e.hostile) continue;
        float dd = vdist(e.pos, from);
        if (dd < bd) { bd = dd; best = &e; }
    }
    if (distOut) *distOut = best ? bd : 1e9f;
    return best;
}

// The single damage-multiplier chain; applyDamage() and the HUD both use it.
float Game::ratedAgainst(int w, const Enemy &e, const char **reason) const {
    const int traits = ORGS[e.type].traits;
    const char *why = nullptr;
    float m = effectiveness(w, traits, &why);
    if (e.capsuleIntact && w != W_IGG) { m *= 0.35f; why = "slime shell blocks it"; }
    if ((traits & T_ACIDFAST) && !hasUpg(U_ISONIAZID)) { m *= 0.45f; why = "waxy armor"; }
    if (e.resistTo & (1 << w)) { m *= 0.30f; why = "resistant strain"; }
    if (e.opsonized && w != W_IGG) m *= 1.6f;
    if ((e.type == O_MEROZOITE || e.type == O_INFECTED_RBC) && hasUpg(U_ARTEMISININ)) m *= 2.0f;
    if ((traits & T_GRAMPOS) && hasUpg(U_VANCOMYCIN)) m *= 1.8f;
    if ((traits & T_VIRUS) && hasUpg(U_INTERFERON)) m *= 1.4f;
    if (e.type == O_TRYPANOSOME && fmodf(e.t1, 4.0f) > 3.0f) { m *= 0.12f; why = "shifted its disguise"; }
    if (reason) *reason = why;
    return m;
}

int Game::bestWeaponAgainst(const Enemy &e) const {
    int best = weapon;
    float bm = -1;
    for (int w = 0; w < W_COUNT; w++) {
        if (!weaponUnlocked[w]) continue;
        // With a shell up, stripping it comes first.
        float m = (e.capsuleIntact && w == W_IGG) ? 3.0f : ratedAgainst(w, e);
        if (m > bm) { bm = m; best = w; }
    }
    return best;
}

// Rack meter: heat for the beam, resistance pressure for everything else.
float Game::weaponCharge(int w) const {
    if (WEAPONS[w].kind == WK_BEAM) return clampf(beamHeat, 0.0f, 1.0f);
    return clampf(pressure[w] / 100.0f, 0.0f, 1.0f);
}

// ---------- effects ----------

void Game::burstParticles(Vector2 pos, Color c, int n, float speed) {
    for (int i = 0; i < n; i++) {
        Particle p;
        p.pos = pos;
        p.vel = vpolar(frand(0, 2 * PI), frand(speed * 0.3f, speed));
        p.life = p.maxLife = frand(0.3f, 0.8f);
        p.radius = frand(2, 5);
        p.color = c;
        parts.push_back(p);
    }
}

void Game::addShock(Vector2 pos, float r0, float r1, Color c, float life, float thick) {
    Shock s;
    s.pos = pos;
    s.r0 = r0;
    s.r1 = r1;
    s.life = s.maxLife = life;
    s.thick = thick;
    s.color = c;
    shocks.push_back(s);
}

void Game::hemolysis(Vector2 pos, bool fullBurst) {
    o2Max = std::max(40.0f, o2Max - (fullBurst ? 3.0f : 1.0f));
    o2 = std::min(o2, o2Max);
    burstParticles(pos, Color{220, 40, 50, 255}, fullBurst ? 26 : 10, fullBurst ? 220 : 120);
    if (fullBurst) {
        if (vdist(pos, ppos) < 100) hurtPlayer(12);
        shake = std::max(shake, 5.0f);
        play(sfxBurst, 0.8f);
        say(pos, "BLOOD CELL BURST - oxygen lost", Color{255, 120, 120, 255});
    }
}

void Game::hurtPlayer(float dmg) {
    if (invuln > 0) return;
    php -= dmg;
    invuln = IFRAME_ON_HIT;
    shake = std::max(shake, 6.0f);
    play(sfxHurt, 0.8f);
    burstParticles(ppos, Color{120, 230, 220, 255}, 10, 160);
    // Deferred: the rest of the frame still has to run (see pendingDeath).
    if (php <= 0) { php = 0; pendingDeath = true; }
}

// ---------- damage ----------

// Overusing one weapon selects for resistant strains.
void Game::evolveResistance(int weaponUsed) {
    pressure[weaponUsed] = std::min(140.0f, pressure[weaponUsed] + 7.0f);
    if (pressure[weaponUsed] >= 100.0f && pressure[weaponUsed] - 7.0f < 100.0f) {
        resistantStrains++;
        say({SCREEN_W / 2.0f, 150},
            TextFormat("RESISTANCE! New germs now shrug off %s", WEAPONS[weaponUsed].shortName),
            Color{255, 170, 80, 255});
        play(sfxResist, 0.9f);
    }
}

void Game::applyDamage(Enemy &e, float dmg, int weaponUsed) {
    if (!e.alive || e.submerged) return;
    const OrgDef &d = ORGS[e.type];

    // IgG strips shells and marks targets rather than killing.
    if (weaponUsed == W_IGG) {
        if (e.capsuleIntact) {
            e.capsuleHp -= dmg * 3.0f;
            if (e.capsuleHp <= 0) {
                e.capsuleIntact = false;
                burstParticles(e.pos, Color{200, 220, 255, 255}, 10, 150);
                if (e.feedbackCd <= 0) {
                    say(e.pos, "SHELL STRIPPED", Color{255, 240, 130, 255});
                    e.feedbackCd = 1.0f;
                }
            }
        }
        if (!e.opsonized && e.feedbackCd <= 0 && !e.capsuleIntact) {
            say(e.pos, "TAGGED +60%", Color{255, 240, 130, 255});
            e.feedbackCd = 1.2f;
        }
        e.opsonized = true;
        e.opsonT = 6.0f;
    }

    // After the IgG block on purpose: a shell stripped by this hit no longer counts.
    const char *reason = nullptr;
    float mul = ratedAgainst(weaponUsed, e, &reason);

    // Say why a shot barely worked.
    if (e.feedbackCd <= 0 && reason && mul <= 0.6f) {
        say(vadd(e.pos, {0, -e.radius - 8}), TextFormat("BLOCKED: %s", reason), Color{255, 150, 110, 255});
        e.feedbackCd = 1.4f;
        play(sfxResist, 0.4f);
    } else if (e.feedbackCd <= 0 && mul >= 1.8f) {
        say(vadd(e.pos, {0, -e.radius - 8}), reason ? reason : "EFFECTIVE", Color{160, 255, 170, 255});
        e.feedbackCd = 1.6f;
    }

    e.hp -= dmg * mul;
    if (hitSfxCd <= 0) {
        play(sfxHit, 0.28f);
        hitSfxCd = 0.045f;
    }
    if (e.hp <= 0) {
        if (!(d.traits & (T_HOST | T_INERT | T_BOSS))) evolveResistance(weaponUsed);
        killEnemy(e);
    }
}

// Area damage with mild falloff. `sparesHost` skips friendly host cells.
void Game::splashDamage(Vector2 pos, float radius, float dmg, int weaponUsed, bool sparesHost) {
    for (auto &e : enemies) {
        if (!e.alive || e.submerged) continue;
        if (sparesHost && (ORGS[e.type].traits & T_HOST) && !e.hostile) continue;
        float d = vdist(e.pos, pos) - e.radius;
        if (d > radius) continue;
        float f = 1.0f - clampf(d / radius, 0.0f, 1.0f) * 0.45f;
        applyDamage(e, dmg * f, weaponUsed);
    }
}

void Game::killEnemy(Enemy &e) {
    if (!e.alive) return;
    e.alive = false;
    const OrgDef &d = ORGS[e.type];

    switch (e.type) {
        case O_RBC: hemolysis(e.pos, false); return;
        case O_EPITHELIAL:
        case O_MACROPHAGE:
        case O_NEUTROPHIL:
            burstParticles(e.pos, Color{240, 190, 190, 255}, 12, 150);
            return;
        case O_DEBRIS:
            burstParticles(e.pos, Color{180, 180, 190, 255}, 10, 140);
            return;
        case O_INFECTED_RBC: hemolysis(e.pos, false); break;
        case O_TB:
            if (frand(0, 1) < 0.30f) {
                spawnEnemy(O_TB_LATENT, e.pos);
                say(e.pos, "WENT DORMANT", Color{255, 200, 120, 255});
            }
            break;
        case O_CDIFF:
            // One recurrence only: t3 carries the generation.
            if (e.t3 < 1.0f) {
                Enemy &sp = spawnEnemy(O_CDIFF_SPORE, e.pos);
                sp.t3 = e.t3 + 1.0f;
                say(e.pos, "LEFT A SPORE", Color{230, 210, 160, 255});
            }
            break;
        case O_BOSS_SCHISTO:
        case O_BOSS_GRANULOMA:
        case O_BOSS_ASCARIS: {
            meta.bosses++;
            saveMeta();
            play(sfxBossDie);
            shake = 14.0f;
            addShock(e.pos, 20, 340, Color{255, 220, 120, 255}, 0.8f, 10);
            burstParticles(e.pos, Color{255, 220, 120, 255}, 60, 320);
            int bid = e.id;
            for (auto &o : enemies)
                if (o.alive && (o.parentId == bid || o.type == O_SCHISTO_EGG ||
                                o.type == O_ASCARIS_LARVA || o.type == O_GRANULOMA_SHIELD))
                    o.alive = false;
            break;
        }
        default: break;
    }
    // Host-cell deaths returned above and never score; hostile shield cells do.
    runKills++;
    burstParticles(e.pos, d.color, 12, 180);
}

// ---------- movement helpers ----------

void Game::clampToArena(Vector2 &p, float r) const {
    p.x = std::max(ARENA_MARGIN + r, std::min(SCREEN_W - ARENA_MARGIN - r, p.x));
    p.y = std::max(ARENA_MARGIN + r, std::min(SCREEN_H - ARENA_MARGIN - r, p.y));
}

void Game::pushOutObstacles(Vector2 &p, float r) const {
    for (auto &o : obstacles) {
        float dd = vdist(p, o.pos);
        float minD = o.radius + r;
        if (dd < minD && dd > 0.001f) p = vadd(o.pos, vscale(vnorm(vsub(p, o.pos)), minD));
    }
}

// ---------- weapons ----------

void Game::updateWeapon(float dt, Vector2 aimPos, bool firing) {
    const WeaponDef &w = WEAPONS[weapon];
    fireCd = std::max(0.0f, fireCd - dt);
    beamOn = false;

    if (w.kind == WK_BEAM) {
        if (firing && !beamLock) {
            fireBeam(dt, aimPos);
            beamHeat = std::min(1.0f, beamHeat + dt / 2.4f);
            if (beamHeat >= 1.0f) {
                beamLock = true;
                beamOn = false;
                say(vadd(ppos, {0, -34}), "OVERHEATED - let it cool", Color{255, 170, 90, 255});
                play(sfxResist, 0.8f);
            }
        }
    }

    if (!beamOn) {
        beamHeat = std::max(0.0f, beamHeat - dt / 1.7f);
        if (beamLock && beamHeat <= 0.15f) beamLock = false;
    }

    if (w.kind != WK_BEAM && firing && fireCd <= 0) {
        fireCd = w.cooldown * (hasUpg(U_PYROGEN) ? 0.82f : 1.0f);
        if (w.kind == WK_NOVA) fireNova();
        else fireShot(aimPos);
    }
}

// Continuous piercing line, limited by heat.
void Game::fireBeam(float dt, Vector2 aimPos) {
    const WeaponDef &w = WEAPONS[W_COMPLEMENT];
    beamOn = true;

    Vector2 dir = vnorm(vsub(aimPos, ppos));
    if (vlen(dir) < 0.5f) dir = vpolar(aimAngle, 1);
    Vector2 start = vadd(ppos, vscale(dir, PLAYER_RADIUS + 4));

    // Stop the beam at the arena wall.
    float tmax = w.speed;
    if (dir.x > 1e-5f) tmax = std::min(tmax, (SCREEN_W - ARENA_MARGIN - start.x) / dir.x);
    if (dir.x < -1e-5f) tmax = std::min(tmax, (ARENA_MARGIN - start.x) / dir.x);
    if (dir.y > 1e-5f) tmax = std::min(tmax, (SCREEN_H - ARENA_MARGIN - start.y) / dir.y);
    if (dir.y < -1e-5f) tmax = std::min(tmax, (ARENA_MARGIN - start.y) / dir.y);
    beamEnd = vadd(start, vscale(dir, std::max(0.0f, tmax)));

    // Damage in discrete ticks, so feedback and resistance accounting are
    // framerate-independent.
    beamTick += dt;
    if (beamTick >= 0.05f) {
        float tickDmg = w.dmg * beamTick;
        beamTick = 0;
        for (auto &e : enemies) {
            if (!e.alive || e.submerged) continue;
            if (distToSeg(e.pos, start, beamEnd) < e.radius + w.area)
                applyDamage(e, tickDmg, W_COMPLEMENT);
        }
    }
    if (fmodf((float)gtime, 0.14f) < dt) play(sfxBeam, 0.16f);
}

// Unaimed blast centered on the player.
void Game::fireNova() {
    const WeaponDef &w = WEAPONS[W_OXBURST];
    addShock(ppos, 12, w.area, Color{255, 205, 120, 255}, 0.36f, 9);
    burstParticles(ppos, Color{255, 228, 155, 255}, 26, 330);
    splashDamage(ppos, w.area, w.dmg, W_OXBURST, true);
    shake = std::max(shake, 5.0f);
    pvel = vscale(pvel, 0.55f);
    play(sfxNova, 0.75f);
}

void Game::fireShot(Vector2 aimPos) {
    const WeaponDef &w = WEAPONS[weapon];
    float base = atan2f(aimPos.y - ppos.y, aimPos.x - ppos.x);
    float spread = w.spreadDeg * PI / 180.0f;

    for (int i = 0; i < w.pellets; i++) {
        // Fan multi-shot weapons evenly, with slight jitter.
        float fan = (w.pellets > 1) ? ((float)i / (w.pellets - 1) - 0.5f) * 2.0f * spread : 0.0f;
        float a = base + fan + frand(-spread * 0.3f, spread * 0.3f);
        Proj p;
        p.pos = vadd(ppos, vpolar(a, PLAYER_RADIUS + 4));
        p.vel = vpolar(a, w.speed);
        p.dmg = w.dmg;
        p.life = w.life;
        p.pierce = w.pierce;
        p.weapon = weapon;
        switch (w.kind) {
            case WK_LOB:    p.lob = true; p.radius = 9; break;
            case WK_SEEKER: p.homing = 5.2f; p.radius = 5; break;
            default:        p.radius = 4; break;
        }
        projs.push_back(p);
    }
    play(sfxShoot, w.kind == WK_LOB ? 0.34f : 0.26f);
    pvel = vsub(pvel, vpolar(base, w.kind == WK_LOB ? 42.0f : 16.0f));
}

// ---------- player ----------

void Game::updatePlayer(float dt, Vector2 moveDir, Vector2 aimPos, bool firing, bool dashPressed) {
    float speed = PLAYER_SPEED;
    if (hasUpg(U_ADRENALINE)) speed *= 1.12f;
    if (o2 < 25) speed *= 0.78f;
    if (biome == 2 && hydration < 40) speed *= 0.82f;

    Vector2 target = vscale(vnorm(moveDir), speed);
    float k = std::min(1.0f, PLAYER_ACCEL * dt);
    pvel.x += (target.x - pvel.x) * k;
    pvel.y += (target.y - pvel.y) * k;

    if (dashPressed && dashCd <= 0 && o2 > 15 && vlen(moveDir) > 0.1f) {
        pvel = vscale(vnorm(moveDir), DASH_SPEED);
        dashCd = hasUpg(U_ADRENALINE) ? 0.6f : 0.85f;
        dashT = 0.18f;
        invuln = std::max(invuln, 0.28f);
        o2 -= DASH_O2_COST;
        burstParticles(ppos, Color{120, 230, 220, 200}, 8, 120);
    }

    ppos = vadd(ppos, vscale(pvel, dt));
    if (biome == 0 && !inTutorial) ppos = vadd(ppos, vscale(flowDir, 20.0f * dt));
    clampToArena(ppos, PLAYER_RADIUS);
    pushOutObstacles(ppos, PLAYER_RADIUS);

    aimAngle = atan2f(aimPos.y - ppos.y, aimPos.x - ppos.x);
    dashCd = std::max(0.0f, dashCd - dt);
    dashT = std::max(0.0f, dashT - dt);
    invuln = std::max(0.0f, invuln - dt);
    updateWeapon(dt, aimPos, firing);

    float o2Regen = ((biome == 1) ? 16.0f : 8.0f) * (hasUpg(U_PYROGEN) ? 0.75f : 1.0f);   // fever raises oxygen demand
    o2 = std::max(0.0f, std::min(o2Max, o2 + o2Regen * dt));
    hydration = std::min(100.0f, hydration + ((biome == 2 && !hasUpg(U_ORS)) ? 1.5f : 6.0f) * dt);

    for (int w = 0; w < W_COUNT; w++)
        if (w != weapon) pressure[w] = std::max(0.0f, pressure[w] - 3.5f * dt);   // rotating relieves pressure
}

// ---------- enemy behavior ----------

void Game::updateEnemies(float dt) {
    // Index, not iterator: behaviors append to `newborns`, never to `enemies`.
    for (size_t idx = 0; idx < enemies.size(); idx++) {
        Enemy &e = enemies[idx];
        if (!e.alive) continue;
        const OrgDef &d = ORGS[e.type];

        if (e.feedbackCd > 0) e.feedbackCd -= dt;
        if (e.opsonT > 0 && (e.opsonT -= dt) <= 0) e.opsonized = false;
        if (e.type == O_TRYPANOSOME) e.t1 += dt;

        updateEnemy(e, dt);

        if (!e.alive) continue;
        float distP = vdist(e.pos, ppos);
        if (e.hostile && d.contact > 0 && distP < e.radius + PLAYER_RADIUS && !e.submerged)
            hurtPlayer(d.contact);
        if (d.behavior != B_ORBIT_PARENT && d.behavior != B_BURROWER) clampToArena(e.pos, e.radius);
        if (!(d.traits & T_BOSS) && d.behavior != B_STATIONARY) pushOutObstacles(e.pos, e.radius);
    }
}

void Game::updateEnemy(Enemy &e, float dt) {
    const OrgDef &d = ORGS[e.type];
    Vector2 toPlayer = vnorm(vsub(ppos, e.pos));
    float distP = vdist(e.pos, ppos);
    float spd = d.speed;

    switch (d.behavior) {
        case B_HOST_DRIFT:
            e.pos = vadd(e.pos, vscale(e.vel, dt));
            if (biome == 0) e.pos = vadd(e.pos, vscale(flowDir, 25.0f * dt));
            if (frand(0, 1) < 0.01f) e.vel = vpolar(frand(0, 2 * PI), frand(8, 20));
            break;

        case B_STATIONARY:
            if (e.t2 > 0 && (e.t2 -= dt) <= 0) {   // hijacked lung cell bursts
                e.alive = false;
                burstParticles(e.pos, Color{250, 200, 140, 255}, 18, 200);
                play(sfxBurst, 0.5f);
                int n = std::max(1, 3 - (hasUpg(U_OSELTAMIVIR) ? 2 : 0));
                for (int i = 0; i < n; i++)
                    spawnEnemy(O_FLU, vadd(e.pos, vpolar(frand(0, 2 * PI), 16)));
            }
            break;

        case B_CHASE:
            e.pos = vadd(e.pos, vscale(toPlayer, spd * dt));
            break;

        case B_WOBBLE_CHASE: {
            float wig = sinf(e.t1 * 7.0f + e.seed) * 90.0f;
            e.pos = vadd(e.pos, vscale(vadd(vscale(toPlayer, spd), vscale(vperp(toPlayer), wig)), dt));
            break;
        }

        case B_DART: {   // flagellated: bursts of speed, then coast
            e.t1 += dt;
            float burst = (fmodf(e.t1 + e.seed * 0.01f, 1.4f) < 0.5f) ? 1.9f : 0.35f;
            e.pos = vadd(e.pos, vscale(toPlayer, spd * burst * dt));
            break;
        }

        case B_KEEPAWAY_SHOOT: {
            float want = (distP > 300) ? 1.0f : (distP < 210 ? -1.0f : 0.0f);
            Vector2 drift = vscale(vperp(toPlayer), sinf((float)gtime * 3 + e.seed) * 70.0f);
            e.pos = vadd(e.pos, vscale(vadd(vscale(toPlayer, spd * want), drift), dt));
            if ((e.t1 -= dt) <= 0) {
                e.t1 = 2.2f;
                Proj p;
                p.pos = e.pos;
                p.vel = vscale(toPlayer, 260.0f);
                p.dmg = 8;
                p.life = 3.0f;
                p.fromEnemy = true;
                p.toxin = true;
                p.radius = 6;
                projs.push_back(p);
            }
            break;
        }

        case B_BOUNCE:
            e.pos = vadd(e.pos, vscale(e.vel, dt));
            if (e.pos.x < ARENA_MARGIN + e.radius || e.pos.x > SCREEN_W - ARENA_MARGIN - e.radius) e.vel.x *= -1;
            if (e.pos.y < ARENA_MARGIN + e.radius || e.pos.y > SCREEN_H - ARENA_MARGIN - e.radius) e.vel.y *= -1;
            e.angle += 6.0f * dt;
            break;

        case B_SEEK_RBC: {
            Enemy *best = nullptr;
            float bd = 1e9f;
            for (auto &o : enemies) {
                if (!o.alive || o.type != O_RBC) continue;
                float dd = vdist(e.pos, o.pos);
                if (dd < bd) { bd = dd; best = &o; }
            }
            Vector2 tgt = best ? best->pos : ppos;
            e.pos = vadd(e.pos, vscale(vnorm(vsub(tgt, e.pos)), spd * dt));
            if (best && bd < best->radius + e.radius) {
                Vector2 bp = best->pos;
                best->alive = false;
                e.alive = false;
                Enemy &sch = spawnEnemy(O_INFECTED_RBC, bp);
                say(sch.pos, "BROKE INTO A BLOOD CELL", Color{255, 140, 170, 255});
            }
            break;
        }

        case B_INFECTED_BURST:
            if (biome == 0) e.pos = vadd(e.pos, vscale(flowDir, 12.0f * dt));
            if ((e.t1 -= dt) <= 0) {
                e.alive = false;
                hemolysis(e.pos, true);
                int n = hasUpg(U_ARTEMISININ) ? 3 : 4;
                for (int i = 0; i < n; i++)
                    spawnEnemy(O_MEROZOITE, vadd(e.pos, vpolar(frand(0, 2 * PI), 14)));
            }
            break;

        case B_SEEK_EPITHELIAL: {
            Enemy *tgt = e.targetId >= 0 ? findEnemy(e.targetId) : nullptr;
            if (!tgt || tgt->type != O_EPITHELIAL) {
                float bd = 1e9f;
                tgt = nullptr;
                for (auto &o : enemies) {
                    if (!o.alive || o.type != O_EPITHELIAL || o.t2 > 0) continue;
                    float dd = vdist(e.pos, o.pos);
                    if (dd < bd) { bd = dd; tgt = &o; }
                }
                e.targetId = tgt ? tgt->id : -1;
            }
            if (tgt) {
                e.pos = vadd(e.pos, vscale(vnorm(vsub(tgt->pos, e.pos)), spd * dt));
                if (vdist(e.pos, tgt->pos) < tgt->radius) {
                    tgt->t2 = 5.0f + (hasUpg(U_INTERFERON) ? 4.0f : 0.0f);
                    e.alive = false;
                    say(tgt->pos, "LUNG CELL HIJACKED", Color{255, 160, 120, 255});
                }
            } else {
                e.pos = vadd(e.pos, vscale(toPlayer, spd * 1.3f * dt));
            }
            break;
        }

        case B_BUDDING:   // carried by the blood; buds off daughters if ignored
            if (biome == 0) e.pos = vadd(e.pos, vscale(flowDir, 25.0f * dt));
            e.pos = vadd(e.pos, vpolar((float)gtime * 0.6f + e.seed, spd * 0.5f * dt));
            if ((e.t1 -= dt) <= 0 && e.t3 < 3.0f) {
                e.t1 = BUD_PERIOD;
                int living = 0;
                for (auto &o : enemies)
                    if (o.alive && o.type == O_CANDIDA) living++;
                if (living < 8) {
                    e.t3 += 1.0f;   // finite, or the room could never clear
                    Enemy &b = spawnEnemy(O_CANDIDA, vadd(e.pos, vpolar(e.seed * 0.1f, e.radius * 1.6f)));
                    b.t3 = e.t3;
                    clampToArena(b.pos, b.radius);
                    say(e.pos, "BUDDED", Color{200, 240, 240, 255});
                }
            }
            break;

        case B_AMOEBA_LUNGE:
            e.t1 -= dt;
            if (e.t1 > 0.6f) e.pos = vadd(e.pos, vscale(toPlayer, spd * 0.35f * dt));
            else if (e.t1 > 0) e.telegraph = ppos;                       // wind-up
            else if (e.t1 > -0.45f) e.pos = vadd(e.pos, vscale(vnorm(vsub(e.telegraph, e.pos)), 480.0f * dt));
            else e.t1 = frand(2.0f, 3.4f);
            break;

        case B_HYPHA_GROW:
            if ((e.t1 -= dt) <= 0 && e.t3 < 3.0f) {
                e.t1 = 7.0f;
                int living = 0;
                for (auto &o : enemies)
                    if (o.alive && o.type == O_ASPERGILLUS) living++;
                if (living < 9) {
                    e.t3 += 1.0f;   // finite branching
                    Enemy &h = spawnEnemy(O_ASPERGILLUS, vadd(e.pos, vpolar(e.angle + frand(-0.7f, 0.7f), 46)));
                    h.angle = e.angle + frand(-0.7f, 0.7f);
                    h.t3 = e.t3;
                    clampToArena(h.pos, h.radius);
                    say(h.pos, "SPREADING", Color{200, 240, 240, 255});
                }
            }
            break;

        case B_DORMANT:
            if ((e.t1 -= dt) <= 0) {
                e.alive = false;
                OrgId back = (e.type == O_TB_LATENT) ? O_TB : O_CDIFF;
                Enemy &r = spawnEnemy(back, e.pos);
                r.hp = r.maxHp = ORGS[back].hp * 0.7f;
                r.t3 = e.t3;
                say(e.pos, e.type == O_TB_LATENT ? "TB WOKE UP" : "SPORE HATCHED",
                    Color{255, 160, 100, 255});
            }
            break;

        case B_ALLY_HUNT: {
            Enemy *tgt = nullptr;
            float bd = 1e9f;
            for (auto &o : enemies) {
                if (!o.alive || !o.hostile || (ORGS[o.type].traits & T_BOSS)) continue;
                float dd = vdist(e.pos, o.pos);
                if (dd < bd) { bd = dd; tgt = &o; }
            }
            if (tgt) {
                e.pos = vadd(e.pos, vscale(vnorm(vsub(tgt->pos, e.pos)), spd * dt));
                if (bd < e.radius + tgt->radius) {
                    // TB survives being swallowed and turns the macrophage into a host
                    if (tgt->type == O_TB && e.type == O_MACROPHAGE) {
                        tgt->alive = false;
                        becomeType(e, O_TB);
                        say(e.pos, "TB TOOK OVER YOUR MACROPHAGE", Color{255, 120, 120, 255});
                    } else {
                        // Direct damage, so an ally's kill does not breed resistance.
                        tgt->hp -= 30.0f * dt;
                        if (tgt->hp <= 0) killEnemy(*tgt);
                        e.hp -= 8.0f * dt;
                        if (e.hp <= 0) killEnemy(e);
                    }
                }
            }
            break;
        }

        case B_ORBIT_PARENT: {
            Enemy *core = findEnemy(e.parentId);
            if (!core) { e.alive = false; break; }
            e.t1 += 0.85f * dt;
            e.pos = vadd(core->pos, vpolar(e.t1, 105.0f));
            break;
        }

        case B_EGG:
            if ((e.t2 -= dt) <= 0) {
                e.t2 = 2.0f;
                if (distP < 130) hurtPlayer(8);
                burstParticles(e.pos, Color{230, 210, 120, 200}, 10, 200);
            }
            if ((e.t1 -= dt) <= 0) e.alive = false;
            break;

        case B_WORMBOSS: {
            e.t1 += dt;
            Vector2 weave = vscale(vperp(toPlayer), sinf(e.t1 * 2.2f) * 70.0f);
            e.pos = vadd(e.pos, vscale(vadd(vscale(toPlayer, spd), weave), dt));
            clampToArena(e.pos, e.radius);
            e.trail.push_front(e.pos);
            if (e.trail.size() > 46) e.trail.pop_back();
            if ((e.t2 -= dt) <= 0) {
                e.t2 = 5.0f;
                Vector2 tail = e.trail.empty() ? e.pos : e.trail.back();
                for (int i = 0; i < 2; i++)
                    spawnEnemy(O_SCHISTO_EGG, vadd(tail, vpolar(frand(0, 2 * PI), 30)));
            }
            break;
        }

        case B_GRANULOMA_CORE: {
            if ((e.t1 -= dt) <= 0) {
                e.t1 = 6.0f;
                for (int i = 0; i < 2; i++) {
                    Enemy &tb = spawnEnemy(O_TB, vadd(e.pos, vpolar(frand(0, 2 * PI), 150)));
                    tb.hp = tb.maxHp = ORGS[O_TB].hp * 0.55f;
                    tb.parentId = e.id;
                }
            }
            if ((e.t2 -= dt) <= 0) {
                e.t2 = 7.0f;
                for (int i = 0; i < 10; i++) {
                    float a = i * 2 * PI / 10.0f + e.t3;
                    Proj p;
                    p.pos = vadd(e.pos, vpolar(a, e.radius + 6));
                    p.vel = vpolar(a, 215);
                    p.dmg = 10;
                    p.life = 3.5f;
                    p.fromEnemy = true;
                    p.radius = 6;
                    projs.push_back(p);
                }
                e.t3 += 0.5f;
            }
            break;
        }

        case B_BURROWER:
            if (!e.submerged) {
                e.t1 += dt;
                e.pos = vadd(e.pos, vscale(toPlayer, spd * dt));
                clampToArena(e.pos, e.radius);
                e.trail.push_front(e.pos);
                if (e.trail.size() > 60) e.trail.pop_back();
                if (e.t1 > 6.0f) {
                    e.submerged = true;
                    e.t1 = 0;
                    e.telegraph = ppos;
                    burstParticles(e.pos, Color{200, 170, 110, 255}, 20, 220);
                }
            } else {
                e.t1 += dt;
                if (e.t1 < 1.5f) e.telegraph = ppos;
                if (e.t1 > 2.4f) {
                    e.submerged = false;
                    e.t1 = 0;
                    e.pos = e.telegraph;
                    e.trail.clear();
                    shake = 10.0f;
                    play(sfxBurst, 0.9f);
                    addShock(e.pos, 10, 120, Color{225, 195, 130, 255}, 0.45f, 7);
                    burstParticles(e.pos, Color{200, 170, 110, 255}, 30, 280);
                    if (vdist(e.pos, ppos) < 95) hurtPlayer(25);
                    for (int i = 0; i < 2; i++)
                        spawnEnemy(O_ASCARIS_LARVA, vadd(e.pos, vpolar(frand(0, 2 * PI), 40)));
                }
            }
            break;
    }
}

// ---------- projectiles ----------

void Game::updateProjectiles(float dt) {
    for (auto &p : projs) {
        if (!p.alive) continue;

        if (p.homing > 0) {
            // Seekers only chase hostiles.
            const Enemy *tgt = nullptr;
            float bd = 1e9f;
            for (const auto &e : enemies) {
                if (!e.alive || !e.hostile || e.submerged) continue;
                float dd = vdist(e.pos, p.pos);
                if (dd < bd) { bd = dd; tgt = &e; }
            }
            if (tgt && bd < 460.0f) p.vel = steerToward(p.vel, vsub(tgt->pos, p.pos), p.homing * dt);
        }
        if (p.lob) {   // decelerates in flight
            p.vel = vscale(p.vel, 1.0f - LOB_DRAG * dt);
            p.radius = std::min(14.0f, p.radius + 8.0f * dt);
        }

        p.pos = vadd(p.pos, vscale(p.vel, dt));
        p.life -= dt;

        bool offArena = p.pos.x < ARENA_MARGIN - 10 || p.pos.x > SCREEN_W - ARENA_MARGIN + 10 ||
                        p.pos.y < ARENA_MARGIN - 10 || p.pos.y > SCREEN_H - ARENA_MARGIN + 10;
        if (p.life <= 0 || offArena) {
            if (p.lob && !offArena) detonateLob(p);
            p.alive = false;
            continue;
        }

        if (p.fromEnemy) {
            if (invuln <= 0 && vdist(p.pos, ppos) < p.radius + PLAYER_RADIUS) {
                hurtPlayer(p.dmg);
                if (p.toxin && !hasUpg(U_ORS)) {
                    hydration = std::max(0.0f, hydration - 12.0f);
                    say(ppos, "LOSING WATER", Color{140, 200, 255, 255});
                }
                p.alive = false;
            }
            continue;
        }

        for (auto &e : enemies) {
            if (!e.alive || e.submerged) continue;
            bool hit = vdist(p.pos, e.pos) < p.radius + e.radius;
            if (!hit && (ORGS[e.type].traits & T_BOSS) && !e.trail.empty())
                for (size_t i = 0; i < e.trail.size(); i += 4)
                    if (vdist(p.pos, e.trail[i]) < p.radius + e.radius * 0.7f) { hit = true; break; }
            if (!hit) continue;
            if (p.lob) { detonateLob(p); p.alive = false; break; }
            applyDamage(e, p.dmg, p.weapon);
            if (p.pierce > 0) p.pierce--;
            else { p.alive = false; break; }
        }
    }
    projs.erase(std::remove_if(projs.begin(), projs.end(), [](const Proj &p) { return !p.alive; }),
                projs.end());
}

// The IgG grenade's payload: a lingering cloud.
void Game::detonateLob(Proj &p) {
    const WeaponDef &w = WEAPONS[p.weapon];
    Field f;
    f.pos = p.pos;
    f.radius = w.area;
    f.life = f.maxLife = 4.5f;
    fields.push_back(f);
    addShock(p.pos, 8, w.area, Color{255, 240, 150, 255}, 0.40f, 5);
    burstParticles(p.pos, Color{255, 240, 155, 255}, 18, 170);
    splashDamage(p.pos, w.area, w.dmg, p.weapon, true);
    play(sfxBurst, 0.35f);
}

void Game::updateEffects(float dt) {
    hitSfxCd = std::max(0.0f, hitSfxCd - dt);

    for (auto &p : parts) {
        p.pos = vadd(p.pos, vscale(p.vel, dt));
        p.vel = vscale(p.vel, 1.0f - 3.0f * dt);
        p.life -= dt;
    }
    parts.erase(std::remove_if(parts.begin(), parts.end(),
                               [](const Particle &p) { return p.life <= 0; }), parts.end());

    for (auto &s : shocks) s.life -= dt;
    shocks.erase(std::remove_if(shocks.begin(), shocks.end(),
                                [](const Shock &s) { return s.life <= 0; }), shocks.end());

    // Antibody clouds keep stripping shells and tagging anything that walks in.
    for (auto &f : fields) {
        f.life -= dt;
        for (auto &e : enemies) {
            if (!e.alive || !e.hostile) continue;
            if (vdist(e.pos, f.pos) > f.radius + e.radius) continue;
            e.opsonized = true;
            e.opsonT = std::max(e.opsonT, 1.4f);
            if (e.capsuleIntact) {
                e.capsuleHp -= 40.0f * dt;
                if (e.capsuleHp <= 0) {
                    e.capsuleIntact = false;
                    burstParticles(e.pos, Color{200, 220, 255, 255}, 8, 130);
                }
            }
        }
    }
    fields.erase(std::remove_if(fields.begin(), fields.end(),
                                [](const Field &f) { return f.life <= 0; }), fields.end());

    for (auto &f : ftexts) { f.pos.y -= 26 * dt; f.life -= dt; }
    ftexts.erase(std::remove_if(ftexts.begin(), ftexts.end(),
                                [](const FloatText &f) { return f.life <= 0; }), ftexts.end());
}

// ---------- input ----------

void Game::gatherInput(float, Vector2 &moveDir, Vector2 &aimPos, bool &firing, bool &dashPressed) {
    aimPos = GetMousePosition();
    reticle = aimPos;
    firing = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    dashPressed = IsKeyPressed(KEY_SPACE);
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) moveDir.y -= 1;
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) moveDir.y += 1;
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) moveDir.x -= 1;
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) moveDir.x += 1;
    for (int k = 0; k < W_COUNT; k++)
        if (IsKeyPressed(KEY_ONE + k)) selectWeapon(k);
    int wheel = (int)GetMouseWheelMove();
    if (wheel) cycleWeapon(wheel > 0 ? 1 : -1);
    if (IsKeyPressed(KEY_Q)) cycleWeapon(-1);
    if (IsKeyPressed(KEY_E)) cycleWeapon(1);
}

// Stands in for a player under --smoke.
void Game::autopilot(float dt, Vector2 &moveDir, Vector2 &aimPos, bool &firing, bool &dashPressed) {
    float bd = 0;
    const Enemy *tgt = nearestHostile(ppos, &bd);

    if (tgt) {
        int best = weapon;
        float bestM = -1;
        for (int w = 0; w < W_COUNT; w++) {
            if (!weaponUnlocked[w]) continue;
            float m = (tgt->capsuleIntact && w == W_IGG) ? 3.0f : ratedAgainst(w, *tgt);
            if (w == W_COMPLEMENT && beamLock) m *= 0.1f;   // don't hold a locked-out beam
            if (m > bestM) { bestM = m; best = w; }
        }
        weapon = (WeaponId)best;

        // Close to within the weapon's real reach, or the bot plinks out of range forever.
        float want = std::min(250.0f, weaponReach(best) * 0.55f);
        Vector2 to = vnorm(vsub(tgt->pos, ppos));
        float radial = (bd > want * 1.15f) ? 1.0f : (bd < want * 0.55f ? -1.0f : 0.0f);
        moveDir = vnorm(vadd(vscale(to, radial), vscale(vperp(to), 0.8f)));
    } else {
        moveDir = vpolar((float)gtime * 0.9f, 1.0f);
    }
    aimPos = tgt ? tgt->pos : Vector2{SCREEN_W / 2.0f, SCREEN_H / 2.0f};
    reticle = aimPos;
    firing = tgt != nullptr;
    dashPressed = (fmodf((float)gtime, 2.0f) < dt);
}

// ---------- frame ----------

void Game::update(float dt) {
    gtime += dt;
    shake = std::max(0.0f, shake - 30.0f * dt);
    unlockBannerT = std::max(0.0f, unlockBannerT - dt);
    refreshUnlocks(true);
    flushSpawns();

    Vector2 moveDir{0, 0}, aimPos{0, 0};
    bool firing = false, dashPressed = false;
    if (smoke) autopilot(dt, moveDir, aimPos, firing, dashPressed);
    else gatherInput(dt, moveDir, aimPos, firing, dashPressed);

    updatePlayer(dt, moveDir, aimPos, firing, dashPressed);

    for (auto &ps : pending)
        if ((ps.timer -= dt) <= 0) spawnEnemy(ps.type, ps.pos);
    pending.erase(std::remove_if(pending.begin(), pending.end(),
                                 [](const PendingSpawn &s) { return s.timer <= 0; }), pending.end());

    updateEnemies(dt);
    flushSpawns();
    updateProjectiles(dt);
    updateEffects(dt);

    // How long the held weapon has been ineffective against the nearest threat.
    float nd = 0;
    const Enemy *threat = nearestHostile(ppos, &nd);
    if (threat && nd < 340.0f && ratedAgainst(weapon, *threat) < 0.6f) wrongWeaponT += dt;
    else wrongWeaponT = 0;

    enemies.erase(std::remove_if(enemies.begin(), enemies.end(),
                                 [](const Enemy &e) { return !e.alive; }), enemies.end());
    flushSpawns();

    // Resolve death after every kill this frame has been counted.
    if (pendingDeath) { pendingDeath = false; bankRun(); state = ST_GAMEOVER; return; }

    if (inTutorial) { updateTutorial(dt); flushSpawns(); return; }

    bool hostilesLeft = !pending.empty() || !newborns.empty();
    for (auto &e : enemies)
        if (e.alive && e.hostile) { hostilesLeft = true; break; }
    if (!hostilesLeft) {
        waveGap += dt;
        if (waveGap > 1.0f) {
            waveGap = 0;
            wave++;
            if (wave >= wavesInRoom) onRoomCleared();
            else spawnWaveNow();
        }
    }
}
