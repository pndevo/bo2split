// The entities of the world as the shadow passes see them: player 1's body in the other views,
// the entity add and animation advance hooks, name tags and the crosshair scan, and the
// diagnostics that follow a player's model through the scene.

#include "core/common.h"
#include "shadow/shadow.h"
#include "engine/client.h"
#include "engine/render.h"
#include "view/view_private.h"

// p2_show_p1=1: player 1's body in the second view. The server never sends a client its own
// entity, so player 1 has no body in the snapshot entities that pass 2 draws (player 2 would
// see nobody). The engine draws the local player's body only on its third-person path in
// CG_DrawActiveFrame (ps flags & 6, after CG_PredictAndUpdateViewModel): DObj of the player
// through the handle map (0x006cab90(clientNum, lc)), anim advance 0x004bb0b0(cg, dobj, 2),
// notetracks 0x004ba6e0(lc, clientNum) inside a notetrack buffer (0x00790d40 fastcall reset,
// 0x00789150 set, 0x00789140 clear, 0x00790d90 fastcall release), then the lerp 0x004bfc40(lc,
// cent) and the add by type 0x004c0750(lc, cent, cg) with the predictedPlayerEntity cg+0x4aab4
// (0x380 bytes, nextState filled from the predicted playerState). In pass 2 that entity is
// player 2's (prediction on the swapped playerState), so player 1's copy is taken at the end
// of pass 0 and the chain runs on the copy; the DObj lives at handle clientNum in the shared
// map and is created by 0x004c0750 -> 0x004becb0 -> 0x004be340 -> 0x004bc990 on the first call.
__declspec(align(16)) uint8_t g_p1PredEnt[engine::kCentSize];
__declspec(align(16)) static uint8_t g_p1BodyEnt[engine::kCentSize];
bool g_p1PredEntValid;
typedef void(__fastcall* NotetrackBuf_t)(void* buf);
typedef void(__cdecl* NotetrackSet_t)(void* buf);
typedef void(__cdecl* NotetrackClear_t)();
// Diagnostics (p2_body_trace=1): CG_Player 0x004e9650(lc, cent) and R_AddDObjToScene
// 0x004106f0(dobj, cent, entNum, flags, ...) logged for player 1's body entity in pass 2.
static int g_bodyTraceNum = -1;
int g_vmAdds, g_vmTraceLeft, g_vmSceneIdx = -1;
typedef void(__cdecl* CgPlayer_t)(int, void*);
static CgPlayer_t oCgPlayer;
static void __cdecl HookCgPlayer(int lc, void* cent) {
    bool trace = g_bodyTraceNum >= 0 && cent == g_p1BodyEnt;
    if (trace)
        Log("[p2] body trace: CG_Player lc %d cent %p (snap ps flags %02x clientNum %d, cg clientNum %d, third %d)", lc,
            cent, *(const uint8_t*)(*(uint8_t**)(*(uint8_t**)engine::kCg + engine::kCgSnap) + 0x28),
            *(const int*)(*(uint8_t**)(*(uint8_t**)engine::kCg + engine::kCgSnap) + 0x1b0), **(int**)engine::kCg,
            *(const int*)(*(uint8_t**)engine::kCg + 0x480a0));
    oCgPlayer(lc, cent);
    if (trace) Log("[p2] body trace: CG_Player done");
}
static const uint8_t kProCgPlayer[] = {0x83, 0xec, 0x6c, 0x53, 0x55, 0x56};
typedef void(__cdecl* AddDObjToScene_t)(void*, void*, unsigned, unsigned, void*, unsigned*, unsigned*, char, unsigned,
                                        int, int, float, char);
