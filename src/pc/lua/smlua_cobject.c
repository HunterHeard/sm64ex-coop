#include "smlua.h"

#include "game/level_update.h"
#include "game/area.h"
#include "game/mario.h"
#include "game/hardcoded.h"
#include "audio/external.h"
#include "object_fields.h"
#include "pc/djui/djui_hud_utils.h"
#include "pc/lua/smlua.h"
#include "pc/lua/utils/smlua_anim_utils.h"
#include "pc/lua/utils/smlua_collision_utils.h"
#include "pc/lua/utils/smlua_obj_utils.h"
#include "pc/mods/mods.h"

#define LUA_VEC3S_FIELD_COUNT 3
static struct LuaObjectField sVec3sFields[LUA_VEC3S_FIELD_COUNT] = {
    { "x", LVT_S16, sizeof(s16) * 0, false, LOT_NONE },
    { "y", LVT_S16, sizeof(s16) * 1, false, LOT_NONE },
    { "z", LVT_S16, sizeof(s16) * 2, false, LOT_NONE },
};

#define LUA_VEC3F_FIELD_COUNT 3
static struct LuaObjectField sVec3fFields[LUA_VEC3F_FIELD_COUNT] = {
    { "x", LVT_F32, sizeof(f32) * 0, false, LOT_NONE },
    { "y", LVT_F32, sizeof(f32) * 1, false, LOT_NONE },
    { "z", LVT_F32, sizeof(f32) * 2, false, LOT_NONE },
};

struct LuaObjectTable sLuaObjectTable[LOT_MAX] = {
    { LOT_NONE,  NULL,         0                     },
    { LOT_VEC3S, sVec3sFields, LUA_VEC3S_FIELD_COUNT },
    { LOT_VEC3F, sVec3fFields, LUA_VEC3F_FIELD_COUNT },
};

struct LuaObjectField* smlua_get_object_field_from_ot(struct LuaObjectTable* ot, const char* key) {
    // binary search
    s32 min = 0;
    s32 max = ot->fieldCount - 1;
    s32 i = (min + max) / 2;
    while (true) {
        s32 rc = strcmp(key, ot->fields[i].key);
        if (rc == 0) {
            return &ot->fields[i];
        } else if (rc < 0) {
            max = i - 1;
            i = (min + max) / 2;
        } else if (rc > 0) {
            min = i + 1;
            i = (min + max) / 2;
        }

        if (min > max || max < min) {
            return NULL;
        }
    }

    return NULL;
}

struct LuaObjectField* smlua_get_object_field(u16 lot, const char* key) {
    if (lot > LOT_AUTOGEN_MIN) {
        return smlua_get_object_field_autogen(lot, key);
    }

    struct LuaObjectTable* ot = &sLuaObjectTable[lot];
    return smlua_get_object_field_from_ot(ot, key);
}

bool smlua_valid_lot(u16 lot) {
    if (lot > LOT_NONE && lot < LOT_MAX) { return true; }
    if (lot > LOT_AUTOGEN_MIN && lot < LOT_AUTOGEN_MAX) { return true; }
    return false;
}

bool smlua_valid_lvt(u16 lvt) {
    return (lvt < LVT_MAX);
}

  //////////////////
 // obj behavior //
//////////////////

#define CUSTOM_FIELD_MAX 11
#define CUSTOM_FIELD_ITEM_LEN 48
struct CustomFieldItem {
    char key[CUSTOM_FIELD_ITEM_LEN];
    enum LuaValueType lvt;
    struct CustomFieldItem* next;
};

static void smlua_add_custom_field_linked(struct CustomFieldItem** head, struct CustomFieldItem* item, const char* key, enum LuaValueType lvt) {
    snprintf(item->key, CUSTOM_FIELD_ITEM_LEN, "%s", key);
    item->lvt = lvt;
    item->next = NULL;

    if (*head == NULL) {
        item->next = *head;
        *head = item;
        return;
    }

    struct CustomFieldItem* prev = NULL;
    struct CustomFieldItem* node = *head;
    while (node != NULL) {
        if (strcmp(node->key, item->key) > 0) {
            if (prev == NULL) {
                item->next = *head;
                *head = item;
                return;
            } else {
                item->next = prev->next;
                prev->next = item;
                return;
            }
        }

        prev = node;
        node = node->next;
    }

    if (prev != NULL) {
        item->next = prev->next;
        prev->next = item;
    }
}

