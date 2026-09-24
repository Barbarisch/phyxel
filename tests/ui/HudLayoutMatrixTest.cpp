// HudLayoutMatrixTest.cpp -- the standing UI-overlap gate (user, 2026-09-24: "we need a
// specialized test that will always check for UI element overlaps ... i dont want to have to
// create debug sessions every time a new ui element is added which breaks everything else").
//
// What it does, headless (no device): loads the SHIPPED HUD (resources/ui/default_hud.json)
// and every shipped data screen into a UISystem, feeds the HUD the same per-frame binds the
// game does - every `visibleWhen` state key set from a bit pattern, every repeater list filled
// with realistic rows, every text bind given long text - applies the modal-overlay rule, and
// runs the engine's own layout lint (UILayoutLint.h: panel-vs-panel overlap + child overflow,
// the same code the test API's ui_lint uses on the running game). Text is measured with the
// real font (metrics-only load), so wrapping-driven growth counts.
//
// The matrix: EVERY combination of the HUD's state keys (2^N, N read from the JSON, so a new
// gated panel is covered the day it is added) at each window size a player is likely to use.
// A defect prints the size, the exact state that produced it and the lint message, so the fix
// is a JSON edit, not a debug session.
//
// Exclusions are the ONLY hand-maintained part and each carries its reason: a pair of keys the
// game cannot raise together by construction (the providers say so). Keep this list short.

#include <gtest/gtest.h>

#include "ui/BitmapFont.h"
#include "ui/HudDataContext.h"
#include "ui/MenuDefinition.h"
#include "ui/UILayoutLint.h"
#include "ui/UISystem.h"
#include "ui/UIWidget.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

using namespace Phyxel::UI;
using json = nlohmann::json;

namespace {

const std::vector<glm::ivec2> kSizes = {
    {1280, 720}, {1366, 768}, {1536, 960}, {1920, 1080}, {2560, 1440}};

// Pairs the game never raises together, by construction. Cite the provider.
// dialogue.active is `isActive() && !isAIConversation()` (Ravenmere.cpp / scaffold), so a tree
// dialogue and an AI conversation are exclusive; dialogue.waiting is a sub-state of active.
// (Conflicts the HUD resolves ITSELF - the spellbook over the combat hint, the journal over a
// dialogue box, the creative hotbar over the RPG bars - are written as "a && !b" gates in the
// JSON, which this test evaluates exactly as the engine does. Nothing about them lives here.)
const std::vector<std::pair<std::string, std::string>> kExclusive = {
    {"dialogue.active", "dialogue.aiActive"},
    {"dialogue.aiActive", "dialogue.waiting"},
};

struct ShippedHud {
    json hud;
    std::vector<std::string> stateKeys;   // every visibleWhen key, '!' stripped, sorted unique
    std::vector<std::string> rootKeys;    // keys that gate a ROOT panel (enumerated exhaustively)
    std::vector<std::string> childKeys;   // keys that only gate elements inside a panel (swept all-on / all-off)
    std::vector<std::string> listKeys;    // every repeater bind
    std::vector<std::string> textKeys;    // every scalar bind

