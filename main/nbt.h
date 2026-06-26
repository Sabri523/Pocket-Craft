#pragma once

#include <stdint.h>

enum NBTTagType_t {
    TAG_End        = 0,
    TAG_Byte       = 1,
    TAG_Short      = 2,
    TAG_Int        = 3,
    TAG_Long       = 4,
    TAG_Float      = 5,
    TAG_Double     = 6,
    TAG_Byte_Array = 7,
    TAG_String     = 8,
    TAG_List       = 9,
    TAG_Compound   = 10,
    TAG_Int_Array  = 11,
    TAG_Long_Array = 12
};

struct Tag {
    NBTTagType_t type;
    char* name;

    union {
        int8_t   byte_value;
        int16_t  short_value;
        int32_t  int_value;
        int64_t  long_value;

        float    float_value;
        double   double_value;

        char    *string_value;

        struct {
            struct Tag **items;
            size_t count;
        } list;

        struct {
            struct Tag **children;
            size_t count;
        } compound;
    };
    
};