static int smlua_func_define_custom_obj_fields(lua_State* L) {
    LUA_STACK_CHECK_BEGIN();
    if (!smlua_functions_valid_param_count(L, 1)) { return 0; }

    if (lua_type(L, 1) != LUA_TTABLE) {
        LOG_LUA_LINE("Invalid parameter for define_custom_obj_fields()");
        return 0;
    }

    if (gLuaLoadingMod == NULL) {
        LOG_LUA_LINE("define_custom_obj_fields() can only be called on load.");
        return 0;
    }

    struct CustomFieldItem* customFieldsHead = NULL;
    struct CustomFieldItem customFields[CUSTOM_FIELD_MAX] = { 0 };
    u16 customFieldCount = 0;

    // get _custom_object_fields
    lua_getglobal(L, "_G"); // get global table
    lua_getfield(L, LUA_REGISTRYINDEX, gLuaLoadingMod->relativePath); // push file's "global" table
    int fileGlobalIndex = lua_gettop(L);
    lua_getfield(L, fileGlobalIndex, "_custom_object_fields");
    lua_remove(L, -2); // remove file's "global" table
    lua_remove(L, -2); // remove global table
    int customObjectFieldsIndex = lua_gettop(L);

    // table is in the stack at index 't'
    lua_pushnil(L);  // first key
    s32 iterationTop = lua_gettop(L);
    while (lua_next(L, 1) != 0) {
        int keyIndex = lua_gettop(L) - 1;
        int valueIndex = lua_gettop(L) - 0;
        // uses 'key' (at index -2) and 'value' (at index -1)
        if (lua_type(L, keyIndex) != LUA_TSTRING) {
            LOG_LUA_LINE("Invalid key type for define_custom_obj_fields() : %u", lua_type(L, keyIndex));
            lua_settop(L, iterationTop);
            continue;
        }

        if (lua_type(L, valueIndex) != LUA_TSTRING) {
            LOG_LUA_LINE("Invalid value type for define_custom_obj_fields() : %u", lua_type(L, valueIndex));
            lua_settop(L, iterationTop);
            continue;
        }

        const char* key = smlua_to_string(L, keyIndex);
        if (key[0] != 'o') {
            LOG_LUA_LINE("Invalid key name for define_custom_obj_fields()");
            lua_settop(L, iterationTop);
            continue;
        }
        if (strlen(key) >= CUSTOM_FIELD_ITEM_LEN) {
            LOG_LUA_LINE("Too long of key name for define_custom_obj_fields()");
            lua_settop(L, iterationTop);
            continue;
        }

        const char* value = smlua_to_string(L, valueIndex);
        enum LuaValueType lvt = LVT_U32;
        if (!strcmp(value, "u32")) { lvt = LVT_U32; }
        else if (!strcmp(value, "s32")) { lvt = LVT_S32; }
        else if (!strcmp(value, "f32")) { lvt = LVT_F32; }
        else {
            LOG_LUA_LINE("Invalid value name for define_custom_obj_fields()");
            return 0;
        }

        if (customFieldCount >= CUSTOM_FIELD_MAX) {
            LOG_LUA_LINE("Ran out of custom fields!");
            return 0;
        }

        smlua_add_custom_field_linked(&customFieldsHead, &customFields[customFieldCount], key, lvt);
        customFieldCount++;

        lua_settop(L, iterationTop);
    }

    lua_settop(L, iterationTop);

    struct CustomFieldItem* node = customFieldsHead;
    u32 fieldIndex = 0x1B;
    while (node != NULL) {
        // keep fieldIndex in range
        if (fieldIndex < 0x1B) {
            fieldIndex = 0x1B;
        } else if (fieldIndex > 0x22 && fieldIndex < 0x48) {
            fieldIndex = 0x48;
        } else if (fieldIndex > 0x4A) {
            LOG_LUA_LINE("Ran out of custom fields!");
            return 0;
        }

        lua_pushvalue(L, customObjectFieldsIndex);
        lua_pushstring(L, node->key);
        lua_newtable(L);
        {
            // set fieldIndex
            lua_pushstring(L, "_fieldIndex");
            lua_pushinteger(L, fieldIndex);
            lua_rawset(L, -3);

            // set lvt
            lua_pushstring(L, "_lvt");
            lua_pushinteger(L, node->lvt);
            lua_rawset(L, -3);
        }
        lua_settable(L, -3); // set _custom_object_fields

        fieldIndex++;

        node = node->next;
        lua_settop(L, iterationTop);
    }

    lua_pop(L, 1); // pop key
    lua_pop(L, 1); // pop _custom_object_fields

    LUA_STACK_CHECK_END();
    return 1;
}