static AddDObjToScene_t oAddDObjToScene;
static void __cdecl HookAddDObjToScene(void* dobj, void* cent, unsigned num, unsigned flags, void* a5, unsigned* a6,
                                       unsigned* a7, char a8, unsigned a9, int a10, int a11, float a12, char a13) {
    bool trace = g_bodyTraceNum >= 0 && g_cgSwapped && (int)num == g_bodyTraceNum;
    // p2_dobj_log=N (diagnostic): N lines, one per scene add of a player's DObj (entity numbers
    // 0 to 17) inside the view passes: pass, entity, pose origin, and whether the add comes from
    // AddPlayer1Body. Shows which pass draws which body where.
    if (num < 18 && g_inRenderViews && cent) {
        static int left = -1, armed = -1;
        {
            int v = ReadIniInt(L"p2_dobj_log", 0);
            if (v != armed) {
                armed = v;
                left = v;
            }
        }
        if (left > 0) {
            --left;
            const float* o = (const float*)cent + 11;
            const uint8_t* cg = *(const uint8_t**)engine::kCg;
            Log("[dobj] pass %d ent %u dobj %p cent %p%s origin %.0f %.0f %.0f flags %08x | pass clientNum %d",
                g_viewPass, num, dobj, cent, cent == g_p1BodyEnt ? " (p1 body copy)" : "", o[0], o[1], o[2], flags,
                cg ? *(const int*)(cg + 0x48248) : -1);
        }
    }
    oAddDObjToScene(dobj, cent, num, flags, a5, a6, a7, a8, a9, a10, a11, a12, a13);
    // p2_vm_trace=N: the viewmodel add (entity number 0x3ff, from 0x005393e0) per pass.
    if (num == 0x3ff && g_inRenderViews) {
        ++g_vmAdds;
        {
            const short* m = *(const short**)engine::kSceneIndexMap;
            g_vmSceneIdx = m ? (int)m[num] : -1;
        }
        if (g_vmTraceLeft > 0) {
            --g_vmTraceLeft;
            const uint8_t* cg = *(const uint8_t**)engine::kCg;
            const float* vo = (const float*)(cg + 0x69828);
            const float* lo = (const float*)a5;
            const short* idx = *(const short**)engine::kSceneIndexMap;
            Log("[vm] add pass %d swapped %d: dobj %p flags %08x view origin %.0f %.0f %.0f light origin %.0f %.0f "
                "%.0f scene idx %d | pose %p (cg+0x%x) origin %.0f %.0f %.0f cullIn %d",
                g_viewPass, (int)g_cgSwapped, dobj, flags, vo[0], vo[1], vo[2], lo[0], lo[1], lo[2],
                idx ? (int)idx[num] : -2, cent, (unsigned)((const uint8_t*)cent - cg), ((const float*)cent)[11],
                ((const float*)cent)[12], ((const float*)cent)[13], *(const int*)((const uint8_t*)cent + 0x28));
        }
    }
    if (trace) {
        const short* idx = *(const short**)engine::kSceneIndexMap;
        Log("[p2] body trace: R_AddDObjToScene dobj %p cent %p num %u flags %08x models %d -> scene idx %d (dobj list "
            "count %d)",
            dobj, cent, num, flags, (int)((const uint8_t*)dobj)[9], idx ? (int)idx[num] : -2,
            *(const int*)engine::kSceneDObjCount);
    }
}
static const uint8_t kProAddDObjToScene[] = {0x8b, 0x0d, 0x94, 0xdc, 0x4a, 0x03, 0x83, 0xec, 0x08};
// p2_p1_pose=1: player 1's slot in the entity array (0x01060e48, 0x380 per entity) keeps player
// 1's position. What hangs on a player reads the parent's cent there: a linked entity takes
// origin (+0x2c) and angles (+0x38) of CG_GetEntity(parent) (0x0049e230), so does an effect on a
// tag of the player. The slot of the local player is not filled from a snapshot, so with four
// players an entity linked to player 1 (ent 0, burning in the lava at -5900 4900) would stand at
// player 4's origin (-6915 5155) in every pass, and its fire would show next to player 4. The pose
// player 1's body gets in a shadow pass (AddPlayer1Body, after the lerp) is written to the slot
// before the entities of each pass are added and at the end of each pass.
static float g_p1Pose[6];
static bool g_p1PoseValid;
void WritePlayer1Pose() {
    if (!g_p1PoseValid || !g_p1PredEntValid || !ReadIniInt(L"p2_p1_pose", 1)) return;
    uint8_t* ents = *(uint8_t**)engine::kCgEntities;
    int num = *(const int*)(g_p1PredEnt + 0x1dc);
    if (ents && num >= 0 && num < 18) memcpy(ents + num * engine::kCentSize + 0x2c, g_p1Pose, sizeof(g_p1Pose));
}
void AddPlayer1Body(uint8_t* cg) {
    if (!g_p1PredEntValid || !ReadIniInt(L"p2_show_p1", 1)) return;
    static const uint8_t expAdd[] = {0x53, 0x8b, 0x5c, 0x24, 0x08, 0x56, 0x8b, 0x74,
                                     0x24, 0x10, 0x83, 0xbe, 0x60, 0x02, 0x00, 0x00};
    static int checked;
    if (!checked) {
        checked = BytesMatch(engine::kCG_AddEntityByType, expAdd, sizeof(expAdd)) ? 1 : -1;
        if (checked < 0) Log("[p2] body: 0x004c0750 prologue mismatch, p2_show_p1 disabled");
    }
    if (checked < 0) return;
    // The player draw (0x004e6dc0) keeps two physics bodies per player entity, handles at
    // cent+0x10 (standing) and cent+0x18 (down). The handles count their users (0x007ba170
    // finds the body of an entity number and adds a user, 0x007baf00 drops one). A copy that
    // carried player 1's handles would drop a user it never added when the shadow pass decides
    // against a body that player 1's own pass keeps, and the body would be destroyed under
    // player 1's entity (see the block's handles in cgame_swap.cpp). The copy keeps handles
    // of its own instead, from one frame to the next, and forgets them with a new match.
    static DWORD handlesMatch;
    uint32_t ownStanding = *(const uint32_t*)(g_p1BodyEnt + 0x10);
    uint32_t ownDown = *(const uint32_t*)(g_p1BodyEnt + 0x18);
    if (handlesMatch != g_p2.startedTick) {
        handlesMatch = g_p2.startedTick;
        ownStanding = 0;
        ownDown = 0;
    }
    memcpy(g_p1BodyEnt, g_p1PredEnt, engine::kCentSize);
    if (ReadIniInt(L"p_body_handles", 1)) {
        *(uint32_t*)(g_p1BodyEnt + 0x10) = ownStanding;
        *(uint32_t*)(g_p1BodyEnt + 0x18) = ownDown;
    }
    int num = *(const int*)(g_p1BodyEnt + 0x1dc);
    if (num < 0 || num >= 18) return;
    void* dobj = engine::Com_GetClientDObj(num, 0);
    if (dobj && g_viewPass == 1 && ReadIniInt(L"p2_show_p1_anim", 1)) { // the shared DObj advances once per frame
        __declspec(align(16)) uint8_t notes[0x520];
        ((NotetrackBuf_t)0x00790d40)(notes);
        ((NotetrackSet_t)0x00789150)(notes);
        engine::CG_AdvanceDObjAnimByFrametime(cg, dobj, 2);
        if (ReadIniInt(L"p2_show_p1_notetracks", 0)) engine::CG_ProcessPlayerNotetracks(0, num);
        ((NotetrackClear_t)0x00789140)();
        ((NotetrackBuf_t)0x00790d90)(notes);
    }
    // Scene bookkeeping of R_AddDObjToScene (0x004106f0): one short per entity number at
    // *0x034adc94, -1 = not in the scene yet, else the scene entity index.
    const short* sceneIdx = *(const short**)engine::kSceneIndexMap;
    int idxBefore = sceneIdx ? sceneIdx[num] : -2;
    g_bodyTraceNum = ReadIniInt(L"p2_body_trace", 0) ? num : -1;
    // CG_Player (0x004e9650) skips the entity whose number is the snapshot playerState's
    // clientNum while that playerState has flag bit 2 or 4 set (first person: no own body)
    // and renderingThirdPerson (cg+0x480a0) is 0. cg->snap (cg+0x28) is player 1's snapshot in
    // both passes, so its clientNum is player 2's for the duration of the add
    // (p2_show_p1_snapnum=1).
    uint8_t* snap = *(uint8_t**)(cg + engine::kCgSnap);
    int* snapClientNum = snap ? (int*)(snap + 0x1b0) : nullptr;
    int savedSnapClientNum = snapClientNum ? *snapClientNum : 0;
    bool swapSnapNum = snapClientNum && ReadIniInt(L"p2_show_p1_snapnum", 1);
    if (swapSnapNum) *snapClientNum = *(const int*)(ShadowViewCl() + engine::kClPlayerState + engine::kPsClientNum);
    engine::CG_CalcEntityLerpPositions(0, g_p1BodyEnt);
    memcpy(g_p1Pose, g_p1BodyEnt + 0x2c, sizeof(g_p1Pose));
    g_p1PoseValid = true;
    WritePlayer1Pose();
    engine::CG_AddEntityByType(0, g_p1BodyEnt, cg);
    if (swapSnapNum) *snapClientNum = savedSnapClientNum;
    g_bodyTraceNum = -1;
    static int logged;
    static DWORD nextLog;
    if (logged < 5 || (LONG)(GetTickCount() - nextLog) >= 0) {
        ++logged;
        nextLog = GetTickCount() + 5000;
        const float* org = (const float*)(g_p1BodyEnt + 0x2c);
        const float* p2org = (const float*)(cg + engine::kCgPredictedPs + engine::kPsOrigin);
        void* dobjAfter = engine::Com_GetClientDObj(num, 0);
        Log("[p2] body: p1 ent %d type %d model %d ci %d eFlags %08x/%08x dobj %p (%d models) -> %p scene idx %d -> %d "
            "lerp %.0f %.0f %.0f ang %.0f %.0f flags %08x/%08x | p2 at %.0f %.0f %.0f",
            num, (int)*(const short*)(g_p1BodyEnt + 0x2b4), (int)*(const short*)(g_p1BodyEnt + 0x2b8),
            (int)g_p1BodyEnt[0x2cf], *(const uint32_t*)(g_p1BodyEnt + 0x1e0), *(const uint32_t*)(g_p1BodyEnt + 0x1e4),
            dobj, dobj ? (int)((const uint8_t*)dobj)[9] : -1, dobjAfter, idxBefore, sceneIdx ? (int)sceneIdx[num] : -2,
            org[0], org[1], org[2], org[3], org[4], *(const uint32_t*)(g_p1BodyEnt + 0x378),
            *(const uint32_t*)(g_p1BodyEnt + 0x37c), p2org[0], p2org[1], p2org[2]);
    }
}
// CG_ScanForCrosshairEntity 0x004b35b0(lc): a trace from the view origin (cg+0x4d8c4) along
// the view axis that skips the entity with the snapshot playerState's clientNum (snap+0x1b0)
// and compares the hit entity's team with the team of that client. On a team mate it sets bit
// 8 in the predicted playerState's flags (cg+0x480b8), on an enemy bit 0x10, and
// CG_GetCrosshairColor 0x004b6220 turns the crosshair green or red. cg->snap is player 1's
// snapshot in every pass, so in a shadow player's pass the trace would skip player 1 and hit the
// shadow player's own body: the crosshair would go green while moving. For the scan the clientNum
// is the pass's player (p_crosshair_self=1).
typedef void(__cdecl* CrosshairScan_t)(int);
static CrosshairScan_t oCrosshairScan;
static void __cdecl HookCrosshairScan(int lc) {
    uint8_t* cg = *(uint8_t**)engine::kCg;
    uint8_t* snap = cg ? *(uint8_t**)(cg + engine::kCgSnap) : nullptr;
    uint8_t* cl = (g_cgSwapped && g_viewPass >= 1) ? ShadowViewCl() : nullptr;
    if (!snap || !cl || !ReadIniInt(L"p_crosshair_self", 1)) {
        oCrosshairScan(lc);
        return;
    }
    int* num = (int*)(snap + 0x1b0);
    int saved = *num;
    *num = *(const int*)(cl + engine::kClPlayerState + engine::kPsClientNum);
    oCrosshairScan(lc);
    *num = saved;
}
static const uint8_t kProCrosshairScan[] = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf0};
// Entity adding (CG_DrawActiveFrame -> 0x004c0ff0) draws every entity of the snapshot's
// entity list (the active-entity mask 0x010628b0 only covers the client-side entities
// 0x400..0x6ff, so clearing player 2's bit there hides nothing). The server never sends a
// client its own entity, which is why player 1 has no body in its view; player 2's entity
// is a normal remote player in player 1's snapshot and would be drawn in every view. The add
// (0x004becb0) skips the scene submit when bit 0x20 of the entity's eFlags (cent+0x1e0,
// nextState+4) is set, and 0x0048fe80 (extra models) checks the same bit, so that bit is set
// on player 2's entity around the two add functions in the second pass (p2_hide_body=1).
// 0x0048fe80 also skips the entity whose number is the predicted playerState's clientNum;
// that field is player 2's around the calls (p2_ent_clientnum=1).
// p2_ent_frametime0=1: the entity add of the second pass runs with cg->frametime (cg+0x48088)
// = 0, so the animations of the snapshot entities (other players, zombies) advance once per
// frame (in the first pass) instead of twice (which would animate other characters at
// double speed). Player 1's body in the second pass has its own advance (AddPlayer1Body).
static int g_entClientNumSaved = -1;
static int g_entFrametimeSaved = -1;
static uint8_t* g_entHidden;      // cent whose eFlags got bit 0x20 for the second pass
static uint32_t g_entHiddenFlags; // its eFlags before
static void ShadowEntClientNumBegin() {
    if (!(g_inRenderViews && g_viewPass >= 1 && g_psSwapped)) return;
    uint8_t* cg = *(uint8_t**)engine::kCg;
    if (!cg) return;
    if (g_cgSwapped && ReadIniInt(L"p2_ent_frametime0", 1) && g_entFrametimeSaved < 0) {
        g_entFrametimeSaved = *(int*)(cg + engine::kCgFrametime);
        *(int*)(cg + engine::kCgFrametime) = 0;
    }
    int num = *(const int*)(ShadowViewCl() + engine::kClPlayerState + engine::kPsClientNum);
    if (ReadIniInt(L"p2_ent_clientnum", 1)) {
        int* p = (int*)(cg + engine::kCgPredictedPs + engine::kPsClientNum);
        g_entClientNumSaved = *p;
        *p = num;
    }
    uint8_t* ents = *(uint8_t**)engine::kCgEntities;
    if (ReadIniInt(L"p2_hide_body", 1) && ents && num >= 0 && num < 0x400 && !g_entHidden) {
        uint8_t* cent = ents + num * engine::kCentSize;
        g_entHidden = cent;
        g_entHiddenFlags = *(uint32_t*)(cent + 0x1e0);
        *(uint32_t*)(cent + 0x1e0) = g_entHiddenFlags | 0x20;
    }
}
static void ShadowEntClientNumEnd() {
    if (g_entHidden) {
        *(uint32_t*)(g_entHidden + 0x1e0) = g_entHiddenFlags;
        g_entHidden = nullptr;
    }
    if (g_entFrametimeSaved >= 0) {
        uint8_t* cg = *(uint8_t**)engine::kCg;
        if (cg) *(int*)(cg + engine::kCgFrametime) = g_entFrametimeSaved;
        g_entFrametimeSaved = -1;
    }
    if (g_entClientNumSaved < 0) return;
    uint8_t* cg = *(uint8_t**)engine::kCg;
    if (cg) *(int*)(cg + engine::kCgPredictedPs + engine::kPsClientNum) = g_entClientNumSaved;
    g_entClientNumSaved = -1;
}
typedef int(__cdecl* CgAddEntities_t)(int);
static CgAddEntities_t oCgAddEntities;
// p2_link_p1=1: an entity linked to player 1 in a shadow pass. CG_AddPacketEntities (0x004c0ff0)
// adds a linked entity only after its parent: the parent's cent has flag 0x08000000 (cent+0x378)
// with bit 0x10000000 equal to the call's parity byte (*0x0105f738 + 0x5b58, flipped per call),
// or the parent is the predicted playerState's clientNum (cg+0x48248). The rest waits in a
// delay list (0x004c0ea0). Player 1 is never in its own snapshot, and in a shadow pass the
// clientNum is the shadow player's (p2_ent_clientnum), so an entity linked to player 1 (the
// script model TranZit links to a burning player) would never be added there and the game
// would print "UpdateDelayedPacketEnts(): delayList stuck with 1 entries left." in every
// shadow pass.
// Player 1's cent gets the two bits for the call.
// p2_link_log=N (diagnostic): N lines for the snapshot entities linked to a player (parent
// number below 18) after a shadow pass's add.
static int __cdecl HookCgAddEntities(int lc) {
    ShadowEntClientNumBegin();
    WritePlayer1Pose();
    uint8_t* p1cent = nullptr;
    uint32_t p1flags = 0;
    bool shadow = g_inRenderViews && g_viewPass >= 1 && g_psSwapped;
    uint8_t* ents = *(uint8_t**)engine::kCgEntities;
    const uint8_t* cgs = *(const uint8_t**)engine::kCgs;
    if (shadow && ents && cgs && g_p1PredEntValid && ReadIniInt(L"p2_link_p1", 1)) {
        int num = *(const int*)(g_p1PredEnt + 0x1dc);
        if (num >= 0 && num < 18) {
            p1cent = ents + num * engine::kCentSize;
            p1flags = *(uint32_t*)(p1cent + 0x378);
            *(uint32_t*)(p1cent + 0x378) = (p1flags & ~0x10000000u) | 0x08000000u | (cgs[0x5b58] ? 0x10000000u : 0u);
        }
    }
    int r = oCgAddEntities(lc);
    if (p1cent) *(uint32_t*)(p1cent + 0x378) = p1flags;
    if (shadow && ents) {
        static int left = -1, armed = -1;
        {
            int v = ReadIniInt(L"p2_link_log", 0);
            if (v != armed) {
                armed = v;
                left = v;
            }
        }
        const uint8_t* cg = *(const uint8_t**)engine::kCg;
        const uint8_t* snap = cg ? *(const uint8_t* const*)(cg + engine::kCgSnap) : nullptr;
        if (left > 0 && snap) {
            int n = *(const int*)(snap + 0x2a18);
            for (int i = 0; i < n && i < 1024 && left > 0; ++i) {
                int num = *(const int*)(snap + 0x2a24 + i * 0xf8);
                if (num < 0 || num >= 0x400) continue;
                const uint8_t* c = ents + num * engine::kCentSize;
                const uint8_t* link = *(const uint8_t* const*)(c + 0x2f8);
                if (!link || !IsReadable((uintptr_t)link, 0x34)) continue;
                int parent = *(const int*)(link + 0x30);
                if (parent < 0 || parent >= 18) continue;
                --left;
                Log("[link] pass %d ent %d type %d model %d linked to player ent %d: flags %08x origin %.0f %.0f %.0f",
                    g_viewPass, num, (int)*(const short*)(c + 0x2b4), (int)*(const short*)(c + 0x2b8), parent,
                    *(const uint32_t*)(c + 0x378), ((const float*)c)[11], ((const float*)c)[12], ((const float*)c)[13]);
            }
        }
    }
    ShadowEntClientNumEnd();
    return r;
}
static const uint8_t kProCgAddEntities[] = {0x83, 0xec, 0x20, 0x0f, 0x57, 0xc0};
typedef void(__cdecl* CgAddPlayerExtras_t)(int);
static CgAddPlayerExtras_t oCgAddPlayerExtras;
static void __cdecl HookCgAddPlayerExtras(int lc) {
    ShadowEntClientNumBegin();
    oCgAddPlayerExtras(lc);
    ShadowEntClientNumEnd();
}
static const uint8_t kProCgAddPlayerExtras[] = {0x8b, 0x44, 0x24, 0x04, 0x69, 0xc0, 0x04, 0x0b, 0x00, 0x00};

