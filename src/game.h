#pragma once
#include "raylib.h"
#include <cstdint>
#include <deque>
#include <random>
#include <string>
#include <vector>

constexpr int SCREEN_W = 1280;
constexpr int SCREEN_H = 720;
constexpr float ARENA_MARGIN = 52.0f;

// Player tuning.
constexpr float PLAYER_RADIUS   = 14.0f;
constexpr float PLAYER_SPEED    = 250.0f;
constexpr float PLAYER_ACCEL    = 14.0f;
constexpr float DASH_SPEED      = 640.0f;
constexpr float DASH_O2_COST    = 10.0f;
constexpr float IFRAME_ON_HIT   = 0.7f;

// Lob drag; shared by the physics and the landing preview.
constexpr float LOB_DRAG        = 1.6f;

// Seconds a yeast cell takes to bud; shared by the sim and the bud it draws.
constexpr float BUD_PERIOD      = 12.0f;

// Biology traits. Every weapon interaction derives from these.
enum Trait {
    T_GRAMPOS   = 1 << 0,    // thick exposed cell wall
    T_GRAMNEG   = 1 << 1,    // greasy outer membrane over the wall
    T_CAPSULE   = 1 << 2,    // slime shell that blocks the immune system
    T_CATALASE  = 1 << 3,    // destroys peroxide, so oxidative burst fizzles
    T_ACIDFAST  = 1 << 4,    // waxy armor (tuberculosis)
    T_VIRUS     = 1 << 5,
    T_PROTOZOA  = 1 << 6,
    T_HELMINTH  = 1 << 7,    // worms; tough skin, eosinophil target
    T_FUNGUS    = 1 << 8,
    T_HOST      = 1 << 9,    // friendly - killing these hurts the patient
    T_BOSS      = 1 << 10,
    T_INERT     = 1 << 11,   // tutorial target
};

enum OrgId {
    O_RBC, O_EPITHELIAL, O_MACROPHAGE, O_NEUTROPHIL,
    O_MEROZOITE, O_INFECTED_RBC, O_CANDIDA, O_TRYPANOSOME,
    O_STAPH, O_ECOLI, O_MENINGO,
    O_FLU, O_PNEUMO, O_TB, O_TB_LATENT, O_ASPERGILLUS, O_KLEBSIELLA,
    O_VIBRIO, O_ROTA, O_ENTAMOEBA, O_CDIFF, O_CDIFF_SPORE,
    O_SCHISTO_EGG, O_ASCARIS_LARVA,
    O_BOSS_SCHISTO, O_BOSS_GRANULOMA, O_GRANULOMA_SHIELD, O_BOSS_ASCARIS,
    O_DEBRIS,
    O_COUNT
};

enum Shape {
    SH_BICONCAVE, SH_COCCUS, SH_DIPLOCOCCUS, SH_CLUSTER, SH_ROD, SH_COMMA,
    SH_SPIRAL, SH_SPIKED, SH_WHEEL, SH_RINGFORM, SH_BUDDING, SH_AMOEBA,
    SH_HYPHA, SH_WORM, SH_EGG, SH_LOBED, SH_BLOB, SH_GRANULOMA, SH_DEBRIS
};

enum Behavior {
    B_HOST_DRIFT, B_STATIONARY, B_CHASE, B_WOBBLE_CHASE, B_DART,
    B_KEEPAWAY_SHOOT, B_BOUNCE, B_SEEK_RBC, B_SEEK_EPITHELIAL, B_BUDDING,
    B_AMOEBA_LUNGE, B_HYPHA_GROW, B_DORMANT, B_ALLY_HUNT, B_ORBIT_PARENT,
    B_WORMBOSS, B_GRANULOMA_CORE, B_BURROWER, B_INFECTED_BURST, B_EGG
};

enum WeaponId { W_LYSOZYME, W_COMPLEMENT, W_OXBURST, W_IGG, W_EOSINOPHIL, W_COUNT };