    static ShippedHud load() {
        ShippedHud h;
        std::ifstream f("resources/ui/default_hud.json");
        EXPECT_TRUE(f.good()) << "run from the repo root";
        f >> h.hud;
        std::set<std::string> states, roots, lists, texts;
        for (const auto& p : h.hud)
            if (p.is_object() && p.contains("visibleWhen") && p["visibleWhen"].is_string())
                for (const auto& k : visibleWhenKeys(p["visibleWhen"].get<std::string>())) roots.insert(k);
        std::function<void(const json&)> walk = [&](const json& n) {
            if (n.is_object()) {
                if (n.contains("visibleWhen") && n["visibleWhen"].is_string())
                    for (const auto& k : visibleWhenKeys(n["visibleWhen"].get<std::string>())) states.insert(k);
                if (n.value("type", "") == "repeater") {
                    if (n.contains("bind")) lists.insert(n["bind"].get<std::string>());
                } else if (n.contains("bind") && n["bind"].is_string()) {
                    texts.insert(n["bind"].get<std::string>());
                }
                for (const auto& [k, v] : n.items()) walk(v);
            } else if (n.is_array()) {
                for (const auto& v : n) walk(v);
            }
        };
        walk(h.hud);
        h.stateKeys.assign(states.begin(), states.end());
        for (const auto& k : h.stateKeys) (roots.count(k) ? h.rootKeys : h.childKeys).push_back(k);
        h.listKeys.assign(lists.begin(), lists.end());
        h.textKeys.assign(texts.begin(), texts.end());
        return h;
    }
};

// The longest text each scalar bind produces in the shipped game. A panel that auto-sizes to
// its text can only be judged against what the game really puts there; nonsense-long text in
// a one-line hint would flag every autosized panel. Unbounded providers (a dialogue line, the
// AI transcript) get genuinely long text - the layout must survive them.
std::string longestText(const std::string& k) {
    if (k == "dialogue.text")
        return "New face. Mind the Reeve - he has got no patience left for strangers since the children "
               "went missing, and the whole town has been holding its breath for a week now; if you mean "
               "to help, start at the town hall and do not dawdle, because the new moon is three nights off.";
    if (k == "dialogue.aiHistory") {
        std::string h;
        for (int i = 0; i < 14; ++i)
            h += (i % 2 ? "You: " : "Wynn: ") + std::string("The barrow lies north past the farm, beyond the "
                 "tracks the wolves left; nobody who went in has come back to tell of it. ") + std::to_string(i) + "\n";
        return h;
    }
    if (k == "dialogue.speaker" || k == "dialogue.aiSpeaker") return "Reeve Aldric of Ravenmere";
    if (k == "combat.hintText")      return "Your turn: click an enemy to attack, the ground to move";
    if (k == "combat.budgetText")    return "Action:Y  Bonus:Y  Move:9.1";
    if (k == "combat.hitChanceText") return "Hit chance 65% (AC 14, +5 to hit)";
    if (k == "combat.roundText")     return "COMBAT - Round 12";
    if (k == "combat.turnLabel")     return "npc_AlphaWolf's TURN";
    if (k == "countdown.text")       return "Reach the farm  4:59.9";   // "<label>  m:ss.s" (Ravenmere.cpp)
    if (k == "player.goldText")      return "12345 gp";
    if (k == "player.xpText")        return "Lv 20  -  355000 / 355000 XP";
    if (k == "player.health")        return "100/100";
    if (k == "item.title")           return "Hollow Relic of the Barrow Priest";
    if (k == "item.detail")          return "A relic taken from the crypt; the priest will want it back.";
    if (k == "item.rowText")         return "> npc_AlphaWolf [15] 18/18";
    if (k == "item.label")           return "Companion";
    if (k == "item.count")           return "99";
    if (k == "item.icon")            return "sword";
    return "The quick brown fox jumps over the lazy dog";
}

// A row generator: realistic, LONG content so wrapping shows.
// Each shipped list provider's row, at its longest real content (the shell's providers in
// tools/create_project.py, PartyHud.cpp, the Ravenmere content): the bars carry a spell name
// with a charge count and its icon (the label draws only when the PNG is missing), the party
// row is "<name>   <hp> / <max>", the initiative row "> <entityId> [roll]  hp/max", the
// objective "[ ] <title>", the choice "[n] <text>". A list this file does not know gets the
// generic long rows below.
std::vector<HudRecord> rows(const std::string& key, int n) {
    std::vector<HudRecord> out;
    for (int i = 0; i < n; ++i) {
        HudRecord r;
        const std::string idx = std::to_string(i + 1);
        if (key == "actionbar" || key == "spellbook") {
            r.texts["label"]   = "Mass Healing Word (3)";                     // longest spell name + charges
            r.texts["icon"]    = "resources/ui/icons/spells/mass_healing_word.png";
            r.texts["tooltip"] = "Mass Healing Word (3)\nLevel 3 evocation";
            r.texts["payload"] = "spell:mass_healing_word";
            r.floats["enabled"] = 1.0f;
            out.push_back(r);
            continue;
        }
        if (key == "hotbar") {   // the editor's creative bar: a material icon + a count, no label
            r.texts["icon"]  = "resources/textures/source/cobblestone_top.png";
            r.texts["count"] = "64";
            r.floats["selected"] = i == 0 ? 1.0f : 0.0f;
            out.push_back(r);
            continue;
        }
        if (key == "party") {
            r.texts["name"]       = "Old Moll";                  // longest companion name (Ravenmere)
            r.texts["hpText"]     = "340 / 340";                 // a level-20 sheet
            r.texts["statusText"] = "";
            r.texts["rowText"]    = "Old Moll   340 / 340";
            r.floats["hpPercent"] = 100.0f;
            out.push_back(r);
            continue;
        }
        if (key == "combat.turn_order") {
            r.texts["name"]  = "npc_HollowPriest";
            r.texts["label"] = std::string(i == 0 ? "> " : "  ") + "npc_HollowPriest [15]  340/340";
            out.push_back(r);
            continue;
        }
        if (key == "objectives") {
            r.texts["label"] = "[ ] Ask Priestess Maera what she knows of the Hollow Barrow";   // longest title
            out.push_back(r);
            continue;
        }
        if (key == "dialogue.choices") {
            r.texts["label"] = "[" + idx + "] Release the poacher Wren to me. I need a tracker more than you need an example.";
            out.push_back(r);
            continue;
        }
        if (key == "compass.poi") {
            r.texts["label"] = "Town of Ravenmere";
            r.floats["bearing"] = 30.0f * i;
            out.push_back(r);
            continue;
        }
        r.texts["title"]   = "Speak with Reeve Aldric about the missing children and the barrow " + idx;
        r.texts["detail"]  = "The Reeve holds court at the town hall; the tracks lead north past Hollin Farm " + idx;
        r.texts["text"]    = "[ ] Find the missing children of Ravenmere before the new moon rises " + idx;
        r.texts["label"]   = "Companion the " + idx + "th";
        r.texts["name"]    = "npc_AlphaWolf_" + idx;
        r.texts["hp"]      = "100/100";
        r.texts["status"]  = "Away";
        r.texts["rowText"] = "> npc_AlphaWolf [15] 18/18 " + idx;
        r.texts["icon"]    = "sword";
        r.texts["count"]   = "12";
        r.texts["action"]  = "attack";
        r.texts["choice"]  = idx + ". I will look for the children, whatever it takes - tell me everything you know";
        r.floats["bearing"] = 30.0f * i;
        r.floats["percent"] = 50.0f;
        out.push_back(r);
    }
    (void)key;
    return out;
}

struct Matrix : ::testing::Test {
    ShippedHud shipped = ShippedHud::load();
    BitmapFont font;
    UITheme theme;
    void SetUp() override {
        ASSERT_TRUE(font.initializeTTF(nullptr, "resources/fonts/JetBrainsMonoNerdFontMono-Regular.ttf", 48.0f))
            << "metrics-only font load (repo root cwd)";
        ASSERT_FALSE(shipped.stateKeys.empty());
    }

