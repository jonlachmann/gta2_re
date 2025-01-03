#include "miss2_0x11c.hpp"
#include "Globals.hpp"
#include "miss2_8.hpp"

#include "Car_BC.hpp"
#include "cool_nash_0x294.hpp"

#if defined(EXPORT_VARS) || defined(IMPORT_VARS)
EXPORT_VAR s16 word_6212EE;
GLOBAL(word_6212EE, 0x6212EE);
#else
s16 word_6212EE = 1;
#endif

EXPORT_VAR Miss2_8EC* gMiss2_8EC_6F8064;
GLOBAL(gMiss2_8EC_6F8064, 0x6F8064);

STUB_FUNC(0x503200)
void miss2_0x11C::sub_503200()
{
}

STUB_FUNC(0x503410)
char_type miss2_0x11C::sub_503410(u32 a1)
{
    return 0;
}

MATCH_FUNC(0x5035b0)
void miss2_0x11C::sub_5035B0()
{
    field_118 = 0;
    field_11A = word_6212EE++;
}

STUB_FUNC(0x5035d0)
void miss2_0x11C::sub_5035D0()
{
}

EXPORT_VAR s32 dword_6F806C;
GLOBAL(dword_6F806C, 0x6F806C);

MATCH_FUNC(0x503620)
void miss2_0x11C::Next_503620(SCR_CMD_HEADER* a2)
{
	if ( (u16) a2->field_4_cmd_next != 0xFFFF ) {        // FF FF (low endian) is the script terminator
        dword_6F806C = this->field_4_level_start;
        this->field_4_level_start = a2->field_4_cmd_next;
        this->field_C = 0;
    } else {
        miss2_0x11C::sub_503670();
    }
}

MATCH_FUNC(0x503650)
void miss2_0x11C::sub_503650(u16 a2)
{
	if ( a2 != 0xFFFF ) {
        this->field_4_level_start = a2;
    } else {
        miss2_0x11C::sub_503670();
    }
}

STUB_FUNC(0x503670)
void miss2_0x11C::sub_503670()
{
}

STUB_FUNC(0x503680)
s32 miss2_0x11C::SCRCMD_OBJ_DECSET_2D_3D_503680(s32* a1, s32 a2)
{
    return 0;
}

STUB_FUNC(0x5038d0)
void miss2_0x11C::SCRCMD_OBJ_DECSET_5038D0(s32* a1, s32 a2)
{
}

STUB_FUNC(0x503a20)
void miss2_0x11C::SCRCMD_PLAYER_PED_503A20(SCR_PLAYER_PED* pCmd)
{
}

STUB_FUNC(0x503bc0)
void miss2_0x11C::SCRCMD_CAR_DECSET_503BC0(s32* a1, s32 a2)
{
}

MATCH_FUNC(0x503f80)
void miss2_0x11C::SCRCMD_PARKED_CAR_DECSET_503F80(s32 a1)
{
	s32 v1;
    miss2_0x11C::SCRCMD_CAR_DECSET_503BC0((s32 *)a1, a1);
    (*(Car_BC **)(a1 + 8))->sub_443EB0(9);
    v1 = *(s32*)(a1 + 8);
    *(s32*)(v1 + 124) = 4;
    *(s16*)(v1 + 118) = 0;
}

STUB_FUNC(0x503fb0)
void miss2_0x11C::SCRCMD_CHAR_DECSET_2D_3D_503FB0(s32* a1, s32 a2)
{
}

MATCH_FUNC(0x504110)
s32 miss2_0x11C::sub_504110(s32 a1, s32 a2)
{
    s32 v2;
    s32 v3;
	
    (*(cool_nash_0x294 **)(a2 + 8))->sub_463570(
		*(s16*)(a1 + 10),
		9999);
    v2 = *(s32 *)(a2 + 8);
	
    v3 = *(s32 *)(v2 + 540);
    v3 &= ~0x400u;

    *(s32 *)(v2 + 540) = v3;
    return v2;
}

STUB_FUNC(0x504150)
void miss2_0x11C::SCRCMD_MAP_ZONE_SET_504150(s16* a1)
{
}

STUB_FUNC(0x5041b0)
s32 miss2_0x11C::SCRCMD_ARROW_DEC_5041B0(s32 a1)
{
    return 0;
}

STUB_FUNC(0x5041c0)
s32 miss2_0x11C::SCRCMD_CRANE_5041C0(s32 a1, s32 a2)
{
    return 0;
}