// p2_ent_anim_once=1: CG_AdvanceEntityAnims(lc) (0x00524ee0, called from CG_DrawActiveFrame at
// 0x005255ae) walks the snapshot entities (snap+0x2a18 count, numbers at +0x2a24, 0xf8 apart)
// and the client entities (mask 0x010628b0) and advances each DObj's animation by cg->frametime
// (CG_AdvanceDObjAnimByFrametime 0x004bb0b0), then runs its notetracks (footsteps, sounds).
// Every pass runs it, so zombies and the other player would animate at double speed
// and their notetrack sounds would come twice. It is not inside CG_AddPacketEntities, so
// p2_ent_frametime0 does not cover it. The second pass skips it; player 1's body in that pass
// has its own advance (AddPlayer1Body).
typedef void(__cdecl* CgAdvanceEntityAnims_t)(int);
static CgAdvanceEntityAnims_t oCgAdvanceEntityAnims;
static void __cdecl HookCgAdvanceEntityAnims(int lc) {
    if (g_inRenderViews && g_viewPass >= 1 && g_psSwapped && ReadIniInt(L"p2_ent_anim_once", 1)) return;
    oCgAdvanceEntityAnims(lc);
}
static const uint8_t kProCgAdvanceEntityAnims[] = {0x81, 0xec, 0x10, 0x05, 0x00, 0x00};

