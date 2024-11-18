#pragma once

#include <windows.h>
#include "Function.hpp"
#include "DrawUnk_0xBC.hpp"
#include "zealous_borg.hpp"
#include "sad_mirzakhani.hpp"

class infallible_turing;
class cool_nash_0x294;
class keen_bhaskara_0x30;
class angry_lewin_0x85C;

// todo: move
struct thirsty_lamarr
{
    EXPORT void sub_4921B0(s32 amount);

    s32 field_0;
    s32 field_4;
    char_type field_8;
    char_type field_9;
    char_type field_A;
    char_type field_B;
    s32 field_C;
    char_type field_10;
    char_type field_11;
    char_type field_12;
    char_type field_13[9];
    char_type field_1C;
    char_type field_1D_buf[9];
    char_type field_26;
    char_type field_27;
    char_type field_28;
    char_type field_29;
    s16 field_2A;
    char_type field_2C;
    char_type field_2D;
    s16 field_2E_idx;
    s32 field_30;
    s16 field_34;
    s16 field_36;
};

// todo: move
class eager_benz
{
public:
    // TODO: stub all
    void AddCash_592620(s32 cash);

    thirsty_lamarr field_0_unk;
    thirsty_lamarr field_38_unk;
    s32 field_70;
    char_type field_74;
    char_type field_75;
    char_type field_76;
    char_type field_77;
    s32 field_78;
    s16 field_7C;
    s16 field_7E;
    s32 field_80;
    s16 field_84;
    s16 field_86;
    s16 field_88;
    s16 field_8A;
    BYTE field_8C[256];
    s32 field_18C;
    s32 field_190;
    s32 field_194;
    char_type field_198;
    char_type field_199;
    char_type field_19A;
    char_type field_19B;
    s32 field_19C;
    s32 field_1A0;
    char_type field_1A4;
    char_type field_1A5;
    char_type field_1A6;
    char_type field_1A7;
    sad_mirzakhani field_1A8_unk;
    angry_lewin_0x85C *field_368_pObj;
};



// TODO: add these later
class Car_BC;
class Zone_144;

class angry_lewin_0x85C
{
public:
    EXPORT u8 GetIdx_4881E0();
    EXPORT void sub_5645B0(Car_BC* a2);
    EXPORT char_type sub_564610(Car_BC* a2, char_type a3);
    EXPORT u32* sub_564680(s32 a2);
    EXPORT void sub_564710(Car_BC* a2, s32 a3);
    EXPORT void sub_564790(s32 idx);
    EXPORT void sub_5647D0();
    EXPORT keen_bhaskara_0x30* sub_5648F0();
    EXPORT void sub_564910(keen_bhaskara_0x30* a2);
    EXPORT char_type sub_564940();
    EXPORT char_type sub_564960(s32 a2, u8 a3);
    EXPORT u16 sub_5649D0(char_type a2, char_type a3);
    EXPORT void sub_564AD0(Car_BC* a2);
    EXPORT s32 sub_564B60();
    EXPORT s32 sub_564B80();
    EXPORT u16 sub_564C00();
    EXPORT void sub_564C50(s32 a2);
    EXPORT void sub_564CC0();
    EXPORT void sub_564CF0();
    EXPORT char_type sub_564D60(s32 a2);
    EXPORT u16 sub_565070();
    EXPORT s32 sub_5651F0(s32 a2);
    EXPORT void sub_565310();
    EXPORT void sub_5653E0();
    EXPORT void sub_565460();
    EXPORT s32 sub_565490(cool_nash_0x294* pPed);
    EXPORT void SetInputs_565740(u32 input);
    EXPORT void sub_565770(u8 count);
    EXPORT void sub_565860();
    EXPORT void sub_565890(u16 action);
    EXPORT void sub_566380(u16 a2);
    EXPORT char_type sub_566520();
    EXPORT void sub_566820();
    EXPORT void sub_5668D0(cool_nash_0x294* a2);
    EXPORT char_type sub_566C30(Car_BC* a2);
    EXPORT char_type sub_566C80(u32* a2);
    EXPORT void sub_566EE0( char_type a2);
    EXPORT s32 sub_5670B0();
    EXPORT void sub_567130();
    EXPORT void sub_567850();
    EXPORT void sub_5679E0();
    EXPORT void sub_568520();
    EXPORT void sub_568630();
    EXPORT char_type sub_568670();
    EXPORT char_type sub_5686D0(DrawUnk_0xBC* a2);
    EXPORT void sub_568730();
    EXPORT void sub_5687F0();
    EXPORT s32 sub_569410();
    EXPORT void sub_569530();
    EXPORT void sub_5695A0();
    EXPORT char_type sub_569600(Car_BC* a2);
    EXPORT void sub_5696D0(s32 a2);
    EXPORT s32 sub_569840(u8* a2, u8* a3, u8* a4);
    EXPORT Car_BC* sub_5698E0();
    EXPORT s32 sub_569920(u32* a2, int* a3, int* a4);
    EXPORT void sub_5699F0(s32 a2);
    EXPORT void sub_569A10();
    EXPORT void sub_569C20();
    EXPORT void sub_569CB0();
    EXPORT void sub_569E70();
    EXPORT char* GetDeathText_569F00();
    EXPORT s32 sub_569F40();
    EXPORT s32 sub_569FF0();
    EXPORT void SetUnknown_56A000();
    EXPORT void sub_56A010();
    EXPORT void sub_56A020();
    EXPORT void sub_56A030();
    EXPORT void sub_56A040();
    EXPORT s32 sub_56A0F0();
    EXPORT zealous_borg* sub_56A1A0(s32 a2);
    EXPORT void sub_56A310(s32 a2);
    EXPORT void sub_56A490();
    EXPORT void sub_56A6D0();

