# Pathogen Protocol

You have been shrunk to the size of a cell and injected into a dying patient. Clear the infection one
organ system at a time: bloodstream, lungs, gut.

Pathogen Protocol is a top-down twin-stick roguelite written in C++ with raylib. The enemies are real
pathogens with weaknesses drawn from real microbiology and immunology, so an enemy's color tells you
which weapon works on it.

## Play

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
./build/pathogen_protocol
```

raylib is downloaded automatically during the build, so there is nothing else to install.

| | |
|---|---|
| Move | WASD |
| Aim | mouse |
| Fire | left mouse |
| Dash | SPACE (costs oxygen) |
| Switch weapon | 1–5, Q/E, or mouse wheel |
| Field guide | TAB |

## The one rule

Bacteria are drawn in the color they stain under a microscope: purple for Gram-positive, pink for
Gram-negative, red for acid-fast. The other groups each get a color of their own. The color tells you
which weapon to use.

| Color | Use |
|---|---|
| Purple | **1** — Lysozyme Stream |
| Pink | **2** — Complement Beam |
| White halo | **4** — IgG Tag, to strip the shell first |
| Red | Needs the Armor Breaker drug |
| Tan | **5** — Eosinophil Seekers |
| Teal / green | Parasites and viruses; the wall-breaking guns barely scratch them |

The game also teaches this table as you play. The crosshair turns red when you aim the wrong weapon
at something, and the strip above a weapon's slot turns green when it is the right one. If you keep
holding the wrong weapon while an enemy closes in, the game shows which key to press.

When a shot does little damage, the enemy displays the reason, e.g. "outer membrane blocks it",
"it eats peroxide", or "waxy armor".

Enemy shapes are taken from the real organisms too, which makes targets easy to tell apart: staph
grows in grape-like clumps, cholera is a comma with a tail, rotavirus is a wheel, lung mold is a set
of branching threads, and malaria is a ring.

## Weapons

Each of the five weapons fires in a different way, so switching weapons changes how you move and aim
as well as how much damage you do.

| | Weapon | How it plays | Unlocked by |
|---|---|---|---|
| **1** | Lysozyme Stream | Hold the trigger for a fast, weak, endless stream. | you start with it |
| **2** | Complement Beam | A solid line that pierces everything it touches. It uses no ammo, but it overheats and cuts out until it cools. | finish training, or 30 career kills |
| **3** | Oxidative Burst | A shockwave centered on you. It cannot be aimed, so you have to walk into range. It does not hurt your own cells. | 200 career kills |
| **4** | IgG Tag Grenade | A slow arc that you have to lead. It bursts into a cloud that strips shells and marks everything inside for +60% damage. | finish training, or 80 career kills |
| **5** | Eosinophil Seekers | Homing granules that chase parasites and worms and never your own cells. | beat any boss |

Each weapon draws its own aim guide: the burst shows its radius around you, the grenade shows where
the cloud will land, and the beam shows its line. The beam's heat is the only ammo-like resource in
the game.

### Unlocks

Locked weapons appear on the rack from the first run with a padlock, the requirement and a progress
bar. Pressing a locked weapon's key shows what you still need
(`OX BURST LOCKED - 200 career kills (142/200)`), and a weapon becomes usable the moment you reach its
threshold, even mid-fight.

Unlocks add options and do not raise any stats. The only thing that carries over between runs is
which weapons you own.

Training gives you Complement and IgG as it teaches them. If you skip training, you do not get them
for free and have to earn them through kills.

### Resistance

Every kill with a weapon fills the meter under its slot. Once the meter passes 100%, every enemy that
spawns is a resistant strain, marked with orange spikes, that ignores 70% of that weapon's damage.
The meter drains while you use other weapons, so rotating weapons keeps resistance down.

This models antibiotic resistance, which arises the same way: overusing one drug selects for the
strains it cannot kill.

## The biology

Enemy behavior is based on how each pathogen behaves in the body:

| Enemy | What it does to you | What that is |
|---|---|---|
| Malaria parasite | Hunts your red cells, gets inside, bursts them on a timer | Blood-stage malaria; the parasites released reinfect more cells |
| Red blood cells | Every one that dies, including to your own stray shots, shrinks your oxygen bar until you take the Blood Booster | Anemia; real bone marrow replaces red cells, but slowly |
| Blood yeast | Drifts with the blood and buds off copies of itself if you leave it alone | Candida in the bloodstream; yeast multiply by budding |
| Shapeshifter | Flashes white during moments where almost nothing hurts it | Sleeping sickness changing its coat, which is why there is no vaccine for it |
| Staph | Takes only a quarter damage from the Oxidative Burst | It makes catalase, which breaks down the peroxide in the burst. In reality this only protects it when the burst is already weak |
| E. coli | Darts in bursts, ignores lysozyme | A greasy outer membrane over the wall; part of that layer is the toxin behind septic shock |
| Flu | Can only multiply inside your lung cells, which burst when it is done | Viruses must hijack a host cell to copy themselves. Real flu buds out of the cell, which then dies |
| Pneumococcus | Shrugs off damage until IgG strips its shell | The slime capsule, which is also what the vaccine targets |
| TB | Armored; 30% of your "kills" go dormant and wake up later; turns your own macrophage ally hostile on contact | Waxy armor, latent TB, and the immune cell sent to eat it becoming its hiding place |
| Lung mold | Grows new branches instead of chasing you | Mold spreads as a network of threads |
| Cholera | Hangs back and fires toxin that drains your water | Cholera kills by dehydration |
| C. diff | Leaves behind a spore that hatches into a new one | Spores survive hand sanitizer, which is why hospitals struggle to get rid of it |
| Gut amoeba | Crawls, then lunges | It moves by pushing out an arm and flowing into it; its name means tissue-dissolving |
| Granuloma boss | Live bacteria walled inside a shell of orbiting immune cells | Your body sealing off what it cannot kill, which also keeps it alive |

The weapons are based on real immune mechanisms. Lysozyme cuts cell walls, so it only works where the
wall is exposed. Complement punches holes in membranes, and a capsule blocks it. The oxidative burst is
similar to bleach, and your own cells survive it because they carry enzymes that break it down. IgG
does little damage itself and instead marks targets for the other weapons. Eosinophils are the body's
main defense against worms and do very little against a virus.

The drugs are modeled on their real mechanisms: artemisinin kills blood-stage malaria, oseltamivir
reduces how much virus escapes each infected cell, isoniazid stops TB building its waxy armor, and
rehydration salts counter cholera through a transporter the toxin leaves working. Fever raises your
fire rate but slows oxygen recovery, because a fever raises the body's oxygen demand. Upgrade choices always include the drug for
the organ you are in until you take it, so the Armor Breaker is always on offer in the lungs.

### Where the game bends the biology

The biology is simplified where that made the game play better, and none of it is medical advice.
The main liberties are:

- **Lysozyme against staph.** Lysozyme is the anti-Gram-positive weapon because it attacks an exposed
  cell wall, but real *Staphylococcus aureus* chemically modifies its wall and resists lysozyme.
- **Catalase.** In a real infection catalase mostly matters when the oxidative burst is already weak,
  as in chronic granulomatous disease. A healthy burst kills staph.
- **Flu bursting cells.** Real influenza buds out of the cell surface, and the cell dies afterward.
- **Parasite counts.** An infected red cell releases four parasites here; the real number is 16 to 32.
- **IgG stripping shells.** Real antibody coats a capsule and flags the bacterium for other defenses.
- **Weapons against protozoa.** Eosinophil Seekers and lysozyme both damage protozoa so that every
  enemy can be fought with the weapons you have. Real eosinophils mainly target worms, and lysozyme
  does nothing to a protozoan.
- **Colors.** Only the bacteria have real stain colors. The colors for viruses, parasites, worms and
  fungi are arbitrary.
- **Resistance.** The resistance meter applies the idea of antibiotic resistance to immune weapons.

## How the field guide works

During a fight, the game shows two things about an enemy: what it does to you and how to kill it.
The species names, mechanisms and explanations are in the field guide on TAB, so technical terms like
*histolytica* never appear mid-fight. Upgrades follow the same pattern: the card says
**Armor Breaker** in large letters with *isoniazid* in small text underneath.

You can play the whole game knowing only the colors, or read every entry.

---

## For developers

<details>
<summary>Architecture, testing, and design decisions</summary>

### Structure

```
pathogen-protocol/
├── src/
│   ├── game.h            # Traits, enums, entity structs, the Game class
│   ├── vecmath.h         # Header-only 2D helpers shared by sim and render
│   ├── content.cpp       # Organism table, weapons, drugs, effectiveness(), weaponReach()
│   ├── sim.cpp           # Input, weapons, enemy behavior, damage. Draws nothing
│   ├── render.cpp        # Every draw* method, all const
│   ├── game.cpp          # Run flow, persistence, audio, unlocks, tutorial, main loop
│   └── main.cpp          # Argument parsing
├── tests/
│   └── test_rules.cpp    # Unit tests over the pure rules
└── CMakeLists.txt
```

`content.cpp` builds as a separate `pp_content` static library so the tests can link the biology
without the game loop.

Built and tested on macOS (Apple Silicon) and Linux via CMake 3.16+. For editor tooling,
`CMAKE_EXPORT_COMPILE_COMMANDS` is on, so run `ln -sf build/compile_commands.json .` once and clangd
will resolve raylib and the `src/` include path.

### Design decisions

- **Top-down 2D.** Swarms attack from every side, and a top-down view keeps that readable. It also
  avoids a 3D asset pipeline for a solo project.
- **Traits instead of special cases.** Effectiveness comes from a trait bitmask (`T_GRAMNEG`,
  `T_CAPSULE`, `T_CATALASE`, `T_ACIDFAST`, and so on) through a single `effectiveness()`, so adding
  an organism means describing its biology and writing no combat code.
- **One damage calculation.** `ratedAgainst()` is called by the HUD, the autopilot and the damage
  code, so the rating shown to the player is the multiplier that gets applied.
- **Simulation and drawing are separate.** The simulation never draws, and every `draw*` method on
  `Game` is `const`, which lets headless mode be a flag on the same code path.
- **Deferred death.** `hurtPlayer` sets `pendingDeath` instead of switching state mid-frame, so the
  enemy loop always runs to completion and kills cannot land after the run is banked.

### Tests

```sh
ctest --test-dir build --output-on-failure
```

`tests/test_rules.cpp` covers the pure rules in `content.cpp`: the color rule, that every notable
multiplier returns a plain-language reason, that no weapon is rated good against host tissue, that
every organism is killable by something, that no data table has a missing row, and that the grenade's
landing marker matches the physics that throws it. It links `content.cpp` alone, with no window and
no game loop.

The game can also play itself with no window, display server, or audio:

```sh
./build/pathogen_protocol --smoke                    # 60s autoplay, prints a summary
./build/pathogen_protocol --smoke --frames 72000     # 20 min of simulated play
./build/pathogen_protocol --smoke --start-biome 2    # jump to the Gut to test the finale
./build/pathogen_protocol --smoke --tutorial         # verify the tutorial advances and exits
./build/pathogen_protocol --smoke --seed 7           # reproducible; replay any failure exactly
./build/pathogen_protocol --skip-tutorial            # windowed, straight into a run
```

The autopilot picks the most effective unlocked weapon for its nearest target and closes to within
that weapon's reach, which for the grenade means integrating its drag instead of multiplying speed by
lifetime. A first simulated run is therefore fought with lysozyme alone. The run exits 1 with
`SMOKE FAIL` if any room goes 150 seconds without clearing, which catches soft-locks from
self-replicating enemies. `--smoke` never touches the save file at `~/.pathogen_protocol_save`.

</details>

## License

zlib, the same license raylib uses. See [LICENSE](LICENSE).

Built with [raylib](https://github.com/raysan5/raylib).
