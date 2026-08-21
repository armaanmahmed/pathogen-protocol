#include "game.h"
#include "vecmath.h"
#include <algorithm>
#include <string>
#include <vector>

// ============================================================================
// Rendering. Every function here is const: drawing never changes the world.
// ============================================================================

namespace {

const Color TEAL     = {140, 240, 220, 255};
const Color AMBER    = {255, 185, 95, 255};
const Color GOOD     = {130, 245, 155, 255};
const Color BAD      = {255, 110, 110, 255};
const Color PANEL_BG = {14, 19, 24, 255};

Color biomeBg(int b) {
    switch (b) {
        case 0:  return Color{34, 10, 14, 255};
        case 1:  return Color{26, 26, 34, 255};
        default: return Color{32, 26, 14, 255};
    }
}
Color biomeWall(int b) {
    switch (b) {
        case 0:  return Color{130, 30, 40, 255};
        case 1:  return Color{170, 120, 135, 255};
        default: return Color{165, 130, 60, 255};
    }
}

// Four rating bands shared by the rack, the reticle and the nudge.
int ratingBand(float m) { return m >= 1.8f ? 3 : m >= 1.0f ? 2 : m >= 0.5f ? 1 : 0; }
Color bandColor(int band) {
    switch (band) {
        case 3:  return GOOD;
        case 2:  return Color{225, 235, 240, 255};
        case 1:  return AMBER;
        default: return BAD;
    }
}
const char *bandWord(int band) {
    switch (band) {
        case 3:  return "STRONG";
        case 2:  return "OK";
        case 1:  return "WEAK";
        default: return "USELESS";
    }
}

void panel(int x, int y, int w, int h, Color edge, float edgeAlpha = 0.55f) {
    DrawRectangle(x, y, w, h, Fade(PANEL_BG, 0.93f));
    DrawRectangleLines(x, y, w, h, Fade(edge, edgeAlpha));
}

void bar(int x, int y, int w, int h, float frac, Color c, const char *label) {
    DrawRectangle(x, y, w, h, Fade(BLACK, 0.55f));
    DrawRectangle(x, y, (int)(w * clampf(frac, 0.0f, 1.0f)), h, c);
    DrawRectangleLines(x, y, w, h, Fade(WHITE, 0.28f));
    if (label) DrawText(label, x + 6, y + h / 2 - 5, 10, WHITE);
}

// Greedy word wrap. Returns the y just past the last line drawn.
int wrapText(const char *text, int x, int y, int width, int size, Color c, int lineGap = 5) {
    std::string s = text, line;
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t sp = s.find(' ', pos);
        if (sp == std::string::npos) sp = s.size();
        std::string word = s.substr(pos, sp - pos);
        std::string test = line.empty() ? word : line + " " + word;
        if (!line.empty() && MeasureText(test.c_str(), size) > width) {
            DrawText(line.c_str(), x, y, size, c);
            y += size + lineGap;
            line = word;
        } else line = test;
        if (sp >= s.size()) break;
        pos = sp + 1;
    }
    if (!line.empty()) { DrawText(line.c_str(), x, y, size, c); y += size + lineGap; }
    return y;
}

void drawPadlock(int x, int y, Color c) {
    DrawRing({(float)x + 4, (float)y + 3}, 3.2f, 5.0f, 180, 360, 10, c);
    DrawRectangle(x, y + 3, 9, 7, c);
}

}   // namespace

// Shared by the card art and the mouse hit test.
Rectangle Game::upgradeCardRect(int i) {
    return {SCREEN_W / 2.0f - 480 + i * 330.0f, 210, 300, 250};
}

// ---------- world ----------

void Game::drawArenaBackdrop() const {
    ClearBackground(biomeBg(biome));
    Color wall = biomeWall(biome);
    for (int x = 0; x < SCREEN_W; x += 8) {
        float w1 = sinf(x * 0.02f + (float)gtime * 1.4f) * 10.0f;
        float w2 = cosf(x * 0.017f + (float)gtime * 1.1f) * 10.0f;
        DrawRectangle(x, 0, 8, (int)(ARENA_MARGIN - 12 + w1), wall);
        DrawRectangle(x, SCREEN_H - (int)(ARENA_MARGIN - 12 + w2), 8, (int)(ARENA_MARGIN - 12 + w2), wall);
    }
    for (int y = 0; y < SCREEN_H; y += 8) {
        float w1 = sinf(y * 0.02f + (float)gtime * 1.2f) * 10.0f;
        float w2 = cosf(y * 0.019f + (float)gtime * 1.5f) * 10.0f;
        DrawRectangle(0, y, (int)(ARENA_MARGIN - 12 + w1), 8, wall);
        DrawRectangle(SCREEN_W - (int)(ARENA_MARGIN - 12 + w2), y, (int)(ARENA_MARGIN - 12 + w2), 8, wall);
    }
    for (int i = 0; i < 40; i++) {
        float t = (float)gtime * 0.3f + i * 12.9898f;
        float x = fmodf(i * 133.7f + t * 40.0f * (1 + (i % 3)), (float)SCREEN_W);
        float y = fmodf(i * 71.3f + sinf(t + i) * 30.0f + 400.0f, (float)SCREEN_H);
        DrawCircle((int)x, (int)y, 2, Fade(wall, 0.22f));
    }
    for (auto &o : obstacles) {
        DrawCircleV(o.pos, o.radius, Fade(wall, 0.5f));
        DrawCircleV(o.pos, o.radius * 0.6f, Fade(wall, 0.38f));
        DrawCircleLines((int)o.pos.x, (int)o.pos.y, o.radius, Fade(WHITE, 0.13f));
    }
}