    // Bind a state pattern + full lists + long texts into `ctx`.
    void bind(HudDataContext& ctx, uint32_t pattern, bool childrenOn) {
        for (size_t i = 0; i < shipped.rootKeys.size(); ++i) {
            const bool on = (pattern >> i) & 1u;
            ctx.setFloat(shipped.rootKeys[i], [on]() { return on ? 1.0f : 0.0f; });
        }
        for (const auto& k : shipped.childKeys)
            ctx.setFloat(k, [childrenOn]() { return childrenOn ? 1.0f : 0.0f; });
        for (const auto& k : shipped.listKeys) {
            const int n = (k.find("actionbar") != std::string::npos) ? 12
                        : (k.find("hotbar") != std::string::npos) ? 9
                        : (k.find("spellbook") != std::string::npos) ? 8
                        : (k.find("turn_order") != std::string::npos) ? 6
                        : (k.find("choices") != std::string::npos) ? 4
                        : (k.find("party") != std::string::npos) ? 4 : 6;
            ctx.setList(k, [k, n]() { return rows(k, n); });
        }
        for (const auto& k : shipped.textKeys) {
            ctx.setText(k, [k]() { return longestText(k); });
            ctx.setFloat(k, []() { return 0.5f; });   // progress bars read floats
        }
    }

    bool excluded(uint32_t pattern) const {
        for (const auto& [a, b] : kExclusive) {
            auto ia = std::find(shipped.rootKeys.begin(), shipped.rootKeys.end(), a);
            auto ib = std::find(shipped.rootKeys.begin(), shipped.rootKeys.end(), b);
            if (ia == shipped.rootKeys.end() || ib == shipped.rootKeys.end()) continue;
            const size_t bi = ia - shipped.rootKeys.begin(), bj = ib - shipped.rootKeys.begin();
            if (((pattern >> bi) & 1u) && ((pattern >> bj) & 1u)) return true;
        }
        return false;
    }