STUB_FUNC(0x5043a0)
char_type miss2_0x11C::SCRCMD_CONVEYOR_DECSET1_2_5043A0(s32* a1, s32 a2)
{
    return 0;
}

STUB_FUNC(0x504420)
Maccies_2C* miss2_0x11C::SCRCMD_GENERATOR_DECSET_504420(s32* a1, s32 a2)
{
    return 0;
}

STUB_FUNC(0x504530)
s32* miss2_0x11C::SCRCMD_DESTRUCTOR_DECSET_504530(s32* a1, s32 a2)
{
    return 0;
}

STUB_FUNC(0x5045a0)
Snooky_30* miss2_0x11C::SCRCMD_CRUSHER_BASIC_5045A0(s32 a1, s32 a2)
{
    return 0;
}

STUB_FUNC(0x5045d0)
s32 miss2_0x11C::SCRCMD_THREAD_DECLARE2_5045D0(s32 a1, s16* a2)
{
    return 0;
}

STUB_FUNC(0x504660)
void miss2_0x11C::SCRCMD_THREAD_DECLARE3_504660(miss2_0x11C* a1, s32 a2)
{
}

STUB_FUNC(0x504710)
void miss2_0x11C::sub_504710(s32 a1)
{
}

STUB_FUNC(0x5047c0)
s32* miss2_0x11C::SCRCMD_THREAD_DECLARE4_5047C0(s32 a1, s16* a2)
{
    return 0;
}

STUB_FUNC(0x504830)
void miss2_0x11C::SCRCMD_SET_GANG_INFO1_504830(s32 a1)
{
}

STUB_FUNC(0x504950)
s16 miss2_0x11C::SCRCMD_SET_DOOR_INFO_504950(s32 a1)
{
    return 0;
}

STUB_FUNC(0x504970)
s32 miss2_0x11C::SCRCMD_DOOR_DECLARE_D1_S1_504970(s32)
{
    return 0;
}

STUB_FUNC(0x504b80)
void miss2_0x11C::SCRCMD_DOOR_DECLARE_D2_S2_504B80(s32 a1, s32 a2, s32 a3)
{
}

STUB_FUNC(0x504dd0)
void miss2_0x11C::SCRCMD_DECLARE_MISSION_504DD0(u16* a1)
{
}

STUB_FUNC(0x504ee0)
void miss2_0x11C::sub_504EE0(s32 a1, s32 a2)
{
}

STUB_FUNC(0x505030)
void miss2_0x11C::SCRCMD_SET_STATION_EMPTY_STATION_505030(s32 a1)
{
}

STUB_FUNC(0x5051d0)
void miss2_0x11C::SCRCMD_RADIOSTATION_DEC_5051D0(s32 a1)
{
}

STUB_FUNC(0x505210)
void miss2_0x11C::CRCMD_SET_TRAIN_STATIONS_505210(s32 a1)
{
}

STUB_FUNC(0x5052c0)
void miss2_0x11C::SCRCMD_OBJ_DECSET_2D_STR_5052C0(s32 a1)
{
}

STUB_FUNC(0x505340)
void miss2_0x11C::SCRCMD_SOUND_DECSET_505340(s32 a1, s32 a2)
{
}

STUB_FUNC(0x505430)
void miss2_0x11C::SCRCMD_SET_MISSIONS_TOTAL_505430(s32 a1)
{
}

STUB_FUNC(0x505580)
void miss2_0x11C::SCRCMD_PASSED_FAILED_FLAGS_505580(s32 a1)
{
}

STUB_FUNC(0x505710)
void miss2_0x11C::SCRCMD_FINISH_SCORE_505710(s32 a1)
{
}

STUB_FUNC(0x505750)
void miss2_0x11C::SCRCMD_DECLARE_CARLIST_505750(s32 a1)
{
}

STUB_FUNC(0x505790)
s32 miss2_0x11C::sub_505790(u16 a1)
{
    return 0;
}

STUB_FUNC(0x505b10)
void miss2_0x11C::sub_505B10(u16 idx)
{
}

STUB_FUNC(0x505ea0)
u16 miss2_0x11C::sub_505EA0(u16 idx)
{
    return 0;
}

STUB_FUNC(0x505f50)
void miss2_0x11C::SCRCMD_DISPLAY_MESSAGES_505F50(s32 a1)
{
}

