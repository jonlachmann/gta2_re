#include "Function.hpp"
#include "zealous_borg.hpp"

MATCH_FUNC(0x484ED0)
zealous_borg::zealous_borg() // 0x484ED0
{
    sub_484EF0();
}

MATCH_FUNC(0x484EE0)
zealous_borg::~zealous_borg() // 0x484EE0
{
    // Empty
}

STUB_FUNC(0x484EF0)
void zealous_borg::sub_484EF0()
{
    /*
    BYTE *v1;         // edx
    zealous_borg *v2; // eax
    int v3;           // esi

    v1 = this->field_28;
    v2 = this;
    v3 = 10;
    do
    {
        v2->field_0[0] = 0;
        *v1 = 1;
        v2 = (zealous_borg *)((char *)v2 + 4);
        ++v1;
        --v3;
    } while (v3);
    this->field_34 = 0;
    */
}

MATCH_FUNC(0x484F20)
void zealous_borg::sub_484F20()
{
    sub_484F30();
}

STUB_FUNC(0x484F30)
void zealous_borg::sub_484F30()
{
    *(int *)&this->field_28[1] = 0x1010101;
    *(int *)&this->field_28[5] = 0x1010101;
    this->field_28[9] = 1;
}

STUB_FUNC(0x484F50)
void zealous_borg::sub_484F50(int a2)
{
    switch (a2)
    {
    case 1:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
        ++this->field_0[a2];
        break;
    case 2:
        if (this->field_28[a2])
        {
            ++this->field_0[a2];
            this->field_28[a2] = 0;
        }
        break;
    default:
        return;
    }
}

MATCH_FUNC(0x484FA0)
void zealous_borg::sub_484FA0(int a2)
{
    field_34 += a2;
}

MATCH_FUNC(0x484FB0)
void zealous_borg::sub_484FB0(int amount)
{
    field_38 += amount;
}