// p2_anim_log=N (diagnostic): N reports, one a second, of the animation advances of the
// players' DObjs (client handles 0 to 3): CG_AdvanceDObjAnimByFrametime 0x004bb0b0(cg, dobj,
// flags) advances by cg->frametime (cg+0x48088, ms). Per handle: calls and summed ms per view
// pass. A DObj at normal speed gets about 1000 ms a second.
typedef void(__cdecl* CgAdvanceDObjAnimHook_t)(uint8_t*, void*, int);
static CgAdvanceDObjAnimHook_t oCgAdvanceDObjAnim;
static void __cdecl HookCgAdvanceDObjAnim(uint8_t* cg, void* dobj, int flags) {
    static int left = -1, armed = -1;
    static DWORD next;
    static int calls[4][5], ms[4][5];
    {
        int v = ReadIniInt(L"p2_anim_log", 0);
        if (v != armed) {
            armed = v;
            left = v;
            next = GetTickCount() + 1000;
            memset(calls, 0, sizeof(calls));
            memset(ms, 0, sizeof(ms));
        }
    }
    bool skip = false;
    if (cg && dobj && g_inRenderViews && g_viewPass >= 1 && g_p1PredEntValid && ReadIniInt(L"p2_p1_anim_once", 1)) {
        int num = *(const int*)(g_p1PredEnt + 0x1dc);
        skip = num >= 0 && num < 18 && engine::Com_GetClientDObj(num, 0) == dobj;
    }
    if (left > 0 && cg && dobj && !skip) { // counts the advances that run
        int pass = g_inRenderViews ? (g_viewPass >= 0 && g_viewPass <= 3 ? g_viewPass : 4) : 4;
        for (int h = 0; h < 4; ++h)
            if (engine::Com_GetClientDObj(h, 0) == dobj) {
                ++calls[h][pass];
                ms[h][pass] += *(const int*)(cg + engine::kCgFrametime);
            }
        if ((LONG)(GetTickCount() - next) >= 0) {
            next = GetTickCount() + 1000;
            --left;
            for (int h = 0; h < 4; ++h)
                Log("[anim] client %d: pass0 %d calls %d ms | pass1 %d / %d | pass2 %d / %d | pass3 %d / %d | outside "
                    "%d / %d",
                    h, calls[h][0], ms[h][0], calls[h][1], ms[h][1], calls[h][2], ms[h][2], calls[h][3], ms[h][3],
                    calls[h][4], ms[h][4]);
            memset(calls, 0, sizeof(calls));
            memset(ms, 0, sizeof(ms));
        }
    }
    // p2_p1_anim_once=1: player 1's DObj advances in player 1's pass only. CG_DrawActiveFrame
    // advances the DObj of client handle 0 in every pass (call at 0x005257fe), and
    // AddPlayer1Body adds one more in pass 1: with four views that is 5000 ms of animation
    // a second for player 1's model (pass 0 once, pass 1 twice, passes 2 and 3 once each) against
    // 1000 ms for the other players (player 1 would animate two to three times too fast in
    // the other views, reload included).
    if (skip) return;
    oCgAdvanceDObjAnim(cg, dobj, flags);
}
static const uint8_t kProCgAdvanceDObjAnim[] = {0x8b, 0x44, 0x24, 0x0c, 0x8b, 0x4c, 0x24, 0x04};
// CG_DrawOverheadNames (0x004b3d50, cdecl(lc)): draws the name tags collected for this pass
// in the list 0x00fbc7f8 (16 bytes per entry: entity number, alpha, mode, squared distance;
// count 0x00fbd2f0). The collectors (0x004b3fa0, 0x004b4c10) walk the snapshot entities, and
// the snapshot is player 1's in both passes: the second pass would get a tag for player 2's own
// entity (floating in its own view) and none for player 1, whom the server never sends to
// itself. p2_tag=1: in the second pass the entry of player 2's entity becomes player 1's, with
// player 1's body entity (g_p1BodyEnt, the copy the body is drawn from) in the entity slot of
// player 1 for the duration of the draw; without that copy the entry is dropped. 0 = engine.
typedef void(__cdecl* DrawOverheadNames_t)(int);
static DrawOverheadNames_t oDrawOverheadNames;
static void __cdecl HookDrawOverheadNames(int lc) {
    int mode = ReadIniInt(L"p2_tag", 1);
    if (!(mode && lc == 0 && g_inRenderViews && g_viewPass >= 1 && g_cgSwapped && g_p2.cl)) {
        oDrawOverheadNames(lc);
        return;
    }
    int* count = (int*)engine::kOverheadNameCount;
    int* list = (int*)engine::kOverheadNameList;
    int p2num = *(const int*)(ShadowViewCl() + engine::kClPlayerState + engine::kPsClientNum);
    int p1num = g_p1PredEntValid ? *(const int*)(g_p1BodyEnt + 0x1dc) : -1;
    uint8_t* cents = *(uint8_t**)engine::kCgEntities;
    bool redirect = false;
    int n = 0;
    for (int i = 0; i < *count; ++i) {
        int* en = list + i * 4;
        if (en[0] == p2num) {
            if (p1num < 0 || p1num >= 18 || !cents || redirect || !ReadIniInt(L"p2_show_p1", 1)) continue; // dropped
            en[0] = p1num;
            redirect = true;
        }
        if (n != i) memcpy(list + n * 4, en, 16);
        ++n;
    }
    *count = n;
    if (!redirect) {
        oDrawOverheadNames(lc);
        return;
    }
    __declspec(align(16)) static uint8_t saved[engine::kCentSize];
    uint8_t* slot = cents + p1num * engine::kCentSize;
    memcpy(saved, slot, engine::kCentSize);
    memcpy(slot, g_p1BodyEnt, engine::kCentSize);
    oDrawOverheadNames(lc);
    memcpy(slot, saved, engine::kCentSize);
}
static const uint8_t kProDrawOverheadNames[] = {0xa1, 0xf0, 0xd2, 0xfb, 0x00, 0x85, 0xc0};