STUB_FUNC(0x506010)
s16 miss2_0x11C::SCRCMD_DEC_DEATH_BASE_506010(s32 a1)
{
    return 0;
}

STUB_FUNC(0x5060d0)
SCR_CMD_HEADER* miss2_0x11C::SCRCMD_DO_CRANE_POWERUP_5060D0(u16* a1)
{
    return 0;
}

STUB_FUNC(0x506140)
void miss2_0x11C::SCRCMD_START_BASIC_KF_506140(u16* a1)
{
}

STUB_FUNC(0x5061c0)
void miss2_0x11C::ExecOpCode_5061C0()
{
}

STUB_FUNC(0x5069c0)
void miss2_0x11C::SCRCMD_LEVELSTART_5069C0()
{
}

STUB_FUNC(0x5069f0)
void miss2_0x11C::SCRCMD_LEVELEND_5069F0()
{
}

STUB_FUNC(0x506a00)
void miss2_0x11C::SCRCMD_CREATE_THREAD_506A00()
{
}

STUB_FUNC(0x506a60)
void miss2_0x11C::SCRCMD_STOP_THREAD_506A60()
{
}

STUB_FUNC(0x506af0)
void miss2_0x11C::SCRCMD_IF_JUMP_506AF0()
{
}

STUB_FUNC(0x506b30)
void miss2_0x11C::sub_506B30()
{
}

STUB_FUNC(0x506b80)
void miss2_0x11C::sub_506B80()
{
}

STUB_FUNC(0x506bc0)
char_type miss2_0x11C::sub_506BC0(u32 a1)
{
    return 0;
}

STUB_FUNC(0x506d60)
void miss2_0x11C::sub_506D60()
{
}

STUB_FUNC(0x506ed0)
void miss2_0x11C::sub_506ED0()
{
}

STUB_FUNC(0x507110)
void miss2_0x11C::sub_507110()
{
}

STUB_FUNC(0x507750)
void miss2_0x11C::SCRCMD_NOT_507750()
{
}

STUB_FUNC(0x5078d0)
void miss2_0x11C::SCRCMD_START_EXEC_5078D0()
{
}

STUB_FUNC(0x5079a0)
void miss2_0x11C::SCRCMD_STOP_EXEC_5079A0()
{
}

STUB_FUNC(0x507a70)
void miss2_0x11C::SCRCMD_INCREMENT_507A70()
{
}

STUB_FUNC(0x507b50)
void miss2_0x11C::SCRCMD_DECREMENT_507B50()
{
}

STUB_FUNC(0x507ce0)
void miss2_0x11C::sub_507CE0()
{
}

STUB_FUNC(0x507f80)
void miss2_0x11C::sub_507F80()
{
}

STUB_FUNC(0x508220)
void miss2_0x11C::SCRCMD_MAKE_CAR_DUMMY_508220()
{
}

STUB_FUNC(0x508280)
void miss2_0x11C::sub_508280()
{
}

STUB_FUNC(0x508550)
void miss2_0x11C::sub_508550()
{
}

STUB_FUNC(0x5086f0)
void miss2_0x11C::sub_5086F0()
{
}

STUB_FUNC(0x508dc0)
void miss2_0x11C::SCRCMD_ARROW_COLOUR_508DC0()
{
}

STUB_FUNC(0x508e80)
void miss2_0x11C::SCRCMD_REMOVE_ARROW_508E80()
{
}

STUB_FUNC(0x508f00)
void miss2_0x11C::SCRCMD_CHECK_CAR_DAMAGE_508F00()
{
}

STUB_FUNC(0x509030)
void miss2_0x11C::SCRCMD_CHECK_HEALTH_509030()
{
}

STUB_FUNC(0x509180)
void miss2_0x11C::SCRCMD_STORE_CAR_INFO_509180()
{
}

STUB_FUNC(0x5093c0)
void miss2_0x11C::sub_5093C0()
{
}

STUB_FUNC(0x5096e0)
void miss2_0x11C::SCRCMD_DISPLAY_TIMER_5096E0()
{
}

STUB_FUNC(0x509730)
void miss2_0x11C::SCR_CMD_CREATE_CHAR_509730()
{
}

STUB_FUNC(0x509790)
void miss2_0x11C::sub_509790()
{
}

STUB_FUNC(0x5097d0)
void miss2_0x11C::sub_5097D0()
{
}

STUB_FUNC(0x509810)
void miss2_0x11C::sub_509810()
{
}

STUB_FUNC(0x509880)
void miss2_0x11C::sub_509880()
{
}

