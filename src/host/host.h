#pragma once
#include <cstdint>
#include <string>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "host/input.h"
#include "data/bundle.h"
#include "audio/audio.h"
#include "field/script_state.h"
#include "field/event_vm.h"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace ffsmith {

class Field;

struct Combatant { std::string name; int hp = 0, maxhp = 0, atk = 0, def = 0; bool defending = false; int spd = 8, atb = 0, mp = 0, maxmp = 0, intl = 0, mnd = 0, wpn = 4, level = 1; long exp = 0, gil = 0; int sprite = -1, frames = 0; };
struct GameMember { int charIdx = -1; int hp = 0, mp = 0; int equip[6] = {0,0,0,0,0,0}; int level = 0, exp = 0; };   // persistent party (HP/MP + equip + level/exp)
struct FloatNum { int idx = 0; bool enemy = true; int value = 0; int age = 0; bool heal = false; };  // floating battle damage/heal number
struct InvSlot    { int id = 0, count = 0; };

struct HostConfig {
    int logical_width  = 256;
    int logical_height = 176;
    int scale          = 3;
    int aspectW        = 0;   // chosen aspect ratio (0,0 = free-form resizable)
    int aspectH        = 0;
    int tick_hz        = 60;
    int max_frames     = -1;
    const char* title  = "FFSmith";
};

class Host {
public:
    explicit Host(const HostConfig& cfg);
    ~Host();
    Host(const Host&)            = delete;
    Host& operator=(const Host&) = delete;

    enum class Mode { Debug, Title, Field, Battle, Intro };
    struct DebugStart { std::string map; int img = -1, x = 0, y = 0, facing = 0; bool noclip = false; };