// How a weapon delivers damage.
enum WeaponKind {
    WK_STREAM,   // held full-auto hose of fast weak bullets
    WK_BEAM,     // continuous piercing line, limited by heat instead of ammo
    WK_NOVA,     // unaimed blast ring centered on you, point-blank only
    WK_LOB,      // slow arcing blob that bursts into a lingering cloud
    WK_SEEKER,   // homing granules that steer onto hostiles
};

enum UpgradeId {
    U_ARTEMISININ, U_OSELTAMIVIR, U_ISONIAZID, U_ORS, U_EPO,
    U_ADRENALINE, U_INTERFERON, U_PYROGEN, U_ALBUMIN, U_VANCOMYCIN,
    U_COUNT
};

enum GameState { ST_TITLE, ST_PLAY, ST_UPGRADE, ST_CODEX, ST_GAMEOVER, ST_VICTORY };

// Meta::trained bit: training seen (completed or skipped). Separate from the
// per-weapon grant bits.
constexpr int TRAINED_SEEN = 1 << 15;

struct OrgDef {
    const char *name;      // plain-language name
    const char *latin;     // real binomial
    int traits;
    float hp, radius, speed, contact;
    Shape shape;
    Behavior behavior;
    Color color;
    const char *threat;    // what it does to you
    const char *counter;   // how you beat it
    const char *science;   // real biology, codex only
};

struct WeaponDef {
    const char *name;       // display name
    const char *shortName;  // rack label
    const char *tag;        // firing-mode label: AUTO / BEAM / BLAST / LOB / SEEKER
    const char *mech;       // plain-language one-liner
    WeaponKind kind;
    float dmg;        // per pellet; per second for WK_BEAM
    float cooldown;   // unused by WK_BEAM
    float speed;      // projectile speed; range for WK_BEAM
    float spreadDeg;
    float life;       // projectile lifetime
    float area;       // NOVA blast radius / LOB cloud radius / BEAM half-width
    int pellets, pierce;
};

// Plain name first, real drug name second.
struct UpgradeDef { const char *name; const char *real; const char *desc; };

// How each weapon is earned.
struct WeaponUnlock {
    int kills;          // career kills required (0 = not a kill unlock)
    int bosses;         // career bosses required (0 = not a boss unlock)
    bool viaTutorial;   // also granted by completing the relevant training stage
    const char *how;
};

extern const OrgDef ORGS[O_COUNT];
extern const WeaponDef WEAPONS[W_COUNT];
extern const UpgradeDef UPGRADES[U_COUNT];
extern const WeaponUnlock UNLOCKS[W_COUNT];
// Bounds-checked biome name lookup.
const char *biomeName(int b);

// Damage multiplier for a weapon against a trait set, plus a short reason when notable.
float effectiveness(int weapon, int traits, const char **reason);

// Effective range of a weapon.
float weaponReach(int weapon);

struct Enemy {
    int id = 0;
    OrgId type = O_MEROZOITE;
    Vector2 pos{0, 0}, vel{0, 0};
    float hp = 10, maxHp = 10, radius = 10;
    bool alive = true, hostile = true;
    float t1 = 0, t2 = 0, t3 = 0;
    bool capsuleIntact = false;
    float capsuleHp = 0;
    bool opsonized = false;
    float opsonT = 0;
    int resistTo = 0;          // bitmask of WeaponId this strain evolved against
    int targetId = -1, parentId = -1;
    float angle = 0, feedbackCd = 0;
    int seed = 0;
    bool submerged = false;
    Vector2 telegraph{0, 0};
    std::deque<Vector2> trail;
};

struct Proj {
    Vector2 pos{0, 0}, vel{0, 0};
    float dmg = 0, life = 1, radius = 4;
    int pierce = 0, weapon = 0;
    bool fromEnemy = false, toxin = false, alive = true;
    float homing = 0;      // radians per second of steering; 0 = travels straight
    bool lob = false;      // decelerates and bursts into a Field when it expires
    int targetId = -1;
};

