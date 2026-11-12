// The structures that the game reads from its files, or writes to saves and replays, as they are:
// their layout is the 32-bit build's on every target, so files load the same on all of them.
#include "Frontend.hpp"
#include "InputRecorder_67F8B0.hpp"
#include "PlyDat.hpp"
#include "RouteFinder.hpp"
#include "ScriptManager.hpp"
#include "ScriptThread.hpp"
#include "TextTable.hpp"
#include "chunk.hpp"
#include "gtx_0x106C.hpp"
#include "map_0x370.hpp"

#include <stddef.h>

#define CHECK_SIZE(type, size) static_assert(sizeof(type) == (size), "sizeof(" #type ") must be " #size)
#define CHECK_OFFSET(type, field, offset) static_assert(offsetof(type, field) == (offset), #type "::" #field " must be at " #offset)

// Saves, player data, replays
CHECK_SIZE(SaveData, 0x748);
CHECK_SIZE(SavedCarAndScriptData, 0x500);
CHECK_SIZE(SavedObjectData, 0x164);
CHECK_SIZE(SavedPlayerStats, 0x90);
CHECK_SIZE(SavedScriptCounter, 0x4);
CHECK_SIZE(SavedFix16, 0x4);
CHECK_SIZE(SaveSlotInfo, 0x52);
CHECK_SIZE(SaveSlotSummary, 0x4);
CHECK_SIZE(PlyDat, 0x2BC0);
CHECK_SIZE(PlySlot, 0xA4);
CHECK_SIZE(StageStats, 0xC);
CHECK_SIZE(HighScoreTable, 0xF0);
CHECK_SIZE(ScoreTableLine, 0x18);
CHECK_SIZE(BestStageStats, 0x28);
CHECK_SIZE(ReplayHeader, 0x10C);
CHECK_SIZE(InputRecord, 0xC);

// .gmp
CHECK_SIZE(gmp_block_info, 0xC);
CHECK_SIZE(gmp_dmap_info, 0x8);
CHECK_SIZE(DmapInfoList, 0x324);
CHECK_SIZE(gmp_map_object, 0x6);
CHECK_SIZE(gmp_map_light, 0x10);
CHECK_SIZE(gmp_zone_info, 0x16);
CHECK_SIZE(gmp_tile_animation, 0x8);
CHECK_SIZE(gmp_column_word, 0x4);
CHECK_SIZE(Junction, 0x10);
CHECK_SIZE(JunctionSegment, 0x8);
CHECK_SIZE(chunk_header, 0x8);

// .sty
CHECK_SIZE(delta_entry, 0x8);
CHECK_SIZE(delta_store_entry, 0x4);
CHECK_SIZE(font_base, 0x4);
CHECK_SIZE(object_info, 0x2);
CHECK_SIZE(palette_index, 0x8000);
CHECK_SIZE(tile_array, 0x800);
CHECK_SIZE(palette_base, 0x10);
CHECK_SIZE(sprite_base, 0xC);
CHECK_SIZE(car_info, 0x12);

// .gxt
#if defined(GTA2_PORT_64BIT)
CHECK_SIZE(TextKeyRecord, 0xC);
#else
CHECK_SIZE(TextKeyEntry, 0xC);
#endif

// .scr: the commands that hold an object in a 4-byte slot at 0x8, and the field after it
CHECK_SIZE(SCR_CMD_HEADER, 0x8);
CHECK_SIZE(SCR_POINTER, 0xC);
CHECK_SIZE(SCR_ARROW_ENTITY, 0xC);
CHECK_SIZE(SCR_PLAYER_PED, 0x1C);
CHECK_OFFSET(SCR_PLAYER_PED, mPos, 0xC);
CHECK_SIZE(SCR_CHAR_DATA_DEC, 0x20);
CHECK_OFFSET(SCR_CHAR_DATA_DEC, mPos, 0xC);
CHECK_SIZE(SCR_OBJ_DATA, 0x20);
CHECK_OFFSET(SCR_OBJ_DATA, mPos, 0xC);
CHECK_SIZE(SCR_THREAD, 0x18);
CHECK_OFFSET(SCR_THREAD, mThreadId, 0xC);
CHECK_SIZE(SCR_CHAR_AREA_ANY, 0x28);
CHECK_OFFSET(SCR_CHAR_AREA_ANY, mUnusedC, 0xC);
CHECK_SIZE(SCR_CRANE_TARGET_DEC, 0x28);
CHECK_OFFSET(SCR_CRANE_TARGET_DEC, mHomecrane, 0xC);
CHECK_SIZE(SCR_CRANE_BASIC_DEC, 0x30);
CHECK_OFFSET(SCR_CRANE_BASIC_DEC, mHomecrane, 0xC);
CHECK_SIZE(SCR_DOOR_DATA_DEC, 0x2C);
CHECK_OFFSET(SCR_DOOR_DATA_DEC, mBlock, 0xC);