struct LuaObjectField* smlua_get_custom_field(lua_State* L, u32 lot, int keyIndex) {
    LUA_STACK_CHECK_BEGIN();
    static struct LuaObjectField lof = { 0 };
    if (lot != LOT_OBJECT) { return NULL; }

    if (gLuaActiveMod == NULL) {
        LOG_LUA_LINE("Failed to retrieve active mod entry.");
        return NULL;
    }

    // get _custom_object_fields
    lua_getglobal(L, "_G"); // get global table
    lua_getfield(L, LUA_REGISTRYINDEX, gLuaActiveMod->relativePath); // push file's "global" table
    int fileGlobalIndex = lua_gettop(L);
    lua_getfield(L, fileGlobalIndex, "_custom_object_fields");
    lua_remove(L, -2); // remove file's "global" table
    lua_remove(L, -2); // remove global table

    // get value table from key
    lua_pushvalue(L, keyIndex);
    lua_rawget(L, -2);
    if (lua_type(L, -1) != LUA_TTABLE) {
        lua_pop(L, 1); // pop value table
        lua_pop(L, 1); // pop _custom_fields
        LUA_STACK_CHECK_END();
        return NULL;
    }

    // get _fieldIndex
    lua_pushstring(L, "_fieldIndex");
    lua_rawget(L, -2);
    u32 fieldIndex = smlua_to_integer(L, -1);
    lua_pop(L, 1);
    bool validFieldIndex = (fieldIndex >= 0x1B && fieldIndex <= 0x22) || (fieldIndex >= 0x48 && fieldIndex <= 0x4A);
    if (!gSmLuaConvertSuccess || !validFieldIndex) {
        lua_pop(L, 1); // pop value table
        lua_pop(L, 1); // pop _custom_fields
        LUA_STACK_CHECK_END();
        return NULL;
    }

    // get _lvt
    lua_pushstring(L, "_lvt");
    lua_rawget(L, -2);
    u32 lvt = smlua_to_integer(L, -1);
    lua_pop(L, 1);
    bool validLvt = (lvt == LVT_U32 || lvt == LVT_S32 || lvt == LVT_F32);
    if (!gSmLuaConvertSuccess || !validLvt) {
        lua_pop(L, 1); // pop value table
        lua_pop(L, 1); // pop _custom_fields
        LUA_STACK_CHECK_END();
        return NULL;
    }

    lof.immutable = false;
    //lof.key = key;
    lof.lot = LOT_NONE;
    lof.valueOffset = offsetof(struct Object, rawData.asU32[fieldIndex]);
    lof.valueType = lvt;

    lua_pop(L, 1); // pop value table
    lua_pop(L, 1); // pop _custom_fields

    LUA_STACK_CHECK_END();
    return &lof;
}

  /////////////////////
 // CObject get/set //
/////////////////////