// One silhouette per morphology.
void Game::drawOrganism(const Enemy &e) const {
    const OrgDef &d = ORGS[e.type];
    float pulse = sinf((float)gtime * 5.0f + e.seed) * 0.5f + 0.5f;
    Color c = d.color;
    float r = e.radius;
    Vector2 p = e.pos;
    float faceAng = atan2f(ppos.y - p.y, ppos.x - p.x);

    switch (d.shape) {
        case SH_BICONCAVE:
            DrawCircleV(p, r, c);
            DrawCircleV(p, r * 0.55f, Fade(BLACK, 0.28f));
            if (e.type == O_INFECTED_RBC) {
                float g = 1.0f - e.t1 / 6.0f;
                for (int i = 0; i < 4; i++)
                    DrawCircleV(vadd(p, vpolar(i * 1.7f + (float)gtime, 4 + g * 6)), 3.2f, Color{95, 35, 115, 255});
                DrawCircleLines((int)p.x, (int)p.y, r + 3 + pulse * 3, Fade(Color{255, 90, 130, 255}, 0.4f + 0.6f * g));
            }
            break;
        case SH_COCCUS:
            DrawCircleV(p, r, c);
            DrawCircleV(vadd(p, vpolar(e.seed * 0.1f, r * 0.3f)), r * 0.4f, Fade(WHITE, 0.18f));
            break;
        case SH_DIPLOCOCCUS: {
            Vector2 off = vpolar(e.seed * 0.1f, r * 0.52f);
            DrawCircleV(vadd(p, off), r * 0.72f, c);
            DrawCircleV(vsub(p, off), r * 0.72f, c);
            DrawCircleV(vadd(p, off), r * 0.3f, Fade(WHITE, 0.15f));
            break;
        }
        case SH_CLUSTER:
            for (int i = 0; i < 6; i++)
                DrawCircleV(vadd(p, vpolar(i * 1.05f + e.seed * 0.01f, r * 0.55f)), r * 0.46f, c);
            DrawCircleV(p, r * 0.42f, c);
            break;
        case SH_ROD: {
            Rectangle rect = {p.x, p.y, r * 2.5f, r * 1.05f};
            float ang = (d.behavior == B_DORMANT) ? e.seed * 20.0f : faceAng * RAD2DEG;
            DrawRectanglePro(rect, {r * 1.25f, r * 0.52f}, ang, c);
            if (d.traits & T_ACIDFAST)   // banded stain of an acid-fast rod
                for (int i = -1; i <= 1; i++)
                    DrawCircleV(vadd(p, vpolar(faceAng, i * r * 0.7f)), r * 0.3f, Fade(WHITE, 0.22f));
            if (e.type == O_ECOLI)       // flagella
                for (int i = 0; i < 3; i++) {
                    Vector2 base = vadd(p, vpolar(faceAng + PI, r * 1.2f));
                    DrawLineEx(base, vadd(base, vpolar(faceAng + PI + sinf((float)gtime * 18 + i) * 0.5f, 12)),
                               1.5f, Fade(c, 0.8f));
                }
            break;
        }
        case SH_COMMA: {
            DrawRing(p, r * 0.45f, r, faceAng * RAD2DEG + 40, faceAng * RAD2DEG + 265, 16, c);
            Vector2 tail = vadd(p, vpolar(faceAng + PI, r + 2));
            for (int i = 0; i < 4; i++)
                DrawLineEx(vadd(tail, vpolar(faceAng + PI, i * 4.0f)),
                           vadd(tail, vadd(vpolar(faceAng + PI, (i + 1) * 4.0f),
                                           vpolar(faceAng + PI / 2, sinf((float)gtime * 14 + i) * 4))),
                           2, Fade(c, 0.9f));
            break;
        }
        case SH_SPIRAL: {
            bool shifting = fmodf(e.t1, 4.0f) > 3.0f;
            Color sc = shifting ? Color{245, 245, 255, 255} : c;
            Vector2 dir = vpolar(faceAng, 1);
            Vector2 perp = vperp(dir);
            for (int i = 0; i < 9; i++) {
                float off = sinf((float)gtime * 8 + i * 0.9f + e.seed) * 7.0f;
                Vector2 q = vadd(vadd(p, vscale(dir, -i * 4.6f)), vscale(perp, off));
                DrawCircleV(q, r * (1.0f - i * 0.08f) * 0.62f, sc);
            }
            if (shifting) DrawCircleLines((int)p.x, (int)p.y, r + 7, Fade(WHITE, 0.85f));
            break;
        }
        case SH_SPIKED:
            DrawCircleV(p, r, c);
            for (int i = 0; i < 10; i++) {
                float a = i * PI / 5 + e.seed * 0.01f;
                DrawLineEx(vadd(p, vpolar(a, r)), vadd(p, vpolar(a, r + 5)), 2, Fade(c, 0.95f));
                DrawCircleV(vadd(p, vpolar(a, r + 5)), 1.6f, Fade(WHITE, 0.5f));
            }
            break;
        case SH_WHEEL:
            DrawRing(p, r * 0.5f, r, 0, 360, 22, c);
            for (int i = 0; i < 8; i++)
                DrawLineEx(vadd(p, vpolar(e.angle + i * PI / 4, r * 0.2f)),
                           vadd(p, vpolar(e.angle + i * PI / 4, r)), 2, Fade(c, 0.9f));
            break;
        case SH_RINGFORM:
            DrawRing(p, r * 0.45f, r, 0, 360, 14, c);
            DrawCircleV(vadd(p, vpolar((float)gtime * 2 + e.seed, r * 0.55f)), r * 0.38f, Color{190, 130, 240, 255});
            break;
        case SH_BUDDING: {   // yeast cell; the bud swells as the next daughter grows
            float grow = (e.t3 < 3.0f) ? clampf(1.0f - e.t1 / BUD_PERIOD, 0.0f, 1.0f) : 0.0f;
            Vector2 budDir = vpolar(e.seed * 0.1f, 1.0f);
            DrawCircleV(vadd(p, vscale(budDir, r * (0.7f + 0.5f * grow))), r * (0.25f + 0.45f * grow), c);
            DrawCircleV(p, r, c);
            DrawCircleV(p, r * 0.3f, Fade(WHITE, 0.2f));
            break;
        }
        case SH_AMOEBA: {
            bool lunging = e.t1 <= 0;
            for (int i = 0; i < 10; i++) {
                float a = i * PI / 5;
                float wob = 1.0f + sinf((float)gtime * 3 + i * 1.7f + e.seed) * 0.28f;
                DrawCircleV(vadd(p, vpolar(a, r * 0.55f * wob)), r * 0.52f * wob, c);
            }
            DrawCircleV(vadd(p, vpolar(faceAng, r * 0.3f)), r * 0.3f, Fade(BLACK, 0.3f));
            if (e.t1 > 0 && e.t1 < 0.6f)   // wind-up telegraph
                DrawLineEx(p, e.telegraph, 2, Fade(RED, 0.35f + pulse * 0.4f));
            if (lunging) DrawCircleLines((int)p.x, (int)p.y, r + 6, Fade(RED, 0.6f));
            break;
        }
        case SH_HYPHA: {
            Vector2 a = vadd(p, vpolar(e.angle + PI, r * 1.6f));
            Vector2 b = vadd(p, vpolar(e.angle, r * 1.6f));
            DrawLineEx(a, b, r * 0.75f, c);
            DrawCircleV(b, r * 0.5f, c);
            for (int i = 0; i < 3; i++)
                DrawCircleV(vadd(a, vscale(vsub(b, a), (i + 1) / 4.0f)), r * 0.22f, Fade(BLACK, 0.25f));
            break;
        }
        case SH_WORM: {
            if (e.submerged) {
                DrawCircleLines((int)e.telegraph.x, (int)e.telegraph.y, 95, Fade(RED, 0.4f + pulse * 0.45f));
                DrawCircleV(e.telegraph, 8 + pulse * 7, Fade(RED, 0.5f));
                break;
            }
            if (e.trail.empty()) {
                DrawCircleV(p, r, c);
                DrawCircleV(vadd(p, vpolar(faceAng, r * 0.4f)), r * 0.4f, Fade(BLACK, 0.3f));
            } else {
                int i = 0;
                for (auto it = e.trail.rbegin(); it != e.trail.rend(); ++it, ++i) {
                    if (i % 2) continue;
                    float frac = (float)i / std::max<size_t>(e.trail.size(), 1);
                    Color seg = ((i / 2) % 2) ? c : Fade(c, 0.72f);
                    DrawCircleV(*it, r * (0.45f + 0.55f * frac), seg);
                }
                DrawCircleV(p, r, c);
                DrawCircleV(vadd(p, vpolar(faceAng, r * 0.5f)), r * 0.35f, Color{60, 30, 40, 255});
            }
            break;
        }
        case SH_EGG:
            DrawEllipse((int)p.x, (int)p.y, r * 0.8f, r, c);
            if (e.type == O_SCHISTO_EGG) {
                DrawTriangle(vadd(p, vpolar(0.5f, r + 9)), vadd(p, vpolar(0.85f, r - 1)),
                             vadd(p, vpolar(0.15f, r - 1)), c);
                float warn = 1.0f - fmodf(e.t2, 2.0f) / 2.0f;
                DrawCircleLines((int)p.x, (int)p.y, 130 * warn, Fade(c, 0.22f));
            } else {
                DrawEllipse((int)p.x, (int)p.y, r * 0.45f, r * 0.6f, Fade(WHITE, 0.3f));
                DrawCircleLines((int)p.x, (int)p.y, r + 3 + pulse * 2, Fade(c, 0.5f));
            }
            break;
        case SH_LOBED:
            DrawCircleV(p, r, c);
            for (int i = 0; i < 3; i++)
                DrawCircleV(vadd(p, vpolar(i * 2.1f + (float)gtime * 0.6f, r * 0.42f)), r * 0.3f,
                            Color{110, 100, 150, 255});
            break;
        case SH_BLOB: {
            bool infected = (e.type == O_EPITHELIAL && e.t2 > 0);
            Color body = infected ? Color{232, 150, 110, 255} : c;
            for (int i = 0; i < 8; i++) {
                float wob = 1.0f + sinf((float)gtime * 2 + i * 1.3f + e.seed) * 0.12f;
                DrawCircleV(vadd(p, vpolar(i * PI / 4, r * 0.4f)), r * 0.62f * wob, body);
            }
            DrawCircleV(p, r * 0.34f, Fade(BLACK, 0.25f));
            if (infected)
                DrawCircleLines((int)p.x, (int)p.y, r + 4 + pulse * 3, Fade(Color{255, 120, 80, 255}, 0.85f));
            break;
        }
        case SH_GRANULOMA:
            DrawCircleV(p, r + pulse * 3, c);
            DrawCircleV(p, r * 0.72f, Color{238, 228, 196, 255});
            for (int i = 0; i < 6; i++)
                DrawCircleV(vadd(p, vpolar(i * 1.05f + (float)gtime * 0.5f, r * 0.42f)), 5,
                            Color{225, 60, 95, 255});
            break;
        case SH_DEBRIS:
            DrawPoly(p, 5, r, e.seed * 1.0f, c);
            DrawPolyLines(p, 5, r, e.seed * 1.0f, Fade(WHITE, 0.3f));
            break;
    }

    // status rings: shell, tagged, evolved resistance
    if (e.capsuleIntact)
        DrawCircleLines((int)p.x, (int)p.y, r + 6, Fade(Color{215, 230, 255, 255}, 0.45f + pulse * 0.25f));
    if (e.opsonized)
        DrawCircleLines((int)p.x, (int)p.y, r + 9, Fade(Color{255, 240, 120, 255}, 0.9f));
    if (e.resistTo)
        for (int i = 0; i < 8; i++) {
            float a = i * PI / 4 + (float)gtime;
            DrawLineEx(vadd(p, vpolar(a, r + 2)), vadd(p, vpolar(a, r + 8)), 2, Fade(AMBER, 0.8f));
        }

    // Small health bar. Granuloma shields are T_BOSS but still get one.
    bool trueBoss = (e.type == O_BOSS_SCHISTO || e.type == O_BOSS_GRANULOMA || e.type == O_BOSS_ASCARIS);
    if (e.hostile && e.maxHp >= 40 && e.hp < e.maxHp && !trueBoss) {
        float w = 30;
        DrawRectangle((int)(p.x - w / 2), (int)(p.y - r - 12), (int)w, 4, Fade(BLACK, 0.5f));
        DrawRectangle((int)(p.x - w / 2), (int)(p.y - r - 12), (int)(w * e.hp / e.maxHp), 4, GOOD);
    }
}