struct Particle {
    Vector2 pos{0, 0}, vel{0, 0};
    float life = 1, maxLife = 1, radius = 3;
    Color color{255, 255, 255, 255};
};

// Expanding ring, purely cosmetic.
struct Shock {
    Vector2 pos{0, 0};
    float r0 = 0, r1 = 100, life = 0.35f, maxLife = 0.35f, thick = 5;
    Color color{255, 255, 255, 255};
};

// Lingering area effect (the IgG cloud).
struct Field {
    Vector2 pos{0, 0};
    float radius = 100, life = 4, maxLife = 4;
};

struct FloatText {
    Vector2 pos{0, 0};
    std::string text;
    float life = 1.2f;
    Color color{255, 255, 255, 255};
};

struct PendingSpawn { OrgId type; Vector2 pos{0, 0}; float timer = 0.8f; };
struct Obstacle { Vector2 pos{0, 0}; float radius = 50; };
struct Meta { int kills = 0, bosses = 0, victories = 0, trained = 0; };

class Game {
public:
    int run(bool smokeMode);
    int smokeFrames = 3600;
    int startBiome = 0;
    unsigned seed = 0;            // 0 = nondeterministic
    bool skipTutorial = false;
    bool forceTutorial = false;   // run the tutorial under --smoke

private:
    GameState state = ST_TITLE;
    GameState codexReturn = ST_PLAY;
    bool smoke = false;
    std::mt19937 rng{std::random_device{}()};
    Meta meta;

    int biome = 0, roomInBiome = 0, wave = 0, wavesInRoom = 3;
    float waveGap = 0;
    int runKills = 0, lastRunKills = 0;
    std::vector<Enemy> enemies;
    // deque: spawning mid-iteration must not invalidate held references.
    std::deque<Enemy> newborns;
    std::vector<Proj> projs;
    std::vector<Particle> parts;
    std::vector<Shock> shocks;
    std::vector<Field> fields;
    std::vector<FloatText> ftexts;
    std::vector<PendingSpawn> pending;
    std::vector<Obstacle> obstacles;
    bool seenOrg[O_COUNT] = {};
    bool haveUpg[U_COUNT] = {};
    int upChoice[3] = {0, 0, 0};
    Vector2 flowDir{0, 0};
    int nextId = 1;
    int codexPage = 0;

    // tutorial
    bool inTutorial = false;
    int tutStage = 0;
    float tutTimer = 0, tutMoved = 0;
    // True for one update after a stage begins; stage setup keys off this.
    bool tutStageFresh = false;

    // antibiotic-resistance pressure, per weapon
    float pressure[W_COUNT] = {};
    int resistantStrains = 0;

    bool weaponUnlocked[W_COUNT] = {};
    int unlockBannerW = -1;
    float unlockBannerT = 0;

    Vector2 ppos{0, 0}, pvel{0, 0};
    float php = 100, phpMax = 100;
    float o2 = 100, o2Max = 100;
    float hydration = 100;
    float fireCd = 0, dashCd = 0, invuln = 0, dashT = 0;
    WeaponId weapon = W_LYSOZYME;
    float aimAngle = 0, shake = 0;
    double gtime = 0;

    // Beam state. Heat replaces ammo: overheating locks it out until it cools.
    bool beamOn = false, beamLock = false;
    float beamHeat = 0, beamTick = 0;
    Vector2 beamEnd{0, 0};

    Vector2 reticle{0, 0};
    float hitSfxCd = 0;     // throttles impact sounds
    float wrongWeaponT = 0; // time the held weapon has been ineffective
    // Death is resolved once, at the end of the frame.
    bool pendingDeath = false;

    bool audioOk = false;
    Sound sfxShoot{}, sfxHit{}, sfxResist{}, sfxBurst{}, sfxPick{}, sfxHurt{}, sfxBossDie{},
          sfxBeam{}, sfxNova{};