    std::string describe(uint32_t pattern, bool childrenOn) const {
        std::string s;
        for (size_t i = 0; i < shipped.rootKeys.size(); ++i)
            if ((pattern >> i) & 1u) s += (s.empty() ? "" : " ") + shipped.rootKeys[i];
        if (s.empty()) s = "(all off)";
        return s + (childrenOn ? " +children" : "");
    }

    std::vector<LayoutDefect> lintHud(UISystem& ui, glm::ivec2 size) {
        std::vector<UIPanel*> panels;
        for (const auto& [name, vis] : ui.getScreenList())
            if (name.find(':') == std::string::npos && vis)
                if (UIPanel* p = ui.getScreen(name)) if (p->visible) panels.push_back(p);
        return lintLayout(panels, glm::vec2(size), &font, theme);
    }
};

}  // namespace

TEST_F(Matrix, TheShippedHudHasNoOverlapOrOverflowInAnyStateAtAnyPlayableSize) {
    ASSERT_LE(shipped.rootKeys.size(), 20u) << "2^N root states; the matrix is meant to stay exhaustive";
    const uint32_t patterns = 1u << shipped.rootKeys.size();
    int checked = 0;
    // One line per DISTINCT defect (size + message), with the simplest state that produced it
    // (fewest keys on) and how many states hit it - the fix is one JSON edit per line.
    struct Hit { std::string firstState; int firstBits = 99; int states = 0; };
    std::map<std::string, Hit> distinct;
    for (const auto& size : kSizes) {
        UISystem ui(nullptr, size.x, size.y);
        loadHudInto(ui, nullptr);
        for (uint32_t pattern = 0; pattern < patterns * 2; ++pattern) {
            const bool childrenOn = pattern >= patterns;
            const uint32_t rootPattern = pattern % patterns;
            if (excluded(rootPattern)) continue;
            HudDataContext ctx;
            bind(ctx, rootPattern, childrenOn);
            for (const auto& [name, vis] : ui.getScreenList())
                if (name.find(':') == std::string::npos) ui.showScreen(name);   // visibleWhen re-gates
            for (const auto& [name, vis] : ui.getScreenList())
                if (auto* s = ui.getScreen(name)) applyHudBindings(s, ctx);
            applyModalOverlays(ui);
            ++checked;
            int bits = childrenOn ? 1 : 0;
            for (uint32_t b = rootPattern; b; b >>= 1) bits += b & 1u;
            for (const auto& d : lintHud(ui, size)) {
                auto& h = distinct[std::to_string(size.x) + "x" + std::to_string(size.y) + "  " + d.message];
                ++h.states;
                if (bits < h.firstBits) { h.firstBits = bits; h.firstState = describe(rootPattern, childrenOn); }
            }
        }
    }
    EXPECT_GT(checked, 0);
    if (!distinct.empty()) {
        std::string msg = "HUD layout defects: " + std::to_string(distinct.size()) + " distinct across " +
                          std::to_string(checked) + " (size, state) checks\n";
        for (const auto& [key, h] : distinct)
            msg += "  " + key + "   <- e.g. [" + h.firstState + "], " + std::to_string(h.states) + " state(s)\n";
        FAIL() << msg;
    }
}

// The AI transcript label is capped (maxLines): 14 turns of history measure 6 lines, not 34.
TEST_F(Matrix, TheAiHistoryLabelShowsItsTailNotAnUnboundedTranscript) {
    UISystem ui(nullptr, 1280, 720);
    loadHudInto(ui, nullptr);
    UIPanel* ai = ui.getScreen("hud_ai_dialogue");
    ASSERT_NE(ai, nullptr);
    UILabel* hist = nullptr;
    for (auto& c : ai->children) if (c->id == "ai_history") hist = dynamic_cast<UILabel*>(c.get());
    ASSERT_NE(hist, nullptr);
    EXPECT_EQ(hist->maxLines, 6) << "shipped JSON caps the transcript";
    hist->text = longestText("dialogue.aiHistory");
    const float h = hist->measureHeight(&font, theme, 800.0f);
    EXPECT_LE(h, font.lineHeight(2.0f) * 6.0f + 0.5f) << "measured " << h << " px for the capped label";
}