void Game::drawPlayer() const {
    if (invuln > 0 && fmodf((float)gtime, 0.15f) < 0.06f) return;
    Vector2 visor = vadd(ppos, vpolar(aimAngle, 8));
    DrawCircleV(ppos, PLAYER_RADIUS, Color{70, 200, 190, 255});
    DrawCircleV(ppos, 10, Color{50, 160, 155, 255});
    DrawCircleV(visor, 5, Color{230, 250, 250, 255});
    DrawLineEx(vadd(ppos, vpolar(aimAngle, 12)), vadd(ppos, vpolar(aimAngle, 26)), 4,
               Color{230, 250, 250, 255});
    if (dashT > 0) DrawCircleLines((int)ppos.x, (int)ppos.y, 20, Fade(WHITE, dashT * 4));
}

void Game::drawEffects() const {
    // antibody clouds sit under everything else
    for (auto &f : fields) {
        float a = clampf(f.life / f.maxLife, 0.0f, 1.0f);
        DrawCircleV(f.pos, f.radius, Fade(Color{255, 235, 130, 255}, 0.10f * a));
        DrawCircleLines((int)f.pos.x, (int)f.pos.y, f.radius, Fade(Color{255, 240, 150, 255}, 0.5f * a));
        for (int i = 0; i < 10; i++) {
            float ang = i * 2 * PI / 10 + (float)gtime * 0.8f;
            DrawCircleV(vadd(f.pos, vpolar(ang, f.radius * 0.78f)), 2.4f,
                        Fade(Color{255, 245, 170, 255}, 0.75f * a));
        }
    }
    for (auto &s : shocks) {
        float t = 1.0f - clampf(s.life / s.maxLife, 0.0f, 1.0f);
        float r = s.r0 + (s.r1 - s.r0) * (1.0f - (1.0f - t) * (1.0f - t));
        DrawRing(s.pos, std::max(0.0f, r - s.thick), r, 0, 360, 40, Fade(s.color, 0.85f * (1.0f - t)));
    }
}

