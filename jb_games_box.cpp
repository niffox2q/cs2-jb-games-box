#include "jb_games_box.h"
#include <random>
#include <cstdio>
#include <algorithm>

#define MAX_PLAYERS 64

#define CS_TEAM_NONE 0
#define CS_TEAM_SPECTATOR 1
#define CS_TEAM_T 2
#define CS_TEAM_CT 3



jb_games_box g_jb_games_box;
PLUGIN_EXPOSE(jb_games_box, g_jb_games_box);

// SYSTEM API`s
IVEngineServer2* engine = nullptr;
CGlobalVars* gpGlobals = nullptr;
CGameEntitySystem* g_pGameEntitySystem = nullptr;
CEntitySystem* g_pEntitySystem = nullptr;

// API
IUtilsApi* utils;
IMenusApi* menus_api;
IPlayersApi* players_api;
IJailbreakApi* jailbreak_api;

// VARS

bool bFFEnabled = false;
static bool b_CvarLocked = false;




// =========================================
// CONFIG VARS
// =========================================
std::map<std::string, std::string> phrases;

//==========================================
// HELPERS
//==========================================
ConVarRefAbstract* FindConVar(const char* sCvarName)
{
    return new ConVarRefAbstract(g_pCVar->FindConVar(sCvarName));
}



// =========================================
// CONFIGS 
// =========================================

void LoadTranslations() {
    phrases.clear();
    KeyValues* g_kvPhrases = new KeyValues("Phrases");
    const char *pszPath = "addons/translations/jailbreak.phrases.txt";

    if (!g_kvPhrases->LoadFromFile(g_pFullFileSystem, pszPath))
    {
        utils->ErrorLog("%s Failed to load %s", g_PLAPI->GetLogTag(), pszPath);
        delete g_kvPhrases;
        return;
    }

    const char* language = utils->GetLanguage();

    for (KeyValues *pKey = g_kvPhrases->GetFirstTrueSubKey(); pKey; pKey = pKey->GetNextTrueSubKey()) {
        phrases[std::string(pKey->GetName())] = std::string(pKey->GetString(language));
    }
    delete g_kvPhrases;
}

const char* GetTranslation(const char* key) {
    auto it = phrases.find(key);
    if (it == phrases.end()) return key;
    else return it->second.c_str();
}

void PrintSlotPrefixed(int iSlot, const char* content) {
    if (!content || content[0] == '\0') return;
    char buf[512];
    g_SMAPI->Format(buf, sizeof(buf), "%s %s", GetTranslation("Prefix"), content);
    utils->PrintToChat(iSlot, buf);
}

void PrintAllPrefixed(const char* content) {
    if (!content || content[0] == '\0') return;
    char buf[512];
    g_SMAPI->Format(buf, sizeof(buf), "%s %s", GetTranslation("Prefix"), content);
    utils->PrintToChatAll(buf);
}

std::vector<int> GetAliveTerrorists(){
    std::vector<int> vAliveTerrorists;
    for (int i = 0;i < MAX_PLAYERS;i++){
        auto pController = CCSPlayerController::FromSlot(i);
        if (!pController || pController->GetTeam() != CS_TEAM_T) continue;
        auto pPawn = pController->GetPlayerPawn();
        if (pPawn && pPawn->IsAlive()) vAliveTerrorists.push_back(i);
    }
    return vAliveTerrorists;
}

// =========================================
// OTHER
// =========================================

void OnBoxGame(int iSlot){
    if (jailbreak_api->GetWarden() != iSlot) return;
    if (GetAliveTerrorists().size() <= 1) {
        PrintSlotPrefixed(iSlot,GetTranslation("Box_NotEnoughPlayers"));
        return;
    }
    ConVarRefAbstract* FFCvar = FindConVar("mp_teammates_are_enemies");
    if (FFCvar && bFFEnabled) {
        FFCvar->SetBool(false);
        PrintAllPrefixed(GetTranslation("Box_Disabled"));
        bFFEnabled = false;
    } 
    else if (FFCvar && !bFFEnabled) {
        FFCvar->SetBool(true);
        PrintAllPrefixed(GetTranslation("Box_Enabled"));
        bFFEnabled = true;
    }

    if (FFCvar){
        delete FFCvar;
    }
}



CGameEntitySystem* GameEntitySystem() {
    return utils ? utils->GetCGameEntitySystem() : nullptr;
}



void StartupServer() {
    g_pGameEntitySystem = GameEntitySystem();
    g_pEntitySystem = utils->GetCEntitySystem();
    gpGlobals = utils->GetCGlobalVars();
}

