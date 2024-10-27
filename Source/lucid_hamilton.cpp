#include "Function.hpp"
#include "lucid_hamilton.hpp"

lucid_hamilton gLucid_hamilton_67E8E0;

// stub
void lucid_hamilton::sub_4C53D0()
{
    // todo
}

// nomatch
char* lucid_hamilton::SetMapName_4C5870(char_type *Source)
{
    return strncpy(field_0_map_name, Source, 255u);
}

// nomatch
char* lucid_hamilton::SetStyleName_4C5890(char_type *Source)
{
    return strncpy(field_100_style_name, Source, 0xFFu);
}

// nomatch
char* lucid_hamilton::SetScriptName_4C58B0(char_type *Source)
{
    return strncpy(field_200_script_name, Source, 0xFFu);
}

// nomatch
char_type * lucid_hamilton::DebugStr_4C58D0(char_type *Source)
{
    return strncpy(field_300_debug_str, Source, 0xFFu);
}

// nomatch
void lucid_hamilton::sub_4C58F0(char_type a2)
{
    field_400 = a2;
}

// nomatch
void lucid_hamilton::sub_4C5900(char_type a2)
{
    field_401 = a2;
}

// nomatch
void lucid_hamilton::sub_4C5910(char_type a2)
{
    field_402 = a2;
}

MATCH_FUNC(0x4C5920)
void lucid_hamilton::sub_4C5920(char_type a2)
{
    field_403 = a2;
}

// nomatch
void lucid_hamilton::sub_4C5930(char_type a2)
{
    this->field_404 = a2;
}

// nomatch
char* lucid_hamilton::GetMapName_4C5940()
{
    return field_0_map_name;
}

// nomatch
char* lucid_hamilton::GetStyleName_4C5950()
{
    return field_100_style_name;
}

// nomatch
char* lucid_hamilton::GetScriptName_4C5960()
{
    return field_200_script_name;
}

// nomatch
char* lucid_hamilton::GetDebugStr_4C5970()
{
    return this->field_300_debug_str;
}

// nomatch
char_type lucid_hamilton::sub_4C5980()
{
    return field_400;
}

// nomatch
char_type lucid_hamilton::sub_4C5990()
{
    return field_401;
}

// nomatch
char_type lucid_hamilton::sub_4C59A0()
{
    return this->field_402;
}

// nomatch
u8 lucid_hamilton::sub_4C59B0()
{
    return this->field_403;
}

// nomatch
char_type lucid_hamilton::sub_4C59C0()
{
    return this->field_404;
}

// nomatch
void lucid_hamilton::sub_4C59D0(u8 a2, s32 a3)
{
    this->field_408[a2] = a3;
}

// nomatch
s32 lucid_hamilton::sub_4C59F0(u8 idx)
{
    return this->field_408[idx];
}

// stub
void lucid_hamilton::sub_4C5A10(angry_lewin_0x85C *a1)
{
    // todo
    /*
    u8 k10Counter; // bl
    zealous_borg *p_field_644_unk; // esi
    u8 a2; // [esp+10h] [ebp-4h]

    k10Counter = 0;
    a2 = 0;
    p_field_644_unk = &a1->field_644_unk;
    do
    {
        lucid_hamilton::sub_4C59D0(this, a2, p_field_644_unk->field_0[0]);
        ++k10Counter;
        p_field_644_unk = (zealous_borg *)((char_type *)p_field_644_unk + 4);
        a2 = k10Counter;
    } while (k10Counter < 10u);
    lucid_hamilton::sub_4C5A70(this, a1->field_644_unk.field_34);
    lucid_hamilton::sub_4C5A90(this, a1->field_644_unk.field_38);
    */
}

// nomatch
void lucid_hamilton::sub_4C5A70(s32 a2)
{
    this->field_430 = a2;
}

// nomatch
s32 lucid_hamilton::sub_4C5A80()
{
    return this->field_430;
}

// nomatch
void lucid_hamilton::sub_4C5A90(s32 a2)
{
    this->field_434 = a2;
}

// nomatch
s32 lucid_hamilton::sub_4C5AA0()
{
    return this->field_434;
}

// nomatch
void lucid_hamilton::sub_4C5AB0(s16 a2)
{
    this->field_438 = a2;
}