// Three stacked lines: a hot core inside a glow.
static void drawBeam(Vector2 a, Vector2 b, float pulse) {
    DrawLineEx(a, b, 16 + pulse * 4, Fade(Color{120, 190, 255, 255}, 0.20f));
    DrawLineEx(a, b, 8, Fade(Color{170, 225, 255, 255}, 0.55f));
    DrawLineEx(a, b, 3, Color{240, 252, 255, 255});
    DrawCircleV(b, 7 + pulse * 4, Fade(Color{200, 235, 255, 255}, 0.7f));
}

// ---------- aiming ----------

// Per-weapon range guide, in world space so it shakes with the arena.
void Game::drawAimGuides() const {
    const WeaponDef &w = WEAPONS[weapon];
    float pulse = sinf((float)gtime * 6.0f) * 0.5f + 0.5f;
    switch (w.kind) {
        case WK_NOVA: {   // aim is irrelevant; show the ring
            Color ring = beamLock ? BAD : Color{255, 205, 120, 255};
            for (int i = 0; i < 44; i++) {
                if (i % 2) continue;
                float a0 = i * 2 * PI / 44, a1 = (i + 1) * 2 * PI / 44;
                DrawLineEx(vadd(ppos, vpolar(a0, w.area)), vadd(ppos, vpolar(a1, w.area)), 2,
                           Fade(ring, 0.30f + 0.25f * pulse));
            }
            break;
        }
        case WK_BEAM: {
            Vector2 dir = vnorm(vsub(reticle, ppos));
            if (vlen(dir) > 0.5f)
                DrawLineEx(vadd(ppos, vpolar(atan2f(dir.y, dir.x), PLAYER_RADIUS + 6)),
                           vadd(ppos, vscale(dir, w.speed)), 1,
                           Fade(beamLock ? BAD : Color{160, 215, 255, 255}, 0.18f));
            break;
        }
        case WK_LOB: {   // where the cloud will land
            Vector2 dir = vnorm(vsub(reticle, ppos));
            float throwDist = w.speed * (1.0f - expf(-LOB_DRAG * w.life)) / LOB_DRAG;
            Vector2 land = vadd(ppos, vscale(dir, std::min(throwDist, vdist(reticle, ppos))));
            clampToArena(land, w.area * 0.2f);
            DrawCircleLines((int)land.x, (int)land.y, w.area, Fade(Color{255, 240, 150, 255}, 0.45f));
            DrawCircleV(land, 3, Fade(Color{255, 240, 150, 255}, 0.8f));
            break;
        }
        default: break;
    }
}

// Turns red when the held weapon is wrong for the target. Drawn outside the
// shake camera so it stays on the real mouse position.
void Game::drawReticle() const {
    float pulse = sinf((float)gtime * 6.0f) * 0.5f + 0.5f;

    const Enemy *aimed = nullptr;
    float bd = 1e9f;
    for (const auto &e : enemies) {
        if (!e.alive || !e.hostile) continue;
        float dd = vdist(e.pos, reticle);
        if (dd < e.radius + 34 && dd < bd) { bd = dd; aimed = &e; }
    }
    int band = aimed ? ratingBand(ratedAgainst(weapon, *aimed)) : 2;
    Color rc = aimed ? bandColor(band) : Fade(WHITE, 0.55f);

    float r = 11 + (aimed ? 3.0f * pulse : 0.0f);
    for (int i = 0; i < 4; i++) {
        float a = i * PI / 2 + PI / 4;
        DrawLineEx(vadd(reticle, vpolar(a, r)), vadd(reticle, vpolar(a, r + 7)), 2, rc);
    }
    DrawCircleLines((int)reticle.x, (int)reticle.y, r, Fade(rc, 0.75f));
    DrawCircleV(reticle, 1.6f, rc);
    if (aimed && band == 0) {
        const char *t = "WRONG WEAPON";
        DrawText(t, (int)reticle.x - MeasureText(t, 11) / 2, (int)reticle.y + r + 10, 11,
                 Fade(BAD, 0.6f + 0.4f * pulse));
    }
}

// ---------- HUD ----------