    float frand(float a, float b);
    int irand(int a, int b);
    Enemy *findEnemy(int id);
    bool hasUpg(int u) const { return haveUpg[u]; }
    void play(Sound &s, float vol = 1.0f);
    void say(Vector2 pos, const char *text, Color c);

    // ---- flow (game.cpp) ----
    void loadMeta();
    void saveMeta();
    void bankRun();       // fold run kills into the career total
    void resetWorld();
    void startRun();
    void loadRoom();
    void loadTutorial();
    void updateTutorial(float dt);
    void spawnWaveNow();
    void queueSpawn(OrgId t, Vector2 pos);
    Enemy &spawnEnemy(OrgId t, Vector2 pos);
    void becomeType(Enemy &e, OrgId t);   // re-derive stats on species change
    void flushSpawns();
    Vector2 randomEdgePos();
    void pickUpgradeChoices();
    void applyUpgrade(int u);
    void onRoomCleared();
    void initAudio();
    void shutdownAudio();
    int runSmoke();

    // ---- unlocks (game.cpp) ----
    [[nodiscard]] int careerKills() const;
    void refreshUnlocks(bool announce);
    [[nodiscard]] bool unlockProgress(int w, int &have, int &need) const;
    void selectWeapon(int w);
    void cycleWeapon(int dir);

    // ---- simulation (sim.cpp) ----
    void update(float dt);
    void gatherInput(float dt, Vector2 &moveDir, Vector2 &aimPos, bool &firing, bool &dashPressed);
    void autopilot(float dt, Vector2 &moveDir, Vector2 &aimPos, bool &firing, bool &dashPressed);
    void updatePlayer(float dt, Vector2 moveDir, Vector2 aimPos, bool firing, bool dashPressed);
    void updateEnemies(float dt);
    void updateEnemy(Enemy &e, float dt);
    void updateProjectiles(float dt);
    void updateEffects(float dt);
    void updateWeapon(float dt, Vector2 aimPos, bool firing);
    void fireShot(Vector2 aimPos);
    void fireNova();
    void fireBeam(float dt, Vector2 aimPos);
    void detonateLob(Proj &p);
    void applyDamage(Enemy &e, float dmg, int weaponUsed);
    void splashDamage(Vector2 pos, float radius, float dmg, int weaponUsed, bool sparesHost);
    void killEnemy(Enemy &e);
    void hurtPlayer(float dmg);
    void hemolysis(Vector2 pos, bool fullBurst);
    void burstParticles(Vector2 pos, Color c, int n, float speed);
    void addShock(Vector2 pos, float r0, float r1, Color c, float life, float thick);
    void clampToArena(Vector2 &p, float r) const;
    void pushOutObstacles(Vector2 &p, float r) const;
    void evolveResistance(int weaponUsed);

    // ---- queries shared with the UI (sim.cpp) ----
    // Single damage-multiplier source for the HUD, the autopilot and applyDamage().
    [[nodiscard]] const Enemy *nearestHostile(Vector2 from, float *distOut = nullptr) const;
    [[nodiscard]] float ratedAgainst(int w, const Enemy &e, const char **reason = nullptr) const;
    [[nodiscard]] int bestWeaponAgainst(const Enemy &e) const;
    [[nodiscard]] float weaponCharge(int w) const;   // 0..1 rack meter

    // ---- rendering (render.cpp) ----
    // Drawing never mutates game state.
    static Rectangle upgradeCardRect(int i);   // shared by card art and hit test
    void draw();
    void drawArenaBackdrop() const;
    void drawOrganism(const Enemy &e) const;
    void drawPlayer() const;
    void drawEffects() const;
    void drawAimGuides() const;   // world space
    void drawReticle() const;     // screen space
    void drawHUD() const;
    void drawWeaponRack() const;
    void drawTargetPlate() const;
    void drawUnlockBanner() const;
    void drawArsenalPanel(int x, int y) const;
    void drawTutorialPrompt() const;
    void drawTitle() const;
    void drawUpgradeScreen() const;
    void drawCodex() const;
    void drawEndScreen(bool victory) const;
};