static int smlua__get_field(lua_State* L) {
    LUA_STACK_CHECK_BEGIN();
    if (!smlua_functions_valid_param_count(L, 4)) { return 0; }

    enum LuaObjectType lot = smlua_to_integer(L, 1);
    if (!gSmLuaConvertSuccess) { return 0; }

    u64 pointer = smlua_to_integer(L, 2);
    if (!gSmLuaConvertSuccess) { return 0; }

    const char* key = smlua_to_string(L, 3);
    if (!gSmLuaConvertSuccess) {
        LOG_LUA_LINE("Tried to get a non-string field of cobject");
        return 0;
    }

    if (pointer == 0) {
        LOG_LUA_LINE("_get_field on null pointer");
        return 0;
    }

    if (!smlua_valid_lot(lot)) {
        LOG_LUA_LINE("_get_field on invalid LOT '%u'", lot);
        return 0;
    }

    if (!smlua_cobject_allowlist_contains(lot, pointer)) {
        LOG_LUA_LINE("_get_field received a pointer not in allow list. '%u', '%llu", lot, (u64)pointer);
        return 0;
    }

    struct LuaObjectField* data = smlua_get_object_field(lot, key);
    if (data == NULL) {
        data = smlua_get_custom_field(L, lot, 3);
    }
    if (data == NULL) {
        LOG_LUA_LINE("_get_field on invalid key '%s', lot '%d'", key, lot);
        return 0;
    }

    LUA_STACK_CHECK_END();

    u8* p = ((u8*)(intptr_t)pointer) + data->valueOffset;
    switch (data->valueType) {
        case LVT_BOOL:              lua_pushboolean(L, *(u8* )p);              break;
        case LVT_U8:                lua_pushinteger(L, *(u8* )p);              break;
        case LVT_U16:               lua_pushinteger(L, *(u16*)p);              break;
        case LVT_U32:               lua_pushinteger(L, *(u32*)p);              break;
        case LVT_S8:                lua_pushinteger(L, *(s8* )p);              break;
        case LVT_S16:               lua_pushinteger(L, *(s16*)p);              break;
        case LVT_S32:               lua_pushinteger(L, *(s32*)p);              break;
        case LVT_F32:               lua_pushnumber( L, *(f32*)p);              break;
        case LVT_COBJECT:           smlua_push_object(L, data->lot, p);        break;
        case LVT_COBJECT_P:         smlua_push_object(L, data->lot, *(u8**)p); break;
        case LVT_STRING:            lua_pushstring(L, (char*)p);               break;
        case LVT_STRING_P:          lua_pushstring(L, *(char**)p);             break;
        case LVT_BEHAVIORSCRIPT:    lua_pushinteger(L, *(s32*)p);              break;
        case LVT_OBJECTANIMPOINTER: lua_pushinteger(L, *(s32*)p);              break;
        case LVT_COLLISION:         lua_pushinteger(L, *(s32*)p);              break;
        case LVT_LEVELSCRIPT:       lua_pushinteger(L, *(s32*)p);              break;
        case LVT_TRAJECTORY:        lua_pushinteger(L, *(s16*)p);              break;

        // pointers
        case LVT_U8_P:
        case LVT_U16_P:
        case LVT_U32_P:
        case LVT_S8_P:
        case LVT_S16_P:
        case LVT_S32_P:
        case LVT_F32_P:
        case LVT_BEHAVIORSCRIPT_P:
        case LVT_OBJECTANIMPOINTER_P:
        case LVT_COLLISION_P:
        case LVT_LEVELSCRIPT_P:
        case LVT_TRAJECTORY_P:
            smlua_push_pointer(L, data->valueType, *(u8**)p);
            break;

        default:
            LOG_LUA_LINE("_get_field on unimplemented type '%d', key '%s'", data->valueType, key);
            return 0;
    }

    return 1;
}

static int smlua__set_field(lua_State* L) {
    LUA_STACK_CHECK_BEGIN();
    if (!smlua_functions_valid_param_count(L, 5)) { return 0; }

    enum LuaObjectType lot = smlua_to_integer(L, 1);
    if (!gSmLuaConvertSuccess) { return 0; }

    u64 pointer = smlua_to_integer(L, 2);
    if (!gSmLuaConvertSuccess) { return 0; }

    const char* key = smlua_to_string(L, 3);
    if (!gSmLuaConvertSuccess) {
        LOG_LUA_LINE("Tried to set a non-string field of cobject");
        return 0;
    }

    if (pointer == 0) {
        LOG_LUA_LINE("_set_field on null pointer");
        return 0;
    }

    if (!smlua_valid_lot(lot)) {
        LOG_LUA_LINE("_set_field on invalid LOT '%u'", lot);
        return 0;
    }

    if (!smlua_cobject_allowlist_contains(lot, pointer)) {
        LOG_LUA_LINE("_set_field received a pointer not in allow list. '%u', '%llu", lot, (u64)pointer);
        return 0;
    }

    struct LuaObjectField* data = smlua_get_object_field(lot, key);
    if (data == NULL) {
        data = smlua_get_custom_field(L, lot, 3);
    }

    if (data == NULL) {
        LOG_LUA_LINE("_set_field on invalid key '%s'", key);
        return 0;
    }

    if (data->immutable) {
        LOG_LUA_LINE("_set_field on immutable key '%s'", key);
        return 0;
    }

    void* valuePointer = NULL;
    u8* p = ((u8*)(intptr_t)pointer) + data->valueOffset;
    switch (data->valueType) {
        case LVT_BOOL:*(u8*) p = smlua_to_boolean(L, 4); break;
        case LVT_U8:  *(u8*) p = smlua_to_integer(L, 4); break;
        case LVT_U16: *(u16*)p = smlua_to_integer(L, 4); break;
        case LVT_U32: *(u32*)p = smlua_to_integer(L, 4); break;
        case LVT_S8:  *(s8*) p = smlua_to_integer(L, 4); break;
        case LVT_S16: *(s16*)p = smlua_to_integer(L, 4); break;
        case LVT_S32: *(s32*)p = smlua_to_integer(L, 4); break;
        case LVT_F32: *(f32*)p = smlua_to_number(L, 4);  break;

        case LVT_COBJECT_P:
            valuePointer = smlua_to_cobject(L, 4, data->lot);
            if (gSmLuaConvertSuccess) {
                *(u8**)p = valuePointer;
            }
            break;

        // pointers
        case LVT_U8_P:
        case LVT_U16_P:
        case LVT_U32_P:
        case LVT_S8_P:
        case LVT_S16_P:
        case LVT_S32_P:
        case LVT_F32_P:
        case LVT_BEHAVIORSCRIPT_P:
        case LVT_OBJECTANIMPOINTER_P:
        case LVT_COLLISION_P:
        case LVT_TRAJECTORY_P:
            valuePointer = smlua_to_cpointer(L, 4, data->valueType);
            if (gSmLuaConvertSuccess) {
                *(u8**)p = valuePointer;
            }
            break;

        default:
            LOG_LUA_LINE("_set_field on unimplemented type '%d', key '%s'", data->valueType, key);
            return 0;
    }
    if (!gSmLuaConvertSuccess) {
        LOG_LUA_LINE("_set_field failed to retrieve value type '%d', key '%s'", data->valueType, key);
        return 0;
    }

    LUA_STACK_CHECK_END();
    return 1;
}

  //////////
 // bind //