void Game::drawHUD() const {
    bar(14, 12, 200, 16, php / phpMax, GOOD, TextFormat("HULL  %d", (int)php));
    // The O2 bar itself shrinks as maximum capacity is lost.
    DrawRectangle(14, 32, 200, 14, Fade(BLACK, 0.30f));
    bar(14, 32, (int)(200 * o2Max / 100.0f), 14, o2 / o2Max, Color{120, 170, 250, 255},
        TextFormat("OXYGEN  %d", (int)o2));
    if (biome == 2)
        bar(14, 50, 200, 14, hydration / 100.0f, Color{110, 220, 240, 255},
            TextFormat("WATER  %d", (int)hydration));

    const char *loc = inTutorial ? "TRAINING"
                    : (roomInBiome == 3) ? TextFormat("%s  -  BOSS", biomeName(biome))
                    : TextFormat("%s   site %d/3   wave %d/%d", biomeName(biome), roomInBiome + 1,
                                 std::min(wave + 1, wavesInRoom), wavesInRoom);
    DrawText(loc, SCREEN_W / 2 - MeasureText(loc, 18) / 2, 14, 18, Fade(WHITE, 0.85f));
    DrawText("[TAB] codex", SCREEN_W - 96, 16, 12, Fade(WHITE, 0.45f));

    int ux = 14, uy = biome == 2 ? 74 : 58;
    for (int i = 0; i < U_COUNT; i++) {
        if (!haveUpg[i]) continue;
        int w = MeasureText(UPGRADES[i].name, 10) + 8;
        DrawRectangle(ux, uy, w, 14, Fade(Color{80, 160, 150, 255}, 0.5f));
        DrawText(UPGRADES[i].name, ux + 4, uy + 2, 10, WHITE);
        uy += 17;
    }

    for (auto &e : enemies) {
        if (!e.alive || !(ORGS[e.type].traits & T_BOSS) || e.type == O_GRANULOMA_SHIELD) continue;
        int bw = 460;
        DrawRectangle(SCREEN_W / 2 - bw / 2, 44, bw, 14, Fade(BLACK, 0.6f));
        DrawRectangle(SCREEN_W / 2 - bw / 2, 44, (int)(bw * e.hp / e.maxHp), 14, Color{240, 90, 90, 255});
        DrawText(ORGS[e.type].name, SCREEN_W / 2 - MeasureText(ORGS[e.type].name, 12) / 2, 60, 12,
                 Fade(WHITE, 0.8f));
        break;
    }

    for (auto &ps : pending) {
        float f = 1.0f - ps.timer / 0.8f;
        DrawCircleLines((int)ps.pos.x, (int)ps.pos.y, 22 * (1.0f - f) + 6, Fade(RED, 0.3f + 0.5f * f));
    }

    // Low-hull warning on the screen edge.
    float hurt = 1.0f - clampf(php / phpMax / 0.35f, 0.0f, 1.0f);
    if (hurt > 0) {
        float a = hurt * (0.28f + 0.16f * (sinf((float)gtime * 4.0f) * 0.5f + 0.5f));
        for (int i = 0; i < 34; i++) {
            float f = (1.0f - i / 34.0f) * a;
            DrawRectangleLines(i, i, SCREEN_W - 2 * i, SCREEN_H - 2 * i, Fade(Color{230, 40, 50, 255}, f));
        }
    }

    drawTargetPlate();
    drawWeaponRack();
}

// Above the rack: the nearest threat, its counter, and - when the wrong
// weapon is held - which key to press.
void Game::drawTargetPlate() const {
    float nd = 0;
    const Enemy *n = nearestHostile(ppos, &nd);
    if (!n) return;
    const OrgDef &d = ORGS[n->type];

    int y = SCREEN_H - 92;
    int nameW = MeasureText(d.name, 17);
    int hintW = MeasureText(d.counter, 13);
    int w = std::max(nameW + 220, hintW + 40);
    int x = SCREEN_W / 2 - w / 2;

    panel(x, y, w, 34, Fade(d.color, 0.5f), 0.7f);
    DrawCircle(x + 15, y + 12, 6, d.color);
    DrawText(d.name, x + 28, y + 4, 17, d.color);
    DrawText(d.latin, x + 34 + nameW, y + 8, 11, Fade(WHITE, 0.4f));
    DrawText(d.counter, x + 28, y + 21, 11, Fade(WHITE, 0.72f));

    if (wrongWeaponT > 0.5f) {
        int best = bestWeaponAgainst(*n);
        if (best != weapon) {
            float pulse = sinf((float)gtime * 7.0f) * 0.5f + 0.5f;
            const char *t = TextFormat("PRESS [%d]  %s", best + 1, WEAPONS[best].shortName);
            int tw = MeasureText(t, 15) + 20;
            DrawRectangle(x + w - tw - 6, y + 6, tw, 22, Fade(AMBER, 0.20f + 0.18f * pulse));
            DrawRectangleLines(x + w - tw - 6, y + 6, tw, 22, Fade(AMBER, 0.6f + 0.4f * pulse));
            DrawText(t, x + w - tw + 4, y + 10, 15, Fade(WHITE, 0.85f + 0.15f * pulse));
        }
    }
}