const Detour kViewEntityHooks[] = {
    {"CG_ScanForCrosshairEntity (a shadow player's trace skips its own body)", 0x004b35b0, kProCrosshairScan,
     sizeof(kProCrosshairScan), (void*)HookCrosshairScan, (void**)&oCrosshairScan},
    {"CG add entities (player 2's clientNum while adding)", 0x004c0ff0, kProCgAddEntities, sizeof(kProCgAddEntities),
     (void*)HookCgAddEntities, (void**)&oCgAddEntities},
    {"CG add player extras (player 2's clientNum while adding)", 0x0048fe80, kProCgAddPlayerExtras,
     sizeof(kProCgAddPlayerExtras), (void*)HookCgAddPlayerExtras, (void**)&oCgAddPlayerExtras},
    {"CG_AdvanceDObjAnimByFrametime (diagnostic p2_anim_log)", 0x004bb0b0, kProCgAdvanceDObjAnim,
     sizeof(kProCgAdvanceDObjAnim), (void*)HookCgAdvanceDObjAnim, (void**)&oCgAdvanceDObjAnim},
    {"CG advance entity animations (once per frame, not once per pass)", 0x00524ee0, kProCgAdvanceEntityAnims,
     sizeof(kProCgAdvanceEntityAnims), (void*)HookCgAdvanceEntityAnims, (void**)&oCgAdvanceEntityAnims},
    {"CG_DrawOverheadNames (second pass: player 1's tag instead of player 2's own)", 0x004b3d50, kProDrawOverheadNames,
     sizeof(kProDrawOverheadNames), (void*)HookDrawOverheadNames, (void**)&oDrawOverheadNames},
    {"CG_Player (diagnostics p2_body_trace)", 0x004e9650, kProCgPlayer, sizeof(kProCgPlayer), (void*)HookCgPlayer,
     (void**)&oCgPlayer},
    {"R_AddDObjToScene (diagnostics p2_body_trace)", 0x004106f0, kProAddDObjToScene, sizeof(kProAddDObjToScene),
     (void*)HookAddDObjToScene, (void**)&oAddDObjToScene},
};