    bool init();
    int  run();
    bool frame();   // one loop iteration; false when quit (lets caller swap maps on warp)
    void setMap(const Texture& fb);
    void setField(Field* f, const Texture& mapImg);
    void setOverhead(const Texture& img);   // layers 1+ drawn above sprites (z-order)
    void loadChipAnim(const std::string& bundleDir);
    void checkFieldHazard();   // damage floors -> persistent party HP
    std::string awardBattleRewards();   // victory: EXP/gil + level-ups into the persistent party
    void setAnimTick(int t) { animTimer_ = t; }
    void setStartMap(const std::string& m) { startMap_ = m; }
    void setStartPos(int x, int y) { startX_ = x; startY_ = y; }   // New Game spawn (start.bin)
    void setHasSave(bool b) { hasSave_ = b; }
    void setIntroState(int n) { introState_ = n; }
    void loadIntro(const std::string& dir);
    void selfTestDamage();   // headless: walk onto a damage floor, report party HP
    void setBundleDir(const std::string& d) { bundleDir_ = d; }
    void setPlayerSprite(int img, int var) { playerImg_ = img; playerVar_ = var; }
    void setMode(Mode m) { mode_ = m; }
    void openMenu() { menuOpen_ = true; menuCursor_ = 0; menuPage_ = 0; }
    bool loadMenuData(const std::string& bundleDir);   // data/items.bin + chars.bin
    void openMenuPage(int pg) { menuOpen_ = true; menuPage_ = pg; pageCursor_ = 0; pageScroll_ = 0; pageChar_ = 0; }
    void requestSave() { saveReq_ = true; menuOpen_ = false; }
    void requestLoad() { loadReq_ = true; }
    bool consumeSaveRequest() { bool b = saveReq_; saveReq_ = false; return b; }
    bool consumeLoadRequest() { bool b = loadReq_; loadReq_ = false; return b; }
    void startBattle(int monsterId);
    void startFormationBattle(const VMEncounter& enc);  // 0x50 scripted battle
    void setRandomEncounters(bool b) { encountersOn_ = b; }
    void selfTestEncounter();    // headless: 0x50 pause -> battle -> resume
    void selfTestCutscene();     // headless: 0x68/0x69/0x32 actor moves + waits
    GameMember makeMember(int charIdx) const;   // fresh member state from chara_set
    void newGame();
    void selfTestItemUse();      // headless: damage a member, use a Potion, report
    void selfTestEquip();        // headless: swap a weapon, report stat + inventory change
    void selfTestLevel();        // headless: award EXP, report level-up + HP growth
    void selfTestJobStat();      // headless: verify job-derived battle attributes (SetJobStatus)
    void selfTestRevive();       // headless: KO a member, use Phoenix Down
    void selfTestMenu();         // headless: exercise the title menu dispatch
    const std::vector<GameMember>& gameParty() const { return gameParty_; }
    void setGameParty(std::vector<GameMember> p) { gameParty_ = std::move(p); commitActiveParty(); }
    // FFD dual party: two sides (0 = Warriors of Light, 1 = Warriors of Darkness),
    // up to 5 active members each (libjniproxy: side selector @+0x21424, member-id
    // arrays @+0x21428, GetPartyMemberID).  gameParty_ is the *active* side's working
    // copy; parties_[partySide_] is kept in sync via commitActiveParty().
    int  partySide() const { return partySide_; }
    void commitActiveParty() { if (partySide_ >= 0 && partySide_ < 2) parties_[partySide_] = gameParty_; }
    const std::array<std::vector<GameMember>, 2>& parties() { commitActiveParty(); return parties_; }
    void setParties(const std::array<std::vector<GameMember>, 2>& p, int side) {
        parties_ = p; partySide_ = (side == 1) ? 1 : 0; gameParty_ = parties_[partySide_]; }
    void switchSide();                              // swap active Light<->Dark party
    static const int kPartyMax = 5;                 // active members per side (FFD)
    const std::vector<InvSlot>& inventory() const { return inventory_; }
    void setInventory(std::vector<InvSlot> v) { inventory_ = std::move(v); }
    int  gil() const { return gil_; }
    void setGil(int g) { gil_ = g; }
    ScriptState&       scriptState()       { return scriptState_; }
    const ScriptState& scriptState() const { return scriptState_; }
    const VMEnv& vmEnv() const { return vmEnv_; }
    void wireScriptEnv();        // build vmEnv_ hooks (items / party / RNG / CallEvent)
    void loadCommonEvents(const std::string& bundleDir);   // map 10000 = shared event pool
    int  simBattle(int monsterId);   // headless: auto-attack to the end (verification)
    void debugOpenMagic();           // debug: open the Magic spell menu for a screenshot
    bool loadTitle(const std::string& bundleDir);   // ui/title.tex
    void setDebugData(std::vector<std::string> maps, std::vector<int> sprites);
    void debugSelectMap(const std::string& key);
    void setMapKey(const std::string& key) { mapKey_ = key; }   // for the HUD
    int  fadeGen() const { return fadeGen_; }                   // snapshot before a warp; compare after
    // After a door/map warp the script's fade-IN half never runs (the warp is
    // handled outside the VM), so a fade-OUT would leave the screen stuck black.
    // Recover by ramping back in -- but only when the destination issued no fade
    // of its own (caller checks fadeGen()), so real cutscene blacks are respected.
    void fadeInAfterWarp() { if (fadeAlpha_ > 0 && fadeTarget_ != 0) { fadeTarget_ = 0; if (fadeStep_ <= 0) fadeStep_ = 16; } }
    void setViewFlags(bool overlay, bool hud) { overlayOn_ = overlay; hudOn_ = hud; }
    bool consumeDebugStart(DebugStart& out);
    bool shotField(const std::string& path);   // render one field frame, read pixels, save .tex
    bool loadText(const std::string& bundleDir, int bank);   // load text/msg{bank}.bin + font