// What the shipped shell's onResolveVariable can put into a data screen, at its longest:
// any binding may be rebound to "NumpadDivide" (the longest key name, GameSettings.cpp); the
// sheet at level 20 with the longest shipped race (resources/races) and class names; the
// scaffold's tagline is capped at 110 chars (create_project.py). Left unresolved, a literal
// "{{sheet.skill_SleightOfHand}}" is 464 px and every value on the sheet reads as overflowing.
std::optional<std::string> worstCaseToken(const std::string& t) {
    if (t.rfind("keybind.", 0) == 0) return std::string("NumpadDivide");
    if (t == "title")          return std::string("Ravenmere");
    if (t == "tagline")        return std::string("A short Baldur's Gate-like: story-driven party RPG with D&D turn-based combat, "
                                                  "one town and a barrow...");   // 110 chars
    if (t == "loading_target") return std::string("The Tavern Cellar Beneath the Broken Wheel");
    if (t.rfind("sheet.", 0) != 0) return std::nullopt;
    const std::string k = t.substr(6);
    if (k == "name")    return std::string("Adventurer");
    if (k == "race")    return std::string("Lightfoot Halfling");
    if (k == "class")   return std::string("Barbarian");
    if (k == "level")   return std::string("20");
    if (k == "xp" || k == "xp_next") return std::string("355000");
    if (k == "hp" || k == "max_hp")  return std::string("340");
    if (k == "ac")      return std::string("22");
    if (k == "speed")   return std::string("40");
    if (k == "initiative" || k == "proficiency") return std::string("+11");
    if (k.rfind("save_", 0) == 0)  return std::string("+11");
    if (k.rfind("skill_", 0) == 0) return std::string("+11 *");
    if (k.size() > 4 && k.compare(k.size() - 4, 4, "_mod") == 0) return std::string("+5");
    return std::string("20");   // an ability score
}

TEST_F(Matrix, EveryShippedDataScreenFitsAndDoesNotOverlapItselfAtAnyPlayableSize) {
    const std::vector<std::string> screens = {"mainmenu", "intro", "loading", "character", "victory",
                                              "credits", "game_over", "settings"};
    std::vector<std::string> failures;
    for (const auto& size : kSizes) {
        for (const auto& name : screens) {
            UISystem ui(nullptr, size.x, size.y);
            MenuActions actions;
            actions.onResolveVariable = worstCaseToken;
            loadGameScreenInto(ui, name, actions);
            auto lintNs = [&](const std::string& tag) {
                std::vector<UIPanel*> panels;
                for (const auto& [sn, vis] : ui.getScreenList())
                    if (vis && sn.rfind(name + ":", 0) == 0)
                        if (UIPanel* p = ui.getScreen(sn)) { if (p->id.empty()) p->id = "screen:" + sn; panels.push_back(p); }
                for (const auto& d : lintLayout(panels, glm::vec2(size), &font, theme))
                    failures.push_back(std::to_string(size.x) + "x" + std::to_string(size.y) + "  " + tag + "  " + d.message);
            };
            lintNs(name);
            if (name == "settings") {   // the sub-panel too
                for (const auto& [sn, vis] : ui.getScreenList())
                    if (sn.rfind("settings:", 0) == 0) { if (sn == "settings:keybindings") ui.showScreen(sn); else ui.hideScreen(sn); }
                lintNs("settings:keybindings");
            }
        }
        // the pause menu has its own loader
        UISystem ui(nullptr, size.x, size.y);
        MenuActions actions;
        actions.onResolveVariable = worstCaseToken;
        loadPauseMenuInto(ui, actions);
        std::vector<UIPanel*> panels;
        for (const auto& [sn, vis] : ui.getScreenList())
            if (vis && sn.rfind("pause:", 0) == 0) if (UIPanel* p = ui.getScreen(sn)) { if (p->id.empty()) p->id = "screen:" + sn; panels.push_back(p); }
        for (const auto& d : lintLayout(panels, glm::vec2(size), &font, theme))
            failures.push_back(std::to_string(size.x) + "x" + std::to_string(size.y) + "  pause  " + d.message);
    }
    if (!failures.empty()) {
        std::string msg = "data-screen layout defects (" + std::to_string(failures.size()) + "):\n";
        for (const auto& f : failures) msg += "  " + f + "\n";
        FAIL() << msg;
    }
}