// Compact rack: key, name, effectiveness strip and cost meter per slot.
void Game::drawWeaponRack() const {
    const int SW = 150, SH = 48, GAP = 6;
    int total = SW * W_COUNT + GAP * (W_COUNT - 1);
    int x0 = SCREEN_W / 2 - total / 2, y = SCREEN_H - 56;

    const Enemy *nearest = nearestHostile(ppos);

    for (int i = 0; i < W_COUNT; i++) {
        int x = x0 + i * (SW + GAP);
        bool sel = (i == weapon);

        if (!weaponUnlocked[i]) {
            // Locked slots show their requirement and progress.
            DrawRectangle(x, y, SW, SH, Fade(Color{12, 12, 14, 255}, 0.92f));
            DrawRectangleLines(x, y, SW, SH, Fade(AMBER, 0.28f));
            drawPadlock(x + 9, y + 8, Fade(WHITE, 0.45f));
            DrawText(WEAPONS[i].shortName, x + 26, y + 6, 12, Fade(WHITE, 0.42f));
            DrawText(TextFormat("%d", i + 1), x + SW - 14, y + 6, 11, Fade(WHITE, 0.25f));

            int have = 0, need = 0;
            if (unlockProgress(i, have, need)) {
                float f = clampf((float)have / (float)need, 0.0f, 1.0f);
                DrawRectangle(x + 7, y + 25, SW - 14, 8, Fade(BLACK, 0.6f));
                DrawRectangle(x + 7, y + 25, (int)((SW - 14) * f), 8, Fade(AMBER, 0.9f));
                DrawText(TextFormat("%d / %d", std::min(have, need), need), x + 7, y + 36, 10,
                         Fade(WHITE, 0.7f));
            } else {
                DrawText(UNLOCKS[i].how, x + 7, y + 30, 10, Fade(WHITE, 0.55f));
            }
            continue;
        }

        DrawRectangle(x, y, SW, SH, Fade(sel ? Color{26, 62, 64, 255} : Color{15, 20, 24, 255}, 0.93f));
        DrawRectangleLines(x, y, SW, SH, sel ? Color{150, 250, 230, 255} : Fade(WHITE, 0.16f));

        // Effectiveness strip along the top edge.
        if (nearest) {
            int band = ratingBand(ratedAgainst(i, *nearest));
            DrawRectangle(x + 1, y + 1, SW - 2, 4, Fade(bandColor(band), sel ? 1.0f : 0.7f));
            if (sel) {
                const char *word = bandWord(band);
                DrawText(word, x + SW - 6 - MeasureText(word, 10), y + 8, 10, bandColor(band));
            }
        }

        DrawText(TextFormat("%d", i + 1), x + 8, y + 9, 13, sel ? WHITE : Fade(WHITE, 0.55f));
        DrawText(WEAPONS[i].shortName, x + 22, y + 10, 12, sel ? WHITE : Fade(WHITE, 0.72f));
        DrawText(WEAPONS[i].tag, x + 22, y + 25, 9, Fade(sel ? TEAL : WHITE, sel ? 0.9f : 0.35f));

        // Cost meter: heat for the beam, resistance pressure for everything else.
        bool isBeam = WEAPONS[i].kind == WK_BEAM;
        float f = weaponCharge(i);
        Color meter = isBeam ? (beamLock ? BAD : Color{150, 210, 255, 255})
                             : (f >= 1.0f ? Color{255, 120, 60, 255} : Fade(AMBER, 0.8f));
        DrawRectangle(x + 7, y + SH - 11, SW - 14, 6, Fade(BLACK, 0.5f));
        DrawRectangle(x + 7, y + SH - 11, (int)((SW - 14) * f), 6, meter);
        if (isBeam && beamLock) DrawText("COOLING", x + SW - 56, y + SH - 12, 9, BAD);
        else if (!isBeam && f >= 1.0f) DrawText("RESISTED", x + SW - 58, y + SH - 12, 9, WHITE);
    }
}

void Game::drawUnlockBanner() const {
    if (unlockBannerT <= 0 || unlockBannerW < 0) return;
    const WeaponDef &w = WEAPONS[unlockBannerW];
    float a = std::min(1.0f, unlockBannerT / 0.6f);
    int bw = 760, x = SCREEN_W / 2 - bw / 2, y = 176;
    DrawRectangle(x, y, bw, 74, Fade(Color{34, 30, 10, 255}, 0.93f * a));
    DrawRectangleLines(x, y, bw, 74, Fade(Color{255, 210, 110, 255}, a));
    const char *h = TextFormat("NEW WEAPON  -  [%d]  %s   (%s)", unlockBannerW + 1, w.name, w.tag);
    DrawText(h, x + 16, y + 10, 20, Fade(Color{255, 226, 140, 255}, a));
    wrapText(w.mech, x + 16, y + 38, bw - 32, 13, Fade(WHITE, 0.88f * a));
}

// Full arsenal with unlock progress; shared by the title and end screens.
void Game::drawArsenalPanel(int x, int y) const {
    DrawText("ARSENAL", x, y, 16, TEAL);
    y += 26;
    for (int i = 0; i < W_COUNT; i++) {
        bool have = weaponUnlocked[i];
        DrawText(TextFormat("[%d]", i + 1), x, y, 13, Fade(WHITE, have ? 0.8f : 0.3f));
        DrawText(WEAPONS[i].shortName, x + 30, y, 13,
                 have ? Color{190, 250, 235, 255} : Fade(WHITE, 0.34f));
        DrawText(WEAPONS[i].tag, x + 150, y + 1, 11, Fade(have ? TEAL : WHITE, have ? 0.8f : 0.3f));
        if (have) {
            DrawText("READY", x + 222, y, 12, GOOD);
        } else {
            drawPadlock(x + 222, y + 2, Fade(WHITE, 0.4f));
            int hv = 0, nd = 0;
            if (unlockProgress(i, hv, nd)) {
                float f = clampf((float)hv / (float)nd, 0.0f, 1.0f);
                DrawRectangle(x + 240, y + 3, 110, 8, Fade(BLACK, 0.55f));
                DrawRectangle(x + 240, y + 3, (int)(110 * f), 8, AMBER);
                DrawText(TextFormat("%d/%d", std::min(hv, nd), nd), x + 358, y, 12, AMBER);
            }
            DrawText(UNLOCKS[i].how, x + 420, y + 1, 11, Fade(WHITE, 0.45f));
        }
        y += 22;
    }
}

void Game::drawTutorialPrompt() const {
    if (!inTutorial) return;
    struct L { const char *head, *body; };
    static const L lines[6] = {
        {"MOVE", "W A S D to move. You have been shrunk down and injected into a patient."},
        {"SHOOT", "Aim with the MOUSE, hold LEFT MOUSE to fire. Clear the gray junk."},
        {"PURPLE BUGS", "Purple means a bare cell wall. Your [1] LYSOZYME stream melts it. Hold the trigger."},
        {"PINK BUGS",
         "Pink means a greasy outer skin, and [1] just bounces off. Press [2] for the COMPLEMENT BEAM - "
         "hold it on target and it drills straight through. Watch the heat meter."},
        {"WHITE HALO = SHELL",
         "A halo means a slime shell that blocks everything. Press [4] and LOB a tag grenade at them - "
         "the cloud eats the shell. Then switch back to [1] and finish them."},
        {"YOU ARE READY",
         "Color tells you which gun to use. The bar at the bottom turns green when you are holding the "
         "right one. Injecting now..."},
    };
    const L &l = lines[std::min(tutStage, 5)];
    int bw = 840, x = SCREEN_W / 2 - bw / 2, y = 92;
    DrawRectangle(x, y, bw, 74, Fade(Color{8, 26, 30, 255}, 0.92f));
    DrawRectangleLines(x, y, bw, 74, Fade(Color{130, 235, 220, 255}, 0.7f));
    DrawText(l.head, x + 14, y + 9, 18, TEAL);
    wrapText(l.body, x + 14, y + 34, bw - 28, 14, Fade(WHITE, 0.92f));
    if (tutStage < 5) DrawText("[K] skip training", x + bw - 122, y + 10, 11, Fade(WHITE, 0.4f));
}