STUB_FUNC(0x5098e0)
void miss2_0x11C::sub_5098E0()
{
}

STUB_FUNC(0x509990)
void miss2_0x11C::sub_509990()
{
}

STUB_FUNC(0x509a70)
void miss2_0x11C::SCRCMD_CAR_IN_AREA_509A70()
{
}

STUB_FUNC(0x509bb0)
void miss2_0x11C::SCRCMD_HAS_CHAR_DIED_509BB0()
{
}

STUB_FUNC(0x509c10)
void miss2_0x11C::sub_509C10()
{
}

STUB_FUNC(0x509c90)
void miss2_0x11C::sub_509C90()
{
}

STUB_FUNC(0x509d00)
void miss2_0x11C::sub_509D00()
{
}

STUB_FUNC(0x509d60)
void miss2_0x11C::sub_509D60()
{
}

STUB_FUNC(0x509d90)
void miss2_0x11C::sub_509D90()
{
}

STUB_FUNC(0x509e00)
void miss2_0x11C::SCRCMD_ADD_SCORE2_509E00()
{
}

STUB_FUNC(0x509e70)
void miss2_0x11C::sub_509E70()
{
}

STUB_FUNC(0x509ed0)
void miss2_0x11C::sub_509ED0()
{
}

STUB_FUNC(0x509f60)
void miss2_0x11C::SCRCMD_EXPLODE_BUILDING_509F60()
{
}

STUB_FUNC(0x509fd0)
void miss2_0x11C::sub_509FD0()
{
}

STUB_FUNC(0x50a200)
void miss2_0x11C::sub_50A200()
{
}

STUB_FUNC(0x50a3e0)
void miss2_0x11C::sub_50A3E0()
{
}

STUB_FUNC(0x50a460)
void miss2_0x11C::sub_50A460()
{
}

STUB_FUNC(0x50a570)
void miss2_0x11C::SCRCMD_CHANGE_CAR_REMAP_50A570()
{
}

STUB_FUNC(0x50a5b0)
void miss2_0x11C::SCRCMD_CHANGE_CHAR_REMAP_50A5B0()
{
}

STUB_FUNC(0x50a610)
void miss2_0x11C::SCRCMD_CHECK_CAR_MODEL_50A610()
{
}

STUB_FUNC(0x50a670)
void miss2_0x11C::SCRCMD_CHECK_CAR_REMAP_50A670()
{
}

STUB_FUNC(0x50a6e0)
void miss2_0x11C::SCRCMD_CHECK_CAR_BOTH_50A6E0()
{
}

STUB_FUNC(0x50a760)
void miss2_0x11C::sub_50A760(miss2_0x11C* a1)
{
}

STUB_FUNC(0x50a940)
void miss2_0x11C::SCRCMD_DELAY_HERE_50A940()
{
}

STUB_FUNC(0x50a980)
void miss2_0x11C::sub_50A980()
{
}

STUB_FUNC(0x50a9e0)
void miss2_0x11C::sub_50A9E0(u16 idx)
{
}

STUB_FUNC(0x50abc0)
void miss2_0x11C::SCRCMD_DISABLE_THREAD_50ABC0()
{
}

STUB_FUNC(0x50abf0)
void miss2_0x11C::SCRCMD_ENABLE_THREAD_50ABF0()
{
}

STUB_FUNC(0x50ac20)
void miss2_0x11C::SCRCMD_SET_GANG_RESPECT_50AC20()
{
}

STUB_FUNC(0x50acf0)
void miss2_0x11C::sub_50ACF0()
{
}

STUB_FUNC(0x50aef0)
void miss2_0x11C::sub_50AEF0()
{
}

STUB_FUNC(0x50b0e0)
void miss2_0x11C::sub_50B0E0()
{
}

STUB_FUNC(0x50b150)
void miss2_0x11C::SCRCMD_SET_AMBIENT_50B150()
{
}

STUB_FUNC(0x50b180)
void miss2_0x11C::sub_50B180()
{
}

STUB_FUNC(0x50b230)
void miss2_0x11C::SCRCMD_CHECK_PHONE_50B230()
{
}

STUB_FUNC(0x50b2c0)
void miss2_0x11C::SCRCMD_CHECK_PHONETIMER_50B2C0()
{
}

STUB_FUNC(0x50b360)
void miss2_0x11C::SCRCMD_STOP_PHONE_RING_50B360()
{
}