//////////

void smlua_cobject_init_globals(void) {
    lua_State* L = gLuaState;

    {
        lua_newtable(L);
        int t = lua_gettop(gLuaState);
        for (s32 i = 0; i < MAX_PLAYERS; i++) {
            lua_pushinteger(L, i);
            smlua_push_object(L, LOT_MARIOSTATE, &gMarioStates[i]);
            lua_settable(L, t);
        }
        lua_setglobal(L, "gMarioStates");
    }

    {
        lua_newtable(L);
        int t = lua_gettop(gLuaState);
        for (s32 i = 0; i < MAX_PLAYERS; i++) {
            lua_pushinteger(L, i);
            smlua_push_object(L, LOT_NETWORKPLAYER, &gNetworkPlayers[i]);
            lua_settable(L, t);
        }
        lua_setglobal(L, "gNetworkPlayers");
    }

    {
        lua_newtable(L);
        int t = lua_gettop(gLuaState);
        for (s32 i = 0; i < gActiveMods.entryCount; i++) {
            lua_pushinteger(L, i);
            smlua_push_object(L, LOT_MOD, gActiveMods.entries[i]);
            lua_settable(L, t);
        }
        lua_setglobal(L, "gActiveMods");
    }

    {
        lua_newtable(L);
        int t = lua_gettop(gLuaState);
        for (s32 i = 0; i < CT_MAX; i++) {
            lua_pushinteger(L, i);
            smlua_push_object(L, LOT_CHARACTER, &gCharacters[i]);
            lua_settable(L, t);
        }
        lua_setglobal(L, "gCharacters");
    }

    {
        smlua_push_object(L, LOT_GLOBALTEXTURES, &gGlobalTextures);
        lua_setglobal(L, "gTextures");
    }

    {
        smlua_push_object(L, LOT_GLOBALOBJECTANIMATIONS, &gGlobalObjectAnimations);
        lua_setglobal(L, "gObjectAnimations");
    }

    {
        smlua_push_object(L, LOT_GLOBALOBJECTCOLLISIONDATA, &gGlobalObjectCollisionData);
        lua_setglobal(L, "gGlobalObjectCollisionData");
    }

    {
        smlua_push_object(L, LOT_LAKITUSTATE, &gLakituState);
        lua_setglobal(L, "gLakituState");
    }

    {
        smlua_push_object(L, LOT_SERVERSETTINGS, &gServerSettings);
        lua_setglobal(L, "gServerSettings");
    }

    {
        smlua_push_object(L, LOT_LEVELVALUES, &gLevelValues);
        lua_setglobal(L, "gLevelValues");
    }

    {
        smlua_push_object(L, LOT_BEHAVIORVALUES, &gBehaviorValues);
        lua_setglobal(L, "gBehaviorValues");
    }

}

void smlua_cobject_init_per_file_globals(char* path) {
    lua_State* L = gLuaState;

    lua_getfield(L, LUA_REGISTRYINDEX, path); // push per-file globals

    {
        lua_pushstring(L, "_custom_object_fields");
        lua_newtable(L);
        lua_settable(L, -3);
    }

    lua_pop(L, 1); // pop per-file globals
}

void smlua_bind_cobject(void) {
    lua_State* L = gLuaState;

    smlua_bind_function(L, "define_custom_obj_fields", smlua_func_define_custom_obj_fields);

    smlua_bind_function(L, "_get_field", smlua__get_field);
    smlua_bind_function(L, "_set_field", smlua__set_field);

}