    const InputState& input() const { return input_; }
    SDL_Renderer* renderer() const { return renderer_; }

private:
    void pumpEvents();
    void stepInput();
    void update(double dt);
    void render();
    bool ensureMapTexture(const Texture& img);
    SDL_Texture* spriteTex(int img, int var, int& w, int& h);  // cached fldchr sheet
    SDL_Texture* monTex(int group, int variant, int& w, int& h); // cached tex/mon{group}_{variant}.tex (single image)
    bool drawSprite(int img, int var, int facing, int animCol, int lx, int ly, int tile);
    void updateMenu(const InputState& in);
    void renderTitle();
    void renderIntro();
    void renderMenu(int vw, int vh);
    void renderItemPage(int px, int py, int pw, int ph);
    void renderCharPage(int px, int py, int pw, int ph, bool status);
    void updateBattle(const InputState& in);
    void renderBattle();
    int  firstLiving(int from) const;
    bool partyAlive() const;
    int  firstLivingEnemy(int from) const;
    bool enemiesAlive() const;
    void doEnemyAttack();
    void pickNextActor();
    void beginNextTurn();
    void buildSpellList();
    void castOn(int targetIsEnemy, int idx);
    int  physDamage(const Combatant& a, const Combatant& d) const;
    void endBattle();
    void useItem(int invIdx);
    void buildEquipCandidates(int slot);
    void swapEquip(int slot, int pick);
    bool slotAcceptsItem(int slot, const Item& it) const;
    void addInventory(int id, int count = 1);
    int  curMember() const;
    int  memberMaxHp(int i) const;
    int  memberMaxMp(int i) const;
    int  memberJobPct(int i, bool mp) const;        // per-job HP/MP growth percent
    int  memberStat(int i, int which) const;        // job-derived attribute: 0=STR 1=SPD 2=VIT 3=INT 4=MND
    void updateDebug(const InputState& in);
    void renderDebug();
    // FFD GameClass::DrawWindow (libjniproxy 153133-153153): 3-stop vertical blue
    // gradient (top 63,69,134 -> mid 42,43,102 -> bottom 75,78,122) + light frame.
    void drawWindow(int x, int y, int w, int h, int alpha = 160);
    void drawGauge(int x, int y, int w, int h, int cur, int maxv, bool mp);  // HP/MP bar (green->yellow->red / blue)
    void spawnFloat(int idx, bool enemy, int value, bool heal = false);      // floating battle number
    // Display: resize the window to a chosen aspect ratio (w:h).  The UI reflows
    // from the live window size, so this just resizes; 0,0 = free-form.
    void setAspect(int w, int h);
    void renderConfigPage(int px, int py, int pw, int ph);
    void renderFormationPage(int px, int py, int pw, int ph);
    bool charInAnyParty(int charIdx);               // already placed in Light or Dark?
    void drawText(int x, int y, const std::string& s, int maxChars, uint8_t r, uint8_t g, uint8_t b);