STUB_FUNC(0x50b3d0)
void miss2_0x11C::sub_50B3D0()
{
}

STUB_FUNC(0x50b440)
void miss2_0x11C::SCRCMD_CAR_DRIVE_AWAY_50B440()
{
}

STUB_FUNC(0x50b470)
void miss2_0x11C::SCRCMD_GET_CAR_FROM_CRANE_50B470()
{
}

STUB_FUNC(0x50b4f0)
void miss2_0x11C::sub_50B4F0()
{
}

STUB_FUNC(0x50b5a0)
void miss2_0x11C::sub_50B5A0()
{
}

STUB_FUNC(0x50b600)
void miss2_0x11C::sub_50B600()
{
}

STUB_FUNC(0x50b670)
void miss2_0x11C::SCRCMD_SET_GANG_INFO1_50B670()
{
}

STUB_FUNC(0x50b690)
void miss2_0x11C::sub_50B690()
{
}

STUB_FUNC(0x50b6f0)
void miss2_0x11C::SCRCMD_CHECK_SCORE_50B6F0()
{
}

STUB_FUNC(0x50b760)
void miss2_0x11C::SCRCMD_GET_SCORE_50B760()
{
}

STUB_FUNC(0x50b7d0)
void miss2_0x11C::SCRCMD_IS_CHAR_IN_GANG_50B7D0()
{
}

STUB_FUNC(0x50b8b0)
void miss2_0x11C::sub_50B8B0()
{
}

STUB_FUNC(0x50b910)
void miss2_0x11C::SCRCMD_IS_CHAR_FIRING_AREA_50B910()
{
}

STUB_FUNC(0x50b9c0)
void miss2_0x11C::SCRCMD_GET_PASSENGER_NUM_50B9C0()
{
}

STUB_FUNC(0x50ba30)
void miss2_0x11C::sub_50BA30()
{
}

STUB_FUNC(0x50ba70)
void miss2_0x11C::sub_50BA70()
{
}

STUB_FUNC(0x50bad0)
void miss2_0x11C::SCRCMD_CAR_WRECK_IN_LOCATION_50BAD0()
{
}

STUB_FUNC(0x50bb80)
void miss2_0x11C::sub_50BB80()
{
}

STUB_FUNC(0x50bbd0)
void miss2_0x11C::sub_50BBD0()
{
}

STUB_FUNC(0x50bc60)
void miss2_0x11C::SCRCMD_CHECK_NUM_ALIVE_50BC60()
{
}

STUB_FUNC(0x50bcd0)
void miss2_0x11C::SCRCMD_SET_MIN_ALIVE_50BCD0()
{
}

STUB_FUNC(0x50bd10)
void miss2_0x11C::sub_50BD10()
{
}

STUB_FUNC(0x50bdc0)
void miss2_0x11C::sub_50BDC0()
{
}

STUB_FUNC(0x50be00)
void miss2_0x11C::SCRCMD_HAS_CAR_WEAPON_50BE00()
{
}

STUB_FUNC(0x50be70)
void miss2_0x11C::SCRCMD_IS_CHAR_HORN_50BE70()
{
}

STUB_FUNC(0x50bed0)
void miss2_0x11C::SCRCMD_CHECK_MAX_PASS_50BED0()
{
}

STUB_FUNC(0x50bf40)
void miss2_0x11C::SCRCMD_IS_CHAR_IN_ZONE_50BF40()
{
}

STUB_FUNC(0x50c040)
void miss2_0x11C::SCRCMD_SET_PHONE_DEAD_50C040()
{
}

STUB_FUNC(0x50c0e0)
void miss2_0x11C::SCRCMD_IS_TRAILER_ATT_50C0E0()
{
}

STUB_FUNC(0x50c1b0)
void miss2_0x11C::SCRCMD_IS_CAR_ON_TRAIL_50C1B0()
{
}

STUB_FUNC(0x50c230)
void miss2_0x11C::SCRCMD_ENABLE_DISABLE_CRANE_50C230()
{
}

STUB_FUNC(0x50c2a0)
void miss2_0x11C::SCRCMD_CAR_GOT_DRIVER_50C2A0()
{
}

STUB_FUNC(0x50c2f0)
void miss2_0x11C::SCRCMD_SPOTTED_PLAYER_50C2F0()
{
}

STUB_FUNC(0x50c350)
void miss2_0x11C::SCRCMD_GET_LAST_PUNCHED_50C350()
{
}