bool jb_games_box::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late) {
    PLUGIN_SAVEVARS();

    GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
    GET_V_IFACE_ANY(GetEngineFactory, g_pSchemaSystem, ISchemaSystem, SCHEMASYSTEM_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetFileSystemFactory, g_pFullFileSystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer2, SOURCE2ENGINETOSERVER_INTERFACE_VERSION);
    GET_V_IFACE_ANY(GetServerFactory, g_pSource2Server, ISource2Server, SOURCE2SERVER_INTERFACE_VERSION);
    GET_V_IFACE_ANY(GetServerFactory, g_pSource2GameClients, IServerGameClients, SOURCE2GAMECLIENTS_INTERFACE_VERSION);
    GET_V_IFACE_ANY(GetServerFactory, g_pSource2GameEntities, ISource2GameEntities, SOURCE2GAMEENTITIES_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pNetworkSystem, INetworkSystem, NETWORKSYSTEM_INTERFACE_VERSION);

    ConVar_Register(FCVAR_SERVER_CAN_EXECUTE | FCVAR_GAMEDLL);
    g_SMAPI->AddListener(this, this);

    return true;
}



void jb_games_box::AllPluginsLoaded() {
    int ret;
    utils = (IUtilsApi*)g_SMAPI->MetaFactory(Utils_INTERFACE, &ret, nullptr);
    if (ret == META_IFACE_FAILED) {
        META_CONPRINTF("%s | Missing UTILS plugin.",g_PLAPI->GetLogTag());
        engine->ServerCommand(("meta unload " + std::to_string(g_PLID)).c_str());
        return;
    }


    menus_api = (IMenusApi*)g_SMAPI->MetaFactory(Menus_INTERFACE, &ret, nullptr);
    if (ret == META_IFACE_FAILED) {
        META_CONPRINTF("%s | Missing UTILS plugin.",g_PLAPI->GetLogTag());
        engine->ServerCommand(("meta unload " + std::to_string(g_PLID)).c_str());
        return;
    }

    players_api = (IPlayersApi*)g_SMAPI->MetaFactory(PLAYERS_INTERFACE, &ret, nullptr);
    if (ret == META_IFACE_FAILED) {
        META_CONPRINTF("%s | Missing UTILS plugin.",g_PLAPI->GetLogTag());
        engine->ServerCommand(("meta unload " + std::to_string(g_PLID)).c_str());
        return;
    }

    jailbreak_api =(IJailbreakApi*)g_SMAPI->MetaFactory(JAILBREAK_INTERFACE, &ret, nullptr);
    if (ret == META_IFACE_FAILED) {
        META_CONPRINTF("%s | Missing Jailbreak Core plugin.",g_PLAPI->GetLogTag());
        engine->ServerCommand(("meta unload " + std::to_string(g_PLID)).c_str());
        return;
    }


    LoadTranslations();

    jailbreak_api->RegisterGameFeature(g_PLID,"box",GetTranslation("Game_Box"),OnBoxGame);

    utils->HookEvent(g_PLID,"round_start",[](const char* szName, IGameEvent* pEvent, bool bDontBroadcast){
        if (bFFEnabled) {
        ConVarRefAbstract* FFCvar = FindConVar("mp_teammates_are_enemies");
        if (FFCvar) {
            FFCvar->SetBool(false);
        }
        bFFEnabled = false;
        delete FFCvar;
    }
    }); 

    utils->HookOnTakeDamagePre(g_PLID, [](int iSlot, CTakeDamageInfo *pInfo) {
        if (!bFFEnabled) return true;

        auto pController = CCSPlayerController::FromSlot(iSlot);
        if (pController && pController->GetTeam() == CS_TEAM_CT) {

            auto AttackerHandle = pInfo->m_hAttacker.Get();

            if (AttackerHandle.IsValid()) {
                auto attacker = AttackerHandle.Get();

                if (attacker) {
                    auto AttackerEntity = (CBaseEntity*)attacker;

                    if (AttackerEntity) {
                        if (AttackerEntity->GetTeam() == CS_TEAM_CT) return false;
                    }
                }
            }
        }


        return true;
    });

    utils->HookEvent(g_PLID,"player_death",[](const char* szName, IGameEvent* pEvent, bool bDontBroadcast){
        if (bFFEnabled) {
            std::vector<int> vAliveT = GetAliveTerrorists();

            if (vAliveT.size() <= 1) {
                ConVarRefAbstract* FFCvar = FindConVar("mp_teammates_are_enemies");
                if (FFCvar){
                    bFFEnabled = false;
                    FFCvar->SetBool(false);
                    delete FFCvar;
                }

            }
        }
    });

    utils->StartupServer(g_PLID, StartupServer);

    
    
}

bool jb_games_box::Unload(char* error, size_t maxlen) {
    jailbreak_api->ClearAllPluginHooks(g_PLID);
    utils->ClearAllHooks(g_PLID);
    ConVar_Unregister();

   
    return true;
}

const char* jb_games_box::GetAuthor() { return "niffox"; }
const char* jb_games_box::GetDate() { return __DATE__; }
const char* jb_games_box::GetDescription() { return "[JB] Game Box"; }
const char* jb_games_box::GetLicense() { return "Private"; }
const char* jb_games_box::GetLogTag() { return "[JB] Game Box"; }
const char* jb_games_box::GetName() { return "[JB] Game Box"; }
const char* jb_games_box::GetURL() { return "https://t.me/niffox_2q"; }
const char* jb_games_box::GetVersion() { return "1.0.1"; }