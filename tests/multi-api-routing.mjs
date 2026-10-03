import fs from "node:fs";
import assert from "node:assert/strict";

const read = (path) => fs.readFileSync(path, "utf8");

const profiles = read("src/include/miaodesk/ApiRuntimeProfile.h");
const agentHeader = read("src/include/miaodesk/L3Agent.h");
const agent = read("src/ai/agent/L3Agent.cpp");
const pi = read("src/ai/pi/PiRuntime.cpp");
const settings = read("src/ui/settings/DesktopAiSettingsPage.cpp");
const conversation = read("src/ui/ai/ConversationPanelImpl.inc");
const creator = read("src/ui/ai/ContentCreatorDialog.cpp");

assert.ok(profiles.includes("inline std::vector<RuntimeProfile> LoadAll()"));
assert.ok(profiles.includes("inline RuntimeProfile LoadById(std::wstring_view id)"));
const loadDefaultAt = profiles.indexOf("inline RuntimeProfile LoadDefault()");
assert.notStrictEqual(loadDefaultAt, -1);
const loadDefaultBody = profiles.slice(loadDefaultAt, profiles.indexOf("\n}\n", loadDefaultAt) + 3);
assert.ok(loadDefaultBody.includes("if (profile.configured) return profile;"),
  "default routing must choose the first configured profile");
assert.ok(!loadDefaultBody.includes("profile.explicitDefault"),
  "legacy default=1 must not override first-profile policy");

assert.ok(agentHeader.includes("void SetProfileId(std::wstring profileId)"));
assert.ok(agentHeader.includes("std::wstring preferredProfileId_"));
assert.ok(agentHeader.includes("std::wstring profileId;"));
assert.ok(agentHeader.includes("std::wstring profileName;"));
assert.ok(agentHeader.includes("api_runtime_profile::LoadById(preferredProfileId_)"));
assert.ok(agent.includes("Lower(config.profileId)"),
  "direct-model history/session identity must include the selected profile");
// The per-profile credential slot is derived in one place now. Before P0-07 the
// prefix lived inline in L3Agent.cpp while ApiRuntimeProfile::ReadSection read the
// same string from its own copy, so the two could drift and the key would silently
// land in a different slot on one side only. The contract below pins the intent
// (the direct model's credential IS the selected profile's), not one spelling.
assert.ok(agent.includes("agent_config::AgentCredentialTarget(RequestIdentity())"),
  "direct-model credentials must come from the selected central profile");
const credentialSlots = read("src/ai/agent/MiaoAgentConfigAuthority.cpp");
assert.ok(credentialSlots.includes("JoinPrefix(kProfileCredentialPrefix, identity.profileId)"),
  "the shared credential slot must be derived from the selected profile id, not hardcoded");
assert.ok(agent.includes("static_assert(std::wstring_view(kCredentialTarget) =="),
  "the legacy credential slot name must stay pinned to the shared copy so it cannot drift");
assert.ok(!agent.includes('L"MiaoDesk/ApiProfile/" + config_.profileId'),
  "a second inline copy of the profile credential slot must not come back");

assert.ok(pi.includes("return agent.CurrentApiKey();"),
  "Pi must use the same selected profile credential as the L3 agent");
assert.ok(pi.includes("setup.apiKey = LoadApiKey(agent);"));
assert.ok(pi.includes('L"|profile=" + agent.ProfileId()'),
  "Pi child/session identity must include the selected profile id");

assert.ok(!settings.includes('button(L"设为默认"'),
  "API Configuration Center should not expose a second global-default mechanism");
assert.ok(!settings.includes("kSetDefaultId"),
  "retired set-default action must stay removed");
assert.ok(settings.includes("firstConfigured"),
  "settings list must mark the first configured profile as the fallback default");
assert.ok(settings.includes("新 AI 窗口默认使用列表中的第一个已配置 API"));
assert.ok(settings.includes("配置名称不能重复"),
  "profile names must be unique because AI windows select by profile name");

for (const [name, source] of [["conversation", conversation], ["creator", creator]]) {
  assert.ok(source.includes("kApiProfileId"), `${name} must own an API profile selector`);
  assert.ok(source.includes("CBS_DROPDOWNLIST"), `${name} selector must choose central profile names`);
  assert.ok(source.includes("api_runtime_profile::LoadAll()"), `${name} must read central profiles`);
  assert.ok(source.includes("std::make_unique<L3Agent>()"), `${name} must own local profile selection state`);
  assert.ok(source.includes("CBN_SELCHANGE"), `${name} selector must be interactive`);
  assert.ok(source.includes("CBN_DROPDOWN"), `${name} must refresh central profiles when opened`);
  assert.ok(source.includes("pi->Busy()") || source.includes("state.pi->Busy()"),
    `${name} must not switch the shared Pi provider while another AI turn is active`);
  assert.ok(source.includes("apiProfiles.front().id") || source.includes("selectedIndex = 0"),
    `${name} must fall back to the first configured profile`);
}

assert.ok(conversation.includes('L"API · " + profileName'),
  "conversation header selector must visibly identify itself as an API selector");
assert.ok(conversation.includes("ShowConversationApiProfileMenu"),
  "layered conversation surface must open API selection through its own hit target");
assert.ok(conversation.includes("ShowWindow(state.apiProfileCombo, SW_HIDE)"),
  "native ComboBox must stay hidden because UpdateLayeredWindow does not composite child pixels");
const layeredSurface = read("src/ui/ai/ConversationPanelLayeredSurface.inc");
assert.ok(layeredSurface.includes("selectedApiLabel") &&
          layeredSurface.includes("FillRoundedRectangle(selector") &&
          layeredSurface.includes("UpdateConversationApiProfileRect"),
  "API selector label/border/arrow must be rendered into the Direct2D layered surface");
assert.ok(conversation.includes("headerIconsLeft") &&
          conversation.includes("comboRight = headerIconsLeft - Px(state, 10)") &&
          conversation.includes("comboX = comboRight - comboW"),
  "conversation API selector must stay anchored immediately before the header action icons");

assert.ok(conversation.includes("state.agent->ReloadConfig();"),
  "conversation must re-read the selected central profile before a model turn");
assert.ok(creator.includes("agent->ReloadConfig();"),
  "creator must re-read the selected central profile before generation");

console.log("multi-API routing contract: PASS");