STUB_FUNC(0x50c3b0)
void miss2_0x11C::SCRCMD_IS_CHAR_STUNNED_50C3B0()
{
}

STUB_FUNC(0x50c410)
void miss2_0x11C::SCRCMD_KILL_ALL_PASSENG_50C410()
{
}

STUB_FUNC(0x50c470)
void miss2_0x11C::SCRCMD_IS_GROUP_IN_CAR_50C470()
{
}

STUB_FUNC(0x50c4e0)
void miss2_0x11C::SCRCMD_PUNCHED_SOMEONE_50C4E0()
{
}

STUB_FUNC(0x50c540)
void miss2_0x11C::SCRCMD_ADD_CHAR_TO_GANG_50C540()
{
}

STUB_FUNC(0x50c5a0)
void miss2_0x11C::sub_50C5A0()
{
}

STUB_FUNC(0x50c6f0)
void miss2_0x11C::sub_50C6F0()
{
}

STUB_FUNC(0x50c760)
void miss2_0x11C::SCRCMD_BEEN_PUNCHED_BY_50C760()
{
}

STUB_FUNC(0x50c7d0)
void miss2_0x11C::SCRCMD_UPDATE_DOOR_50C7D0()
{
}

STUB_FUNC(0x50c8a0)
void miss2_0x11C::SCRCMD_DOOR_50C8A0()
{
}

STUB_FUNC(0x50c990)
void miss2_0x11C::SCRCMD_REMOVE_WEAPON_50C990()
{
}

STUB_FUNC(0x50c9f0)
void miss2_0x11C::SCRCMD_REMOVE_BLOCK_50C9F0()
{
}

STUB_FUNC(0x50ca30)
void miss2_0x11C::SCRCMD_LOWER_LEVEL_50CA30()
{
}

STUB_FUNC(0x50ca70)
void miss2_0x11C::sub_50CA70()
{
}

STUB_FUNC(0x50cab0)
void miss2_0x11C::SCRCMD_ADD_NEW_BLOCK_50CAB0()
{
}

STUB_FUNC(0x50cb20)
void miss2_0x11C::SCRCMD_ROAD_ON_OFF_50CB20(miss2_0x11C* a1)
{
}

STUB_FUNC(0x50cb70)
void miss2_0x11C::sub_50CB70()
{
}

STUB_FUNC(0x50ccb0)
void miss2_0x11C::sub_50CCB0()
{
}

STUB_FUNC(0x50cd30)
void miss2_0x11C::sub_50CD30()
{
}

STUB_FUNC(0x50cdb0)
void miss2_0x11C::SCRCMD_CAR_DAMAGE_POS_50CDB0()
{
}

STUB_FUNC(0x50ce10)
void miss2_0x11C::SCRCMD_PARK_FINISHED_50CE10()
{
}

STUB_FUNC(0x50ce50)
void miss2_0x11C::sub_50CE50(s32 a2, u16 a3)
{
}

STUB_FUNC(0x50ce90)
void miss2_0x11C::SCRCMD_PHONE_TEMPLATE_50CE90()
{
}

STUB_FUNC(0x50d200)
void miss2_0x11C::SCRCMD_REMOTE_CONTROL_50D200()
{
}

STUB_FUNC(0x50d2e0)
s32 miss2_0x11C::SCRCMD_LAUNCH_MISSION_50D2E0()
{
    return 0;
}

STUB_FUNC(0x50d340)
void miss2_0x11C::SCRCMD_SAVE_GAME_50D340()
{
}

STUB_FUNC(0x50d3c0)
void miss2_0x11C::sub_50D3C0()
{
}

STUB_FUNC(0x50d680)
void miss2_0x11C::SCRCMD_CHANGE_CAR_LOCK_50D680()
{
}

STUB_FUNC(0x50d870)
void miss2_0x11C::SCRCMD_CHANGE_INTENSITY_50D870()
{
}

STUB_FUNC(0x50d900)
void miss2_0x11C::SCRCMD_CHANGE_COLOUR_50D900()
{
}

STUB_FUNC(0x50d9a0)
void miss2_0x11C::SCRCMD_CHANGE_RADIUS_50D9A0()
{
}

STUB_FUNC(0x50da50)
void miss2_0x11C::sub_50DA50()
{
}

STUB_FUNC(0x50db70)
void miss2_0x11C::sub_50DB70()
{
}

STUB_FUNC(0x50dd00)
void miss2_0x11C::sub_50DD00()
{
}