    HostConfig    cfg_;
    SDL_Window*   window_     = nullptr;
    SDL_Renderer* renderer_   = nullptr;
    SDL_Texture*  map_tex_    = nullptr;
    bool          has_map_    = false;
    Field*        field_      = nullptr;
    int           mapW_ = 0, mapH_ = 0;
    std::string   bundleDir_;
    AudioManager  audio_;
    int           titleBgm_ = 18;   // BGM for Title/Intro (opening theme; tweakable)
    void          updateAudio();    // per-frame BGM-by-mode poll
    int           playerImg_ = -1, playerVar_ = 0;
    int  camPX_ = 0, camPY_ = 0; bool camInit_ = false;   // smooth camera (resets per map)
    // 0x2a screen fade — Host-owned so it persists across warps (out -> warp -> in).
    int  fadeAlpha_ = 0, fadeTarget_ = 0, fadeStep_ = 0;
    int  fadeGen_ = 0;                                   // bumped on every setFade; lets a warp tell if the destination ran its own fade
    uint8_t fadeR_ = 0, fadeG_ = 0, fadeB_ = 0;
    void clearFade() { fadeAlpha_ = fadeTarget_ = fadeStep_ = 0; }
    std::unordered_map<int, SDL_Texture*> sprites_;   // key = img*100+var
    std::unordered_map<int, SDL_Texture*> monSprites_;// key = monster sprite set id
    int battleAnim_ = 0;                              // battle idle-animation frame timer
    std::unordered_map<int, SDL_Texture*> sheets_;    // tilesheets for anim overdraw (mc*100+var)
    std::unordered_map<int, std::unordered_map<int, ChipAnim>> chipAnim_;   // mc -> inner -> anim
    std::unordered_map<int, std::unordered_map<int, int>> chipFloor_;       // mc -> inner -> floorAttr
    std::unordered_set<int> damageCells_;   // cell indices that hurt the party
    int lastCell_ = -1;
    SDL_Texture* overhead_tex_ = nullptr;
    LevelTable levels_;
    std::unordered_map<int, SpriteGeo> spriteGeo_;
    bool rewardsGiven_ = false;
    struct AnimCell { int idx, mc, var, baseInner, type, frames, speed; };
    std::vector<AnimCell> animCells_;
    bool animDirty_ = true;
    int animTimer_ = 0;
    void buildAnimCells();
    SDL_Texture* slotSheet(int mc, int var, int& w, int& h);
    SDL_Texture*  fontTex_ = nullptr;
    int           fcw_ = 0, fch_ = 0, fcols_ = 0, ffirst_ = 32;
    std::unordered_map<int, std::string> messages_;
    Mode          mode_ = Mode::Field;
    bool          menuOpen_ = false;
    int           menuCursor_ = 0;
    int           blink_ = 0;
    int           titleSel_ = 0;
    std::string   startMap_;
    bool          hasSave_ = false;
    int           introState_ = 0;
    std::string   introProl_, introChap_;
    SDL_Texture*  titleTex_ = nullptr;
    int           titleW_ = 0, titleH_ = 0;
    std::vector<std::string> dbgMaps_;
    std::vector<int> dbgSprites_;
    int  dbgRow_ = 0, dbgMapIdx_ = 0, dbgSprIdx_ = 0;
    int  dbgX_ = 0, dbgY_ = 0, dbgFacing_ = 0;
    bool dbgNoclip_ = false, dbgOverlay_ = false, dbgHud_ = true, dbgStart_ = false;
    int  dbgScale_ = 3;
    bool overlayOn_ = false, hudOn_ = false;
    bool saveReq_ = false, loadReq_ = false;
    std::string mapKey_;
    std::unordered_map<int, Item> items_;
    std::vector<int> itemIds_;
    std::vector<CharRec> chars_;
    int menuPage_ = 0, pageCursor_ = 0, pageScroll_ = 0, pageChar_ = 0;
    int configCursor_ = 0;                          // Config page (display settings) cursor
    int formCursor_ = 0, formHeld_ = -1, formMode_ = 0, formPick_ = 0;  // Formation page state
    bool textShadow_ = true;                         // 1px drop-shadow under text (FF look)
    int winOpacity_ = 230;                           // global window opacity (scales drawWindow alpha)
    int winColorIdx_ = 0;                            // window colour preset (kWinColors)
    int msgSpeed_ = 1;                               // 0=instant 1=normal 2=slow (typewriter reveal)
    int msgReveal_ = 0, msgRevealId_ = -2;           // typewriter: chars shown for the current message
    int dbgAspect_ = 0;                             // Debug launcher: aspect preset index (kAspects)
    bool dbgFullscreen_ = false;                    // Debug launcher: desktop-fullscreen toggle
    std::vector<Monster> monsters_;
    // Scripted battles (ScriptEncount) + formation data (encounters.bin).
    std::unordered_map<int, std::unordered_map<int, Formation>> encounters_;
    bool scriptBattle_ = false;      // current battle was script-launched
    bool battleNoEscape_ = false;    // formation no_escape -> Run always fails
    int  battleOutcome_ = 1;         // 1 win, 0 loss, 2 escaped (GetReferenceBattle t3)
    int  lastBattleResult_ = 0;      // exposed to scripts via env.battleRef
    bool encountersOn_ = false;      // --encounters: random-encounter stepping (approx roll)
    int  lastEncCell_ = -1;
    void maybeRandomEncounter();
    int  currentBank() const;        // story bank = map group ("g%d" of mapKey_)
    std::vector<Combatant> enemies_;
    int target_ = 0, enemyActor_ = 0;
    std::vector<Combatant> party_;
    int btlPhase_ = 0, btlCmd_ = 0, btlMember_ = 0;
    std::vector<GameMember> gameParty_;             // ACTIVE side's working party (== parties_[partySide_])
    std::array<std::vector<GameMember>, 2> parties_;// [0]=Light, [1]=Dark (each <= kPartyMax)
    int partySide_ = 0;                             // which party is active (walking/battle)
    std::vector<FloatNum> floats_;                  // floating damage/heal numbers (battle)
    ScriptState scriptState_;      // script flags/vars (event VM; saved in FSAV v5)
    FfMap commonEvents_;           // map 10000: CallEvent (0x66) routine pool
    int startX_ = -1, startY_ = -1;
    VMEnv vmEnv_;
    std::string menuMsg_;          // transient item-use feedback
    int equipSlot_ = 0, equipSub_ = 0, equipPick_ = 0;   // equip page: slot cursor / submenu / candidate cursor
    std::vector<int> equipCand_;   // inventory indices that fit the selected slot
    std::vector<InvSlot> inventory_;
    int gil_ = 0;
    bool curIsEnemy_ = false;
    int  curIdx_ = 0;
    std::vector<Spell> spells_;
    std::unordered_map<int, JobGrowth> jobs_;       // job_id -> growth multipliers
    std::vector<int> spellList_;
    int btlSpellSel_ = 0, pendingSpell_ = -1;
    std::string btlMsg_;
    SDL_Texture* btlbgTex_ = nullptr;
    InputState    input_;
    uint32_t      raw_held_   = 0;
    uint32_t      held_prev_  = 0;
    bool          running_    = false;
    uint64_t      tick_count_ = 0;
    bool          started_ = false;
    uint64_t      prevCtr_ = 0;
    double        acc_ = 0.0;
};

}  // namespace ffsmith