    // 0x56A740
    EXPORT angry_lewin_0x85C(u8 a2);

    // 0x56A940
    EXPORT ~angry_lewin_0x85C();


    char_type field_0;
    char_type field_1;
    char_type field_2;
    char_type field_3;
    s32 field_4_inputs;
    s16 field_8;
    s16 field_A;
    s32 field_C;
    s32 field_10;
    s16 field_14;
    s16 field_16;
    s16 field_18;
    s16 field_1A;
    s32 field_1C;
    Car_BC* field_20;
    s32 field_24;
    char_type field_28;
    char_type field_29;
    char_type field_2A;
    char_type field_2B;
    s16 field_2C;
    u8 field_2E_idx;
    char_type field_2F;
    char_type field_30;
    char_type field_31;
    char_type field_32;
    char_type field_33;
    Zone_144* field_34_pObj;
    gmp_map_zone* field_38;
    gmp_map_zone* field_3C;
    gmp_map_zone* field_40;
    s32 field_44_death_type;
    char_type field_48;
    char_type field_49;
    char_type field_4A;
    char_type field_4B;
    infallible_turing* field_4C_pUnk;
    s32 field_50;
    Car_BC* field_54_unk[3];
    s32 field_60;
    char_type field_64;
    char_type field_65;
    char_type field_66;
    char_type field_67;
    s32 field_68;
    s32 field_6C;
    char_type field_70;
    char_type field_71;
    char_type field_72;
    char_type field_73;
    char_type field_74;
    char_type field_75;
    char_type field_76;
    char_type field_77;
    char_type field_78;
    char_type field_79;
    char_type field_7A;
    char_type field_7B;
    char_type field_7C;
    char_type field_7D;
    char_type field_7E;
    char_type field_7F;
    char_type field_80;
    char_type field_81;
    char_type field_82;
    char_type field_83;
    char_type field_84;
    char_type field_85;
    char_type field_86;
    char_type field_87;
    char_type field_88;
    char_type field_89;
    char_type field_8A;
    char_type field_8B;
    char_type field_8C;
    char_type field_8D;
    char_type field_8E_bInUse;
    char_type field_8F;
    DrawUnk_0xBC field_90_game_camera;
    DrawUnk_0xBC field_14C_view_camera;
    DrawUnk_0xBC field_208_aux_game_camera;
    cool_nash_0x294* field_2C4_player_ped;
    cool_nash_0x294* field_2C8_unkq;
    s32 field_2CC;
    char_type field_2D0;
    char_type field_2D1;
    char_type field_2D2;
    char_type field_2D3;
    eager_benz field_2D4_unk;
    char_type field_640;
    char_type field_641;
    char_type field_642;
    char_type field_643;
    zealous_borg field_644_unk;
    s16 field_680;
    s16 field_682;
    thirsty_lamarr field_684_unk;
    thirsty_lamarr field_6BC_unk;
    WORD field_6F4[17];
    s16 field_716;
    keen_bhaskara_0x30* field_718[28];
    s16 field_788_idx;
    char_type field_78A;
    char_type field_78B;
    s32 field_78C;
    s16 field_790;
    s16 field_792;
    char_type field_794;
    char_type field_795;
    s16 field_796;
    s32 field_798;
    s32 field_79C;
    s32 field_7A0;
    s32 field_7A4;
    s32 field_7A8;
    s32 field_7AC;
    s32 field_7B0;
    s32 field_7B4;
    s32 field_7B8;
    s32 field_7BC;
    s32 field_7C0;
    s32 field_7C4;
    s32 field_7C8;
    s32 field_7CC;
    s32 field_7D0;
    s32 field_7D4;
    s32 field_7D8;
    s32 field_7DC;
    s32 field_7E0;
    s32 field_7E4;
    s32 field_7E8;
    s32 field_7EC;
    s32 field_7F0;
    s32 field_7F4;
    s32 field_7F8;
    s32 field_7FC;
    s32 field_800;
    s32 field_804;
    s32 field_808;
    s32 field_80C;
    s32 field_810;
    s32 field_814;
    s32 field_818;
    s32 field_81C;
    s32 field_820;
    s32 field_824;
    s32 field_828;
    s32 field_82C;
    s32 field_830;
    s32 field_834;
    s32 field_838;
    wchar_t field_83C_player_name;
    char_type field_83E;
    char_type field_83F;
    s32 field_840;
    s32 field_844;
    s32 field_848;
    s32 field_84C;
    s32 field_850;
    s32 field_854;
    s32 field_858;
};