// nomatch
s16 lucid_hamilton::sub_4C5AC0()
{
    return this->field_438;
}

// nomatch
void lucid_hamilton::sub_4C5AD0(char_type a2)
{
    this->field_43A = a2;
}

// nomatch
char_type lucid_hamilton::sub_4C5AE0()
{
    return this->field_43A;
}

// stub
void lucid_hamilton::init_4C5AF0()
{
    // todo
    /*
    blissful_ganguly_0x20 *pIter4; // ebx
    s32 *pIter3; // edi
    u16 *pIter2; // esi
    wonderful_knuth_0xC *pIter1; // ebp
    s32 k6Counter; // [esp+10h] [ebp-4h]

    this->field_43B = 0;
    this->field_43C = 0;
    this->field_440 = 0;
    this->field_441 = 0;
    this->field_442 = 6;
    pIter4 = this->field_4B4;
    pIter3 = this->field_49C;
    pIter2 = this->field_490;
    pIter1 = this->field_448;
    k6Counter = 6;
    do
    {
        *(DWORD *)pIter1->field_0 = 0;
        *(DWORD *)&pIter1->field_0[2] = 0;
        *(DWORD *)&pIter1->field_0[4] = 0;
        *pIter2 = 0;
        *pIter3 = 0;
        wcscpy(pIter4->field_0_str, word_67DC8C);
        ++pIter2;
        ++pIter3;
        ++pIter1;
        ++pIter4;
        --k6Counter;
    } while (k6Counter);
    */
}

// nomatch
void lucid_hamilton::sub_4C5B80(char_type a2, s32 a3, char_type a4, char_type a5, s32 a6)
{
    this->field_43B = a2;
    this->field_43C = a3;
    this->field_440 = a4;
    this->field_441 = a5;
    this->field_444 = a6;
}

// nomatch
char_type lucid_hamilton::sub_4C5BC0()
{
    return this->field_43B;
}

// nomatch
s32 lucid_hamilton::sub_4C5BD0()
{
    return this->field_43C;
}

// nomatch
char_type lucid_hamilton::sub_4C5BE0()
{
    return this->field_440;
}

// nomatch
char_type lucid_hamilton::sub_4C5BF0()
{
    return this->field_441;
}

// nomatch
void lucid_hamilton::sub_4C5C00(char_type a2)
{
    if (this->field_442 == 6)
        this->field_442 = a2;
}

// nomatch
char_type lucid_hamilton::sub_4C5C20()
{
    return this->field_442;
}

MATCH_FUNC(0x4C5C30)
void lucid_hamilton::sub_4C5C30(s32 a2, wchar_t *Source)
{
    wcsncpy(field_4B4[static_cast<u16>(a2)].field_0_str, Source, 16u);
}

// nomatch
blissful_ganguly_0x20 * lucid_hamilton::sub_4C5C60(u16 a2)
{
    return &this->field_4B4[a2];
}

// nomatch
void lucid_hamilton::sub_4C5C80(u8 a2, s32 a3)
{
    this->field_49C[a2] += a3;
}

// nomatch
s32 lucid_hamilton::sub_4C5CB0(u8 a2)
{
    return this->field_49C[a2];
}

// stub
void lucid_hamilton::sub_4C5CD0(u8 a2, u8 a3)
{
    // todo
    /*
    angry_lewin_0x85C *v4; // ecx
    s16 v5; // dx

    ++this->field_448[a2].field_0[a3];
    v4 = gGame_0x40_67E008->field_4[a2];
    if (a2 == a3)
    {
        v5 = this->field_490[a2];
        if (v5 > 0)
        {
            this->field_490[a2] = v5 - 1;
            eager_benz::sub_5935D0(&v4->field_2D4_unk, -1);
        }
    }
    else
    {
        ++this->field_490[a2];
        eager_benz::sub_5935D0(&v4->field_2D4_unk, 1);
    }*/
}

// nomatch
u16 lucid_hamilton::sub_4C5D60(u8 a2)
{
    return this->field_490[a2];
}

// nomatch
s16 lucid_hamilton::sub_4C5D80(u8 a2, u8 a3)
{
    return this->field_448[a2].field_0[a3];
}
