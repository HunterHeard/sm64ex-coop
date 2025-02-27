#include <map>
#include "dynos.cpp.h"

extern "C" {
#include "object_fields.h"
#include "game/level_update.h"
#include "game/object_list_processor.h"
#include "pc/configfile.h"
}

// Static maps/arrays
static std::map<const void*, ActorGfx>& DynosValidActors() {
    static std::map<const void*, ActorGfx> sDynosValidActors;
    return sDynosValidActors;
}

static Array<Pair<const char*, void *>>& DynosCustomActors() {
    static Array<Pair<const char*, void *>> sDynosCustomActors;
    return sDynosCustomActors;
}

// TODO: the cleanup/refactor didn't really go as planned.
//       clean up the actor management code more

void DynOS_Actor_AddCustom(const SysPath &aFilename, const char *aActorName) {

    const void* georef = DynOS_Builtin_Actor_GetFromName(aActorName);

    u16 actorLen = strlen(aActorName);
    char* actorName = (char*)calloc(1, sizeof(char) * (actorLen + 1));
    strcpy(actorName, aActorName);

    GfxData *_GfxData = DynOS_Actor_LoadFromBinary(aFilename, actorName, aFilename, false);
    if (!_GfxData) {
        Print("  ERROR: Couldn't load Actor Binary \"%s\" from \"%s\"", actorName, aFilename.c_str());
        free(actorName);
        return;
    }

    void* geoLayout = (*(_GfxData->mGeoLayouts.end() - 1))->mData;
    if (!geoLayout) {
        Print("  ERROR: Couldn't load geo layout for \"%s\"", actorName);
        free(actorName);
        return;
    }

    // Add to custom actors
    if (georef == NULL) {
        DynosCustomActors().Add({ actorName, geoLayout });
        georef = geoLayout;
    }

    // Alloc and init the actors gfx list
    ActorGfx actorGfx;
    actorGfx.mGfxData   = _GfxData;
    actorGfx.mGraphNode = (GraphNode *) DynOS_Geo_GetGraphNode(geoLayout, false);
    actorGfx.mPackIndex = 99;

    // Add to list
    DynOS_Actor_Valid(georef, actorGfx);
}

const void *DynOS_Actor_GetLayoutFromName(const char *aActorName) {
    if (aActorName == NULL) { return NULL; }

    // check levels
    auto& levelsArray = DynOS_Lvl_GetArray();
    for (auto& lvl : levelsArray) {
        for (auto& geo : lvl.second->mGeoLayouts) {
            if (geo->mName == aActorName) {
                return geo->mData;
            }
        }
    }

    // check custom actors
    for (auto& pair : DynosCustomActors()) {
        if (!strcmp(aActorName, pair.first)) {
            return pair.second;
        }
    }

    // check loaded actors
    for (auto& pair : DynosValidActors()) {
        for (auto& geo : pair.second.mGfxData->mGeoLayouts) {
            if (!strcmp(aActorName, geo->mName.begin())) {
                return geo->mData;
            }
        }
    }

    // check built in actors
    for (s32 i = 0; i < DynOS_Builtin_Actor_GetCount(); ++i) {
        auto name = DynOS_Builtin_Actor_GetNameFromIndex(i);
        if (!strcmp(aActorName, name)) {
            return DynOS_Builtin_Actor_GetFromIndex(i);
        }
    }

    return NULL;
}

ActorGfx* DynOS_Actor_GetActorGfx(const void* aGeoref) {
    if (aGeoref == NULL) { return NULL; }
    auto& _ValidActors = DynosValidActors();
    if (_ValidActors.count(aGeoref) == 0) { return NULL; }
    return &_ValidActors[aGeoref];
}

void DynOS_Actor_Valid(const void* aGeoref, ActorGfx& aActorGfx) {
    if (aGeoref == NULL) { return; }
    auto& _ValidActors = DynosValidActors();
    _ValidActors[aGeoref] = aActorGfx;
    DynOS_Tex_Valid(aActorGfx.mGfxData);
}

void DynOS_Actor_Invalid(const void* aGeoref, s32 aPackIndex) {
    if (aGeoref == NULL) { return; }
    auto& _ValidActors = DynosValidActors();
    if (_ValidActors.count(aGeoref) == 0) { return; }
    if (_ValidActors[aGeoref].mPackIndex != aPackIndex) { return; }

    DynOS_Tex_Invalid(_ValidActors[aGeoref].mGfxData);
    _ValidActors.erase(aGeoref);
}

void DynOS_Actor_Override(void** aSharedChild) {
    if ((aSharedChild == NULL) || (*aSharedChild == NULL)) { return; }

    const void* georef = (*(GraphNode**)aSharedChild)->georef;
    if (georef == NULL) { return; }

    auto& _ValidActors = DynosValidActors();
    if (_ValidActors.count(georef) == 0) { return; }

    *aSharedChild = (void*)_ValidActors[georef].mGraphNode;
}

void DynOS_Actor_Override_All(void) {
    if (!gObjectLists) { return; }
    // Loop through all object lists
    for (s32 list : { OBJ_LIST_PLAYER, OBJ_LIST_DESTRUCTIVE, OBJ_LIST_GENACTOR, OBJ_LIST_PUSHABLE, OBJ_LIST_LEVEL, OBJ_LIST_DEFAULT, OBJ_LIST_SURFACE, OBJ_LIST_POLELIKE, OBJ_LIST_UNIMPORTANT }) {
        struct Object *_Head = (struct Object *) &gObjectLists[list];
        for (struct Object *_Object = (struct Object *) _Head->header.next; _Object != _Head; _Object = (struct Object *) _Object->header.next) {
            if (_Object->header.gfx.sharedChild != NULL && _Object->header.gfx.sharedChild->georef != NULL) {
                GraphNode* georef = (GraphNode*)_Object->header.gfx.sharedChild->georef;
                _Object->header.gfx.sharedChild = (GraphNode *) DynOS_Geo_GetGraphNode(georef, true);
            }
            DynOS_Actor_Override((void**)&_Object->header.gfx.sharedChild);
        }
    }
}