// ---------- screens ----------

void Game::drawTitle() const {
    ClearBackground(Color{12, 16, 22, 255});
    for (int i = 0; i < 60; i++) {
        float t = (float)gtime * 0.2f + i;
        DrawCircle((int)fmodf(i * 213.7f + t * 20, (float)SCREEN_W),
                   (int)fmodf(i * 97.3f + sinf(t) * 40 + 500, (float)SCREEN_H), 2 + i % 3,
                   Fade(Color{180, 60, 70, 255}, 0.22f));
    }
    const char *title = "PATHOGEN PROTOCOL";
    DrawText(title, SCREEN_W / 2 - MeasureText(title, 54) / 2, 76, 54, TEAL);
    const char *sub = "You have been shrunk and injected. Clear the infection before the patient dies.";
    DrawText(sub, SCREEN_W / 2 - MeasureText(sub, 16) / 2, 140, 16, Fade(WHITE, 0.75f));

    int x = 66, y = 196;
    DrawText("COLOUR TELLS YOU WHICH GUN TO USE", x, y, 17, TEAL);
    y += 30;
    struct R { const char *a, *b; Color c; };
    const R rows[5] = {
        {"PURPLE", "bare cell wall  ->  [1] LYSOZYME",   {150, 90, 205, 255}},
        {"PINK",   "greasy outer skin  ->  [2] BEAM",    {235, 105, 140, 255}},
        {"HALO",   "slime shell  ->  [4] TAG first",     {215, 230, 255, 255}},
        {"RED",    "waxy armor (TB)  ->  need the drug", {225, 60, 95, 255}},
        {"TAN",    "worms  ->  [5] SEEKERS",             {214, 184, 128, 255}},
    };
    for (auto &r : rows) {
        DrawCircle(x + 9, y + 8, 7, r.c);
        DrawText(r.a, x + 26, y, 15, r.c);
        DrawText(r.b, x + 110, y, 15, Fade(WHITE, 0.72f));
        y += 26;
    }
    y += 14;
    DrawText("Lean on one gun too long and the germs evolve around it. Rotate.", x, y, 13, AMBER);

    drawArsenalPanel(x, 402);

    y = 540;
    DrawText("WASD move   MOUSE aim   LMB fire   SPACE dash   1-5 / Q / E switch weapon   TAB codex",
             x, y, 13, Fade(WHITE, 0.55f));
    y += 22;
    DrawText(TextFormat("Career:  %d germs cleared   %d bosses   %d patients cured", meta.kills,
                        meta.bosses, meta.victories), x, y, 13, Fade(WHITE, 0.45f));

    const char *go = "PRESS [ENTER] TO INJECT";
    float pulse = sinf((float)gtime * 3) * 0.5f + 0.5f;
    DrawText(go, SCREEN_W / 2 - MeasureText(go, 26) / 2, 632, 26,
             Fade(Color{255, 230, 140, 255}, 0.5f + pulse * 0.5f));
}

void Game::drawUpgradeScreen() const {
    drawArenaBackdrop();
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(BLACK, 0.66f));
    const char *h = "SITE CLEARED";
    DrawText(h, SCREEN_W / 2 - MeasureText(h, 30) / 2, 108, 30, TEAL);
    const char *h2 = "Pick one treatment. It lasts for this run only.";
    DrawText(h2, SCREEN_W / 2 - MeasureText(h2, 15) / 2, 148, 15, Fade(WHITE, 0.65f));

    for (int i = 0; i < 3; i++) {
        Rectangle card = upgradeCardRect(i);
        bool hover = CheckCollisionPointRec(GetMousePosition(), card);
        DrawRectangleRounded(card, 0.08f, 8,
                             Fade(hover ? Color{40, 72, 78, 255} : Color{20, 38, 43, 255}, 0.95f));
        DrawRectangleRoundedLines(card, 0.08f, 8, hover ? Color{160, 250, 230, 255} : Fade(WHITE, 0.28f));
        const UpgradeDef &u = UPGRADES[upChoice[i]];
        int cx = (int)card.x + 18;
        DrawText(TextFormat("[%d]", i + 1), cx, (int)card.y + 14, 18, Fade(WHITE, 0.45f));
        DrawText(u.name, cx, (int)card.y + 44, 21, Color{200, 250, 240, 255});
        DrawText(u.real, cx, (int)card.y + 70, 12, Fade(WHITE, 0.42f));
        wrapText(u.desc, cx, (int)card.y + 100, (int)card.width - 36, 14, Fade(WHITE, 0.88f));
    }
    DrawText("click a card, or press 1 / 2 / 3", SCREEN_W / 2 - 100, 494, 14, Fade(WHITE, 0.5f));
}