STUB_FUNC(0x50dd90)
void miss2_0x11C::SCRCMD_SET_DIR_OF_TVVAN_50DD90()
{
}

STUB_FUNC(0x50de00)
void miss2_0x11C::SCRCMD_POINT_ONSCREEN_50DE00()
{
}

STUB_FUNC(0x50de50)
void miss2_0x11C::SCRCMD_CHAR_IN_AIR_50DE50()
{
}

STUB_FUNC(0x50deb0)
void miss2_0x11C::SCRCMD_CHAR_SUNK_50DEB0()
{
}

STUB_FUNC(0x50df10)
void miss2_0x11C::sub_50DF10()
{
}

STUB_FUNC(0x50e0b0)
void miss2_0x11C::sub_50E0B0(miss2_0x11C* a1)
{
}

STUB_FUNC(0x50e120)
void miss2_0x11C::sub_50E120()
{
}

STUB_FUNC(0x50e150)
void miss2_0x11C::sub_50E150()
{
}

STUB_FUNC(0x50e190)
void miss2_0x11C::sub_50E190()
{
}

STUB_FUNC(0x50e360)
void miss2_0x11C::SCRCMD_CHECK_CAR_SPEED_50E360()
{
}

STUB_FUNC(0x50e460)
void miss2_0x11C::sub_50E460()
{
}

STUB_FUNC(0x50e4a0)
void miss2_0x11C::SCRCMD_CHAR_ARRESTED_50E4A0()
{
}

STUB_FUNC(0x50e4f0)
void miss2_0x11C::sub_50E4F0()
{
}

STUB_FUNC(0x50e610)
void miss2_0x11C::sub_50E610()
{
}

STUB_FUNC(0x50e730)
void miss2_0x11C::sub_50E730()
{
}

STUB_FUNC(0x50e780)
void miss2_0x11C::sub_50E780()
{
}

STUB_FUNC(0x50e7f0)
void miss2_0x11C::sub_50E7F0()
{
}

STUB_FUNC(0x50e820)
void miss2_0x11C::sub_50E820()
{
}

STUB_FUNC(0x50e900)
void miss2_0x11C::sub_50E900()
{
}

STUB_FUNC(0x50e9a0)
void miss2_0x11C::sub_50E9A0()
{
}

STUB_FUNC(0x50e9e0)
void miss2_0x11C::sub_50E9E0()
{
}

STUB_FUNC(0x50ea40)
void miss2_0x11C::sub_50EA40()
{
}

STUB_FUNC(0x50eb00)
void miss2_0x11C::sub_50EB00()
{
}

STUB_FUNC(0x50ebd0)
void miss2_0x11C::sub_50EBD0()
{
}

STUB_FUNC(0x50ece0)
void miss2_0x11C::sub_50ECE0()
{
}

STUB_FUNC(0x50ed40)
void miss2_0x11C::sub_50ED40()
{
}

STUB_FUNC(0x50ed80)
void miss2_0x11C::sub_50ED80()
{
}

STUB_FUNC(0x50edc0)
void miss2_0x11C::sub_50EDC0()
{
}

STUB_FUNC(0x50f060)
void miss2_0x11C::SCRCMD_CHAR_INTO_CAR_50F060()
{
}

STUB_FUNC(0x50f150)
void miss2_0x11C::SCRCMD_DECIDE_POWERUP_50F150()
{
}

STUB_FUNC(0x50f220)
void miss2_0x11C::SCRCMD_SUPPRESS_MODEL_50F220()
{
}

STUB_FUNC(0x50f270)
s32 miss2_0x11C::sub_50F270()
{
    return 0;
}

STUB_FUNC(0x50f3d0)
void miss2_0x11C::SCRCMD_SET_GROUP_TYPE_50F3D0()
{
}

STUB_FUNC(0x50f410)
void miss2_0x11C::SCRCMD_CHAR_DO_NOTHING_50F410()
{
}

STUB_FUNC(0x50f450)
void miss2_0x11C::SCRCMD_EMERG_LIGHTS_50F450()
{
}

STUB_FUNC(0x50f4d0)
void miss2_0x11C::SCRCMD_CHECK_OBJ_MODEL_50F4D0()
{
}

STUB_FUNC(0x50f550)
void miss2_0x11C::SCRCMD_PED_GRAPHIC_50F550()
{
}

STUB_FUNC(0x50f5e0)
void miss2_0x11C::sub_50F5E0()
{
}

