#include "sound_obj.hpp"
#include "wizardly_margulis.hpp"

sound_obj gSound_obj_66F680;

void sound_obj::sub_41B700()
{
    unsigned __int8 count = this->field_10_samp_count2;
    for (unsigned int i = 0; i < count; count = this->field_10_samp_count2)
    {
        unsigned int v3 = i + 0x10 * this->field_98;
        ++i;
        *((BYTE *)&this->field_A4[31].field_60 + v3) = count;
    }
    *((BYTE *)&this->field_DBC + this->field_98) = 0;
}

void sound_obj::sub_41B7A0()
{
    // todo
}

// match
void sound_obj::sub_41A2A0()
{
    field_98 = 1;
    sub_41B700();
    field_98 = 0;

    sub_41B700();
    sub_41B7A0();

    field_1450 = 0;

    gWizardly_margulis_6FFF00.sub_58E2A0();
}

// match
void sound_obj::SetCDVol_41A270(unsigned char cdVol)
{
    field_25_cdVol = cdVol;
}

// match
char sound_obj::Set3DSound_41A2F0(char b3dSound)
{
    if (field_0 != 0)
    {
        if (b3dSound != field_1D_b3d_sound)
        {
            if (b3dSound)
            {
                if (gWizardly_margulis_6FFF00.sub_58D820(&field_1C_samp_count))
                {
                    field_1D_b3d_sound = 1;
                    field_10_samp_count2 = field_1C_samp_count; // note: regswap from redundant local
                    return 1;
                }
                else
                {
                    gWizardly_margulis_6FFF00.sub_58D9F0(1);
                    field_1D_b3d_sound = 0;
                    field_10_samp_count2 = 16;
                    return 0;
                }
            }

            if (gWizardly_margulis_6FFF00.sub_58D9F0(1))
            {
                field_1D_b3d_sound = 0;
                field_10_samp_count2 = 16;
                return 1;
            }

            gWizardly_margulis_6FFF00.sub_58D820(&field_1C_samp_count);
            field_1D_b3d_sound = 1;
            field_10_samp_count2 = field_1C_samp_count;
            return 0;
        }
        return 1;
    }
    return 0;
}