void Game::drawCodex() const {
    ClearBackground(Color{9, 13, 18, 255});
    DrawText("FIELD GUIDE", 40, 26, 28, TEAL);
    DrawText("Everything you have met so far.   [TAB] resume    [<-] [->] pages", 40, 60, 13,
             Fade(WHITE, 0.5f));

    std::vector<int> seen;
    for (int i = 0; i < O_COUNT; i++)
        if (seenOrg[i] && i != O_DEBRIS) seen.push_back(i);
    const int perPage = 3;
    int pages = std::max(1, ((int)seen.size() + perPage - 1) / perPage);
    int page = std::max(0, std::min(codexPage, pages - 1));

    if (seen.empty())
        DrawText("Nothing cataloged yet. Go and meet something.", 40, 120, 16, Fade(WHITE, 0.6f));

    int y = 96;
    for (int i = page * perPage; i < (int)seen.size() && i < (page + 1) * perPage; i++) {
        const OrgDef &d = ORGS[seen[i]];
        const int CH = 178;
        DrawRectangle(40, y, SCREEN_W - 80, CH, Fade(Color{18, 24, 30, 255}, 0.9f));
        DrawRectangleLines(40, y, SCREEN_W - 80, CH, Fade(d.color, 0.6f));
        DrawCircleV({76.0f, (float)y + 34}, 15, d.color);
        DrawText(d.name, 104, y + 12, 22, d.color);
        DrawText(d.latin, 108 + MeasureText(d.name, 22), y + 22, 13, Fade(WHITE, 0.45f));

        int tx = 104;
        struct TT { int bit; const char *n; };
        const TT tags[] = {{T_GRAMPOS, "PURPLE / bare wall"}, {T_GRAMNEG, "PINK / outer skin"},
                           {T_CAPSULE, "SLIME SHELL"}, {T_CATALASE, "EATS PEROXIDE"},
                           {T_ACIDFAST, "WAXY ARMOR"}, {T_VIRUS, "VIRUS"}, {T_PROTOZOA, "PARASITE"},
                           {T_HELMINTH, "WORM"}, {T_FUNGUS, "FUNGUS"}, {T_HOST, "YOUR OWN CELL"}};
        for (auto &t : tags) {
            if (!(d.traits & t.bit)) continue;
            int w = MeasureText(t.n, 10) + 10;
            DrawRectangle(tx, y + 50, w, 16, Fade(d.color, 0.28f));
            DrawText(t.n, tx + 5, y + 53, 10, Fade(WHITE, 0.9f));
            tx += w + 5;
        }
        DrawText(d.threat, 104, y + 74, 14, Fade(WHITE, 0.85f));
        DrawText(d.counter, 104, y + 94, 14, Color{150, 235, 200, 255});
        DrawText("THE REAL SCIENCE", 104, y + 120, 10, Fade(TEAL, 0.65f));
        wrapText(d.science, 104, y + 136, SCREEN_W - 220, 12, Fade(WHITE, 0.6f), 4);
        y += CH + 12;
    }
    DrawText(TextFormat("page %d / %d", page + 1, pages), SCREEN_W - 150, SCREEN_H - 34, 14,
             Fade(WHITE, 0.5f));
}

void Game::drawEndScreen(bool victory) const {
    ClearBackground(victory ? Color{14, 26, 20, 255} : Color{26, 12, 14, 255});
    const char *h = victory ? "PATIENT CURED" : "THE INFECTION WON";
    DrawText(h, SCREEN_W / 2 - MeasureText(h, 48) / 2, 140, 48, victory ? Color{150, 250, 180, 255} : BAD);
    const char *sub = victory ? "All three organ systems clear. The fever breaks."
                              : TextFormat("You went down in the %s.", biomeName(biome));
    DrawText(sub, SCREEN_W / 2 - MeasureText(sub, 18) / 2, 204, 18, Fade(WHITE, 0.8f));

    int x = SCREEN_W / 2 - 300;
    DrawText(TextFormat("Cleared this run:  %d", lastRunKills), x, 258, 17, WHITE);
    DrawText(TextFormat("Germs that evolved around your guns:  %d", resistantStrains), x, 282, 15,
             resistantStrains > 3 ? AMBER : Fade(WHITE, 0.7f));
    DrawText(TextFormat("Career:  %d cleared   %d bosses   %d cures", meta.kills, meta.bosses,
                        meta.victories), x, 308, 15, Fade(WHITE, 0.55f));

    drawArsenalPanel(x, 352);
    const char *go = "PRESS [ENTER] FOR THE NEXT PATIENT";
    DrawText(go, SCREEN_W / 2 - MeasureText(go, 22) / 2, 592, 22, Color{255, 230, 140, 255});
}

// ---------- frame ----------

void Game::draw() {
    BeginDrawing();
    switch (state) {
        case ST_TITLE:    drawTitle(); break;
        case ST_CODEX:    drawCodex(); break;
        case ST_UPGRADE:  drawUpgradeScreen(); break;
        case ST_GAMEOVER: drawEndScreen(false); break;
        case ST_VICTORY:  drawEndScreen(true); break;
        case ST_PLAY: {
            // Screen shake moves the world, never the HUD.
            Camera2D cam{};
            cam.zoom = 1.0f;
            cam.offset = {sinf((float)gtime * 57.0f) * shake, cosf((float)gtime * 71.0f) * shake};

            drawArenaBackdrop();
            BeginMode2D(cam);
            drawEffects();
            drawAimGuides();
            for (auto &e : enemies) if (e.alive && !e.hostile) drawOrganism(e);
            for (auto &e : enemies) if (e.alive && e.hostile) drawOrganism(e);
            if (beamOn) drawBeam(vadd(ppos, vpolar(aimAngle, PLAYER_RADIUS + 4)), beamEnd,
                                 sinf((float)gtime * 30.0f) * 0.5f + 0.5f);
            for (auto &p : projs) {
                Color c = p.fromEnemy ? (p.toxin ? Color{150, 230, 250, 255} : Color{250, 160, 90, 255})
                        : (p.weapon == W_IGG) ? Color{255, 240, 120, 255}
                        : (p.weapon == W_OXBURST) ? Color{255, 210, 130, 255}
                        : (p.weapon == W_EOSINOPHIL) ? Color{255, 140, 160, 255}
                        : Color{170, 250, 235, 255};
                if (p.lob) {
                    DrawCircleV(p.pos, p.radius, Fade(c, 0.35f));
                    DrawCircleV(p.pos, p.radius * 0.5f, c);
                } else if (p.homing > 0) {
                    DrawCircleV(p.pos, p.radius, c);
                    DrawLineEx(p.pos, vsub(p.pos, vscale(vnorm(p.vel), 12)), 2, Fade(c, 0.45f));
                } else {
                    DrawCircleV(p.pos, p.radius, c);
                }
            }
            drawPlayer();
            for (auto &p : parts)
                DrawCircleV(p.pos, p.radius * (p.life / p.maxLife), Fade(p.color, p.life / p.maxLife));
            for (auto &f : ftexts)
                DrawText(f.text.c_str(), (int)f.pos.x - MeasureText(f.text.c_str(), 12) / 2,
                         (int)f.pos.y, 12, Fade(f.color, std::min(1.0f, f.life)));
            EndMode2D();

            drawReticle();
            drawHUD();
            drawTutorialPrompt();
            drawUnlockBanner();
            break;
        }
    }
    EndDrawing();
}