STUB_FUNC(0x50f770)
void miss2_0x11C::SCRCMD_MAKE_MUGGERS_50F770()
{
}

STUB_FUNC(0x50f7b0)
void miss2_0x11C::sub_50F7B0()
{
}

STUB_FUNC(0x50f900)
void miss2_0x11C::SCRCMD_STOP_CAR_DRIVE_50F900()
{
}

STUB_FUNC(0x50f940)
void miss2_0x11C::SCRCMD_IS_BUS_FULL_50F940()
{
}

STUB_FUNC(0x50f9b0)
void miss2_0x11C::SCRCMD_NO_CHARS_OFF_BUS_50F9B0()
{
}

STUB_FUNC(0x50fa00)
void miss2_0x11C::sub_50FA00()
{
}

STUB_FUNC(0x50fa40)
void miss2_0x11C::SCRCMD_SET_SHADING_LEV_50FA40()
{
}

STUB_FUNC(0x50fa70)
void miss2_0x11C::SCRCMD_SET_CAR_JAMMED_50FA70()
{
}

STUB_FUNC(0x50fad0)
void miss2_0x11C::SCRCMD_FINISH_MISSION_50FAD0()
{
}

STUB_FUNC(0x50faf0)
void miss2_0x11C::sub_50FAF0()
{
}

STUB_FUNC(0x50fb60)
void miss2_0x11C::sub_50FB60()
{
}

STUB_FUNC(0x50fc20)
void miss2_0x11C::sub_50FC20()
{
}

STUB_FUNC(0x50fc60)
void miss2_0x11C::sub_50FC60()
{
}

STUB_FUNC(0x50fe00)
void miss2_0x11C::sub_50FE00()
{
}

STUB_FUNC(0x50fed0)
void miss2_0x11C::sub_50FED0()
{
}

STUB_FUNC(0x50ff50)
void miss2_0x11C::sub_50FF50()
{
}

STUB_FUNC(0x50ffb0)
void miss2_0x11C::sub_50FFB0()
{
}

STUB_FUNC(0x510030)
void miss2_0x11C::sub_510030()
{
}

STUB_FUNC(0x510050)
void miss2_0x11C::sub_510050()
{
}

STUB_FUNC(0x510090)
void miss2_0x11C::sub_510090()
{
}

STUB_FUNC(0x510100)
void miss2_0x11C::sub_510100()
{
}

STUB_FUNC(0x510280)
void miss2_0x11C::sub_510280()
{
}

STUB_FUNC(0x510530)
void miss2_0x11C::sub_510530()
{
}

STUB_FUNC(0x510560)
void miss2_0x11C::sub_510560()
{
}

STUB_FUNC(0x5105b0)
void miss2_0x11C::sub_5105B0()
{
}

STUB_FUNC(0x510600)
void miss2_0x11C::sub_510600()
{
}

STUB_FUNC(0x510660)
void miss2_0x11C::sub_510660()
{
}

STUB_FUNC(0x510780)
void miss2_0x11C::sub_510780()
{
}

STUB_FUNC(0x5108d0)
void miss2_0x11C::PreExecOpCode_5108D0()
{
}

STUB_FUNC(0x511840)
char_type miss2_0x11C::sub_511840()
{
    return 0;
}

STUB_FUNC(0x511930)
s32 miss2_0x11C::sub_511930(char_type a2, u16 levelStart)
{
    return 0;
}

STUB_FUNC(0x511960)
miss2_0x11C* miss2_0x11C::sub_511960(s16 a2)
{
    return 0;
}

STUB_FUNC(0x5119a0)
s32 miss2_0x11C::launch_mission_5119A0(s32 a2, char_type* String1)
{
    return 0;
}

MATCH_FUNC(0x511a00)
miss2_0x11C::miss2_0x11C()
{
    this->field_0 = 0;
    this->field_114 =  new miss2_8();
    this->field_4_level_start = 0;
    this->field_6 = 0;
    this->field_8 = 0;
    this->field_C = 0;
    this->field_E = 0;
    this->field_10 = 0;
    this->field_12 = 0;
    this->field_11A = 0;
    
    memset(this->field_14_str, 0, sizeof(this->field_14_str));

    this->field_118 = 0;
}

STUB_FUNC(0x511cd0)
void miss2_0x11C::sub_511CD0()
{
}

STUB_FUNC(0x512fd0)
miss2_0x11C::~miss2_0x11C()
{
}
