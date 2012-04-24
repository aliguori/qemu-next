/*
 *  Atmel maXTouch touchscreen emulation
 *
 *  Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *    Igor Mitsyanko  <i.mitsyanko@samsung.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "i2c.h"
#include "console.h"

#ifndef MXT_DEBUG
#define MXT_DEBUG                         0
#endif

/* Fifo length must be a power of 2 */
#define MXT_MESSAGE_FIFO_LEN              16
#define MXT_MESSAGE_FIFO_MASK             (MXT_MESSAGE_FIFO_LEN - 1)
/* Maxtouch supports up to 10 concurrent touches, but we emulate 3 since common
 * PC mouse has only 3 buttons. Exact meaning of each touch (each mouse button
 * press) is defined by target userspace application only */
#define MXT_NUM_OF_TOUCHES                3
#define MXT_CRC_POLY                      0x80001B
#define MXT_CRC_SIZE                      3
/* Maximum value of x and y coordinate in QEMU mouse event callback */
#define MXT_QEMU_MAX_COORD                0x7FFF

/* Each maXTouch device consists of a certain number of subdevices (objects)
 * with code names like T5, T6, T9, e.t.c. Each object implements only a portion
 * of maXTouch functionality. For example, touch detection is performed
 * by T9 object, but information about touch state changes is generated (and can
 * be read) only in T5 object.
 * Various variants of maXTouch can have different set of objects placed at
 * different addresses within maXtouch address space. Composition of objects
 * is described by mandatory Object Table which starts at address 0x7.
 * Length of object table (i.e. number of objects) of this exact variant of
 * maXTouch can be read from address 0x6 */
#define MXT_OBJTBL_ENTRY_LEN              6
/* Offsets within one object table entry */
/* Object type code */
#define MXT_OBJTBL_TYPE                   0x0
/* Start address of object registers within maxTouch address space */
#define MXT_OBJTBL_START_LSB              0x1
#define MXT_OBJTBL_START_MSB              0x2
/* Number of object's registers (actually, this field contains size-1) */
#define MXT_OBJTBL_SIZE                   0x3
/* Number of instances of this object (contains instances-1, so value 0 means
 * one instance). All instances are placed continuously in memory */
#define MXT_OBJTBL_INSTANCES              0x4
/* Number of messages ID's object can generate in T5 object. For example,
 * T9 will generate messages with different ID's for each separate touch */
#define MXT_OBJTBL_REPORT_IDS             0x5

/* Object types */
enum {
    MXT_GEN_MESSAGE_T5 = 0,
    MXT_GEN_COMMAND_T6,
    MXT_GEN_POWER_T7,
    MXT_GEN_ACQUIRE_T8,
    MXT_TOUCH_MULTI_T9,
    MXT_TOUCH_KEYARRAY_T15,
    MXT_SPT_COMMSCONFIG_T18,
    MXT_SPT_GPIOPWM_T19,
    MXT_PROCI_GRIPFACE_T20,
    MXT_PROCG_NOISE_T22,
    MXT_TOUCH_PROXIMITY_T23,
    MXT_PROCI_ONETOUCH_T24,
    MXT_SPT_SELFTEST_T25,
    MXT_PROCI_TWOTOUCH_T27,
    MXT_SPT_CTECONFIG_T28,
    MXT_DEBUG_DIAGNOSTIC_T37,
    MXT_NUM_OF_OBJECT_TYPES
};

static const uint8_t mxt_obj_types_list[MXT_NUM_OF_OBJECT_TYPES] = {
    [MXT_GEN_MESSAGE_T5]       = 5,
    [MXT_GEN_COMMAND_T6]       = 6,
    [MXT_GEN_POWER_T7]         = 7,
    [MXT_GEN_ACQUIRE_T8]       = 8,
    [MXT_TOUCH_MULTI_T9]       = 9,
    [MXT_TOUCH_KEYARRAY_T15]   = 15,
    [MXT_SPT_COMMSCONFIG_T18]  = 18,
    [MXT_SPT_GPIOPWM_T19]      = 19,
    [MXT_PROCI_GRIPFACE_T20]   = 20,
    [MXT_PROCG_NOISE_T22]      = 22,
    [MXT_TOUCH_PROXIMITY_T23]  = 23,
    [MXT_PROCI_ONETOUCH_T24]   = 24,
    [MXT_SPT_SELFTEST_T25]     = 25,
    [MXT_PROCI_TWOTOUCH_T27]   = 27,
    [MXT_SPT_CTECONFIG_T28]    = 28,
    [MXT_DEBUG_DIAGNOSTIC_T37] = 37
};

static const uint8_t mxt_obj_sizes[MXT_NUM_OF_OBJECT_TYPES] = {
    [MXT_GEN_MESSAGE_T5]       = 10,
    [MXT_GEN_COMMAND_T6]       = 6,
    [MXT_GEN_POWER_T7]         = 3,
    [MXT_GEN_ACQUIRE_T8]       = 8,
    [MXT_TOUCH_MULTI_T9]       = 31,
    [MXT_TOUCH_KEYARRAY_T15]   = 11,
    [MXT_SPT_COMMSCONFIG_T18]  = 2,
    [MXT_SPT_GPIOPWM_T19]      = 16,
    [MXT_PROCI_GRIPFACE_T20]   = 12,
    [MXT_PROCG_NOISE_T22]      = 17,
    [MXT_TOUCH_PROXIMITY_T23]  = 15,
    [MXT_PROCI_ONETOUCH_T24]   = 19,
    [MXT_SPT_SELFTEST_T25]     = 14,
    [MXT_PROCI_TWOTOUCH_T27]   = 7,
    [MXT_SPT_CTECONFIG_T28]    = 6,
    [MXT_DEBUG_DIAGNOSTIC_T37] = 128
};

#define MXT_INFO_START                0x00
#define MXT_INFO_SIZE                 7
#define MXT_INFO_END                  (MXT_INFO_START + MXT_INFO_SIZE - 1)
#define MXT_OBJTBL_START              (MXT_INFO_START + MXT_INFO_SIZE)
#define MXT_OBJTBL_END(s)             \
    (MXT_OBJTBL_START + (s->mxt_info[MXT_OBJ_NUM]) * MXT_OBJTBL_ENTRY_LEN - 1)
#define MXT_CRC_START(s)              \
    (MXT_OBJTBL_START + (s->mxt_info[MXT_OBJ_NUM]) * MXT_OBJTBL_ENTRY_LEN)
#define MXT_CRC_END(s)                \
    (MXT_OBJTBL_START + (s->mxt_info[MXT_OBJ_NUM]) * MXT_OBJTBL_ENTRY_LEN + \
    MXT_CRC_SIZE - 1)
#define MXT_OBJECTS_START(s)          (s->obj_tbl[0].start_addr)

/* MXT info Registers */
#define MXT_FAMILY_ID                 0x00
#define MXT_VARIANT_ID                0x01
#define MXT_VERSION                   0x02
#define MXT_BUILD                     0x03
#define MXT_MATRIX_X_SIZE             0x04
#define MXT_MATRIX_Y_SIZE             0x05
#define MXT_OBJ_NUM                   0x06

/* Registers within Multitouch T9 object */
#define MXT_OBJ_T9_CTRL               0
#define MXT_OBJ_T9_XORIGIN            1
#define MXT_OBJ_T9_YORIGIN            2
#define MXT_OBJ_T9_XSIZE              3
#define MXT_OBJ_T9_YSIZE              4
#define MXT_OBJ_T9_BLEN               6
#define MXT_OBJ_T9_TCHTHR             7
#define MXT_OBJ_T9_TCHDI              8
#define MXT_OBJ_T9_ORIENT             9
#define MXT_OBJ_T9_MOVHYSTI           11
#define MXT_OBJ_T9_MOVHYSTN           12
#define MXT_OBJ_T9_NUMTOUCH           14
#define MXT_OBJ_T9_MRGHYST            15
#define MXT_OBJ_T9_MRGTHR             16
#define MXT_OBJ_T9_AMPHYST            17
#define MXT_OBJ_T9_XRANGE_LSB         18
#define MXT_OBJ_T9_XRANGE_MSB         19
#define MXT_OBJ_T9_YRANGE_LSB         20
#define MXT_OBJ_T9_YRANGE_MSB         21
#define MXT_OBJ_T9_XLOCLIP            22
#define MXT_OBJ_T9_XHICLIP            23
#define MXT_OBJ_T9_YLOCLIP            24
#define MXT_OBJ_T9_YHICLIP            25
#define MXT_OBJ_T9_XEDGECTRL          26
#define MXT_OBJ_T9_XEDGEDIST          27
#define MXT_OBJ_T9_YEDGECTRL          28
#define MXT_OBJ_T9_YEDGEDIST          29
#define MXT_OBJ_T9_JUMPLIMIT          30

/* Multitouch T9 messages status flags */
#define MXT_T9_STAT_MOVE              (1 << 4)
#define MXT_T9_STAT_RELEASE           (1 << 5)
#define MXT_T9_STAT_PRESS             (1 << 6)
#define MXT_T9_STAT_DETECT            (1 << 7)

/* Multitouch T9 orient bits */
#define MXT_T9_XY_SWITCH              (1 << 0)

/* Fields of T5 message */
#define MXT_OBJ_T5_REPORTID           0
#define MXT_OBJ_T5_STATUS             1
#define MXT_OBJ_T5_XPOSMSH            2
#define MXT_OBJ_T5_YPOSMSH            3
#define MXT_OBJ_T5_XYPOSLSH           4
#define MXT_OBJ_T5_AREA               5
#define MXT_OBJ_T5_PRESSURE           6
#define MXT_OBJ_T5_UNKNOWN            7
#define MXT_OBJ_T5_CHECKSUM           8

#if MXT_MESSAGE_FIFO_LEN & MXT_MESSAGE_FIFO_MASK
#error Message fifo length must be a power of 2
#endif

/* An entry of object description table */
typedef struct MXTObjTblEntry {
    uint8_t type;
    uint16_t start_addr;
    uint8_t size;
    uint8_t instances;
    uint8_t num_report_ids;
} MXTObjTblEntry;

typedef struct ObjConfig {
    uint8_t type;
    uint8_t instances;
} ObjConfig;

typedef struct MXTVariantInfo {
    const char *name;
    const uint8_t *mxt_variant_info;
    const ObjConfig *mxt_variant_obj_list;
} MXTVariantInfo;

#define TYPE_MAXTOUCH           "maxtouch"
#define MXT(obj)                \
    OBJECT_CHECK(MXTState, (obj), TYPE_MAXTOUCH)
#define MXT_CLASS(klass)        \
    OBJECT_CLASS_CHECK(MXTClass, (klass), TYPE_MAXTOUCH)
#define MXT_GET_CLASS(obj)      \
    OBJECT_GET_CLASS(MXTClass, (obj), TYPE_MAXTOUCH)

/* Definitions for variant #0 of Atmel maXTouch */
#define TYPE_MXT_VARIANT0       "maxtouch.var0"

static const ObjConfig mxt_variant0_objlist[] = {
    { .type = MXT_GEN_MESSAGE_T5,      .instances = 0 },
    { .type = MXT_GEN_COMMAND_T6,      .instances = 0 },
    { .type = MXT_GEN_POWER_T7,        .instances = 0 },
    { .type = MXT_GEN_ACQUIRE_T8,      .instances = 0 },
    { .type = MXT_TOUCH_MULTI_T9,      .instances = 0 },
    { .type = MXT_TOUCH_KEYARRAY_T15,  .instances = 0 },
    { .type = MXT_SPT_GPIOPWM_T19,     .instances = 0 },
    { .type = MXT_PROCI_GRIPFACE_T20,  .instances = 0 },
    { .type = MXT_PROCG_NOISE_T22,     .instances = 0 },
    { .type = MXT_TOUCH_PROXIMITY_T23, .instances = 0 },
    { .type = MXT_PROCI_ONETOUCH_T24,  .instances = 0 },
    { .type = MXT_SPT_SELFTEST_T25,    .instances = 0 },
    { .type = MXT_PROCI_TWOTOUCH_T27,  .instances = 0 },
    { .type = MXT_SPT_CTECONFIG_T28,   .instances = 0 },
};

static const uint8_t mxt_variant0_info[MXT_INFO_SIZE] = {
    [MXT_FAMILY_ID] = 0x80,
    [MXT_VARIANT_ID] = 0x0,
    [MXT_VERSION] = 0x1,
    [MXT_BUILD] = 0x1,
    [MXT_MATRIX_X_SIZE] = 16,
    [MXT_MATRIX_Y_SIZE] = 14,
    [MXT_OBJ_NUM] = ARRAY_SIZE(mxt_variant0_objlist),
};

/* Definitions for variant #1 of Atmel maXTouch */
#define TYPE_MXT_VARIANT1       "maxtouch.var1"

static const ObjConfig mxt_variant1_objlist[] = {
    { .type = MXT_GEN_MESSAGE_T5,      .instances = 0 },
    { .type = MXT_GEN_COMMAND_T6,      .instances = 0 },
    { .type = MXT_GEN_POWER_T7,        .instances = 0 },
    { .type = MXT_GEN_ACQUIRE_T8,      .instances = 0 },
    { .type = MXT_TOUCH_MULTI_T9,      .instances = 0 },
    { .type = MXT_SPT_COMMSCONFIG_T18, .instances = 0 },
    { .type = MXT_PROCI_GRIPFACE_T20,  .instances = 0 },
    { .type = MXT_PROCG_NOISE_T22,     .instances = 0 },
    { .type = MXT_SPT_CTECONFIG_T28,   .instances = 0 },
    { .type = MXT_DEBUG_DIAGNOSTIC_T37, .instances = 0 },
};

static const uint8_t mxt_variant1_info[MXT_INFO_SIZE] = {
    [MXT_FAMILY_ID] = 0x80,
    [MXT_VARIANT_ID] = 0x1,
    [MXT_VERSION] = 0x1,
    [MXT_BUILD] = 0x1,
    [MXT_MATRIX_X_SIZE] = 16,
    [MXT_MATRIX_Y_SIZE] = 14,
    [MXT_OBJ_NUM] = ARRAY_SIZE(mxt_variant1_objlist),
};

static const MXTVariantInfo mxt_variants_info_array[] = {
    {
        .name = TYPE_MXT_VARIANT0,
        .mxt_variant_info = mxt_variant0_info,
        .mxt_variant_obj_list = mxt_variant0_objlist
    },
    {
        .name = TYPE_MXT_VARIANT1,
        .mxt_variant_info = mxt_variant1_info,
        .mxt_variant_obj_list = mxt_variant1_objlist
    }
};

#define MXT_NUM_OF_VARIANTS     ARRAY_SIZE(mxt_variants_info_array)

/* Generate Message T5 message format */
typedef struct MXTMessage {
    uint8_t reportid;
    uint8_t status;
    uint8_t xpos_msh;
    uint8_t ypos_msh;
    uint8_t xypos_lsh;
    uint8_t area;
    uint8_t pressure;
    uint8_t checksum;
} MXTMessage;

/* Possible MXT i2c-related states */
enum {
    IDLE = 0,
    STARTING_WRITE,
    SELECTING_REGISTER,
    WRITING_DATA,
    READING_DATA,
};

typedef struct MXTClass {
    I2CSlaveClass parent_class;

    const uint8_t *mxt_info;
    MXTObjTblEntry *obj_tbl;
    uint8_t crc[MXT_CRC_SIZE];
    uint16_t end_addr;
    /* This is used to speed things up a little */
    uint16_t t5_address;
    uint16_t t9_address;
} MXTClass;

typedef struct MXTState {
    I2CSlave i2c;
    QEMUPutMouseEntry *mouse;
    qemu_irq nCHG;     /* line state changes to low level to signal new event */
    uint8_t *objects;
    uint32_t obj_len;
    uint16_t selected_reg;
    uint8_t i2c_state;

    uint8_t fifo_get;
    uint8_t fifo_add;
    bool fifo_lock;
    MXTMessage msg_fifo[MXT_MESSAGE_FIFO_LEN];

    double scale_x;
    double scale_y;
    uint16_t x_threshold;
    uint16_t y_threshold;
    uint16_t x_curr;
    uint16_t y_curr;
    uint8_t touches;
} MXTState;

static const VMStateDescription mxt_message_vmstate = {
    .name = "mxt-message",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(reportid, MXTMessage),
        VMSTATE_UINT8(status, MXTMessage),
        VMSTATE_UINT8(xpos_msh, MXTMessage),
        VMSTATE_UINT8(ypos_msh, MXTMessage),
        VMSTATE_UINT8(xypos_lsh, MXTMessage),
        VMSTATE_UINT8(area, MXTMessage),
        VMSTATE_UINT8(pressure, MXTMessage),
        VMSTATE_UINT8(checksum, MXTMessage),
        VMSTATE_END_OF_LIST()
    }
};


#if MXT_DEBUG
#define DPRINT(fmt, args...)           \
    do { fprintf(stderr, "QEMU MXT: "fmt, ## args); } while (0)
#define DPRINT_SMPL(fmt, args...)      \
    do { fprintf(stderr, fmt, ## args); } while (0)
#define ERRPRINT(fmt, args...)         \
    do { fprintf(stderr, "QEMU MXT ERROR: "fmt, ## args); } while (0)

static char dbg_reg_buf[50];

static const char *dbg_multitoucht9_regs[] = {
    [MXT_OBJ_T9_CTRL]       = "CTRL",
    [MXT_OBJ_T9_XORIGIN]    = "XORIGIN",
    [MXT_OBJ_T9_YORIGIN]    = "YORIGIN",
    [MXT_OBJ_T9_XSIZE]      = "XSIZE",
    [MXT_OBJ_T9_YSIZE]      = "YSIZE",
    [5]                     = "REG_5",
    [MXT_OBJ_T9_BLEN]       = "BLEN",
    [MXT_OBJ_T9_TCHTHR]     = "TCHTHR",
    [MXT_OBJ_T9_TCHDI]      = "TCHDI",
    [MXT_OBJ_T9_ORIENT]     = "ORIENT",
    [10]                    = "REG_10",
    [MXT_OBJ_T9_MOVHYSTI]   = "MOVHYSTI",
    [MXT_OBJ_T9_MOVHYSTN]   = "MOVHYSTN",
    [13]                    = "REG_13",
    [MXT_OBJ_T9_NUMTOUCH]   = "NUMTOUCH",
    [MXT_OBJ_T9_MRGHYST]    = "MRGHYST",
    [MXT_OBJ_T9_MRGTHR]     = "MRGTHR",
    [MXT_OBJ_T9_AMPHYST]    = "AMPHYST",
    [MXT_OBJ_T9_XRANGE_LSB] = "XRANGE_L",
    [MXT_OBJ_T9_XRANGE_MSB] = "XRANGE_H",
    [MXT_OBJ_T9_YRANGE_LSB] = "YRANGE_L",
    [MXT_OBJ_T9_YRANGE_MSB] = "YRANGE_H",
    [MXT_OBJ_T9_XLOCLIP]    = "XLOCLIP",
    [MXT_OBJ_T9_XHICLIP]    = "XHICLIP",
    [MXT_OBJ_T9_YLOCLIP]    = "YLOCLIP",
    [MXT_OBJ_T9_YHICLIP]    = "YHICLIP",
    [MXT_OBJ_T9_XEDGECTRL]  = "XEDGECTRL",
    [MXT_OBJ_T9_XEDGEDIST]  = "XEDGEDIST",
    [MXT_OBJ_T9_YEDGECTRL]  = "YEDGECTRL",
    [MXT_OBJ_T9_YEDGEDIST]  = "YEDGEDIST",
    [MXT_OBJ_T9_JUMPLIMIT]  = "JUMPLIMIT",
};

static const char *dbg_gen_message_t5_regs[] = {
    [MXT_OBJ_T5_REPORTID] = "REPORTID",
    [MXT_OBJ_T5_STATUS]   = "STATUS",
    [MXT_OBJ_T5_XPOSMSH]  = "XPOSMSH",
    [MXT_OBJ_T5_YPOSMSH]  = "YPOSMSH",
    [MXT_OBJ_T5_XYPOSLSH] = "XYPOSLSH",
    [MXT_OBJ_T5_AREA]     = "AREA",
    [MXT_OBJ_T5_PRESSURE] = "PRESSURE",
    [MXT_OBJ_T5_UNKNOWN]  = "REG_7",
    [MXT_OBJ_T5_CHECKSUM] = "CHECKSUM",
    [9]                   = "REG_9"
};

static const char *dbg_mxt_info_regs[] = {
    [MXT_FAMILY_ID]     = "FAMILY_ID",
    [MXT_VARIANT_ID]    = "VARIANT_ID",
    [MXT_VERSION]       = "VERSION",
    [MXT_BUILD]         = "BUILD",
    [MXT_MATRIX_X_SIZE] = "MATRIX_X_SIZE",
    [MXT_MATRIX_Y_SIZE] = "MATRIX_Y_SIZE",
    [MXT_OBJ_NUM]       = "OBJ_NUM",
};

static const char *dbg_mxt_obj_name(unsigned type)
{
    switch (type) {
    case 5:
        return "GEN_MESSAGE_T5";
    case 6:
        return "GEN_COMMAND_T6";
    case 7:
        return "GEN_POWER_T7";
    case 8:
        return "GEN_ACQUIRE_T8";
    case 9:
        return "TOUCH_MULTI_T9";
    case 15:
        return "TOUCH_KEYARRAY_T15";
    case 18:
        return "SPT_COMMSCONFIG_T18";
    case 19:
        return "SPT_GPIOPWM_T19";
    case 20:
        return "PROCI_GRIPFACE_T20";
    case 22:
        return "PROCG_NOISE_T22";
    case 23:
        return "TOUCH_PROXIMITY_T23";
    case 24:
        return "PROCI_ONETOUCH_T24";
    case 25:
        return "SPT_SELFTEST_T25";
    case 27:
        return "PROCI_TWOTOUCH_T27";
    case 28:
        return "SPT_CTECONFIG_T28";
    default:
        return "UNKNOWN";
    }
}

static char *mxt_get_reg_name(MXTState *s, unsigned int offset)
{
    MXTClass *k = MXT_GET_CLASS(s);
    unsigned i;

    if ((offset >= k->t5_address) &&
       (offset < (k->t5_address + mxt_obj_sizes[MXT_GEN_MESSAGE_T5]))) {
        i = (offset - k->t5_address);
        snprintf(dbg_reg_buf, sizeof(dbg_reg_buf), "MESSAGE_T5[%s]",
                dbg_gen_message_t5_regs[i]);
    } else if ((offset >= k->t9_address) && (offset < k->t9_address +
            mxt_obj_sizes[MXT_TOUCH_MULTI_T9])) {
        i = (offset - k->t9_address);
        snprintf(dbg_reg_buf, sizeof(dbg_reg_buf), "MULTITOUCH_T9[%s]",
                dbg_multitoucht9_regs[i]);
    } else if (offset <= MXT_INFO_END) {
        i = (offset - MXT_INFO_START);
        snprintf(dbg_reg_buf, sizeof(dbg_reg_buf), "INFO[%s]",
                dbg_mxt_info_regs[i]);
    } else if (offset <= MXT_OBJTBL_END(k)) {
        i = (offset - MXT_OBJTBL_START) / MXT_OBJTBL_ENTRY_LEN;
        switch ((offset - MXT_OBJTBL_START) % MXT_OBJTBL_ENTRY_LEN) {
        case MXT_OBJTBL_TYPE:
            snprintf(dbg_reg_buf, sizeof(dbg_reg_buf), "OBJTBL_%u TYPE", i);
            break;
        case MXT_OBJTBL_START_LSB:
            snprintf(dbg_reg_buf, sizeof(dbg_reg_buf), "OBJTBL_%u ADDR_LSB", i);
            break;
        case MXT_OBJTBL_START_MSB:
            snprintf(dbg_reg_buf, sizeof(dbg_reg_buf), "OBJTBL_%u ADDR_MSB", i);
            break;
        case MXT_OBJTBL_SIZE:
            snprintf(dbg_reg_buf, sizeof(dbg_reg_buf), "OBJTBL_%u SIZE", i);
            break;
        case MXT_OBJTBL_INSTANCES:
            snprintf(dbg_reg_buf, sizeof(dbg_reg_buf),
                    "OBJTBL_%u INSTANCES", i);
            break;
        case MXT_OBJTBL_REPORT_IDS:
            snprintf(dbg_reg_buf, sizeof(dbg_reg_buf),
                    "OBJTBL_%u REPORT_IDS", i);
            break;
        }
    } else if (offset <= MXT_CRC_END(k)) {
        i = offset - MXT_CRC_START(k);
        snprintf(dbg_reg_buf, sizeof(dbg_reg_buf), "CRC[%i]", i);
    } else if (offset <= k->end_addr) {
        for (i = 0; i < k->mxt_info[MXT_OBJ_NUM]; i++) {
            if (offset >= (k->obj_tbl[i].start_addr + k->obj_tbl[i].size + 1)) {
                continue;
            }
            snprintf(dbg_reg_buf, sizeof(dbg_reg_buf), "%s[%u]",
                    dbg_mxt_obj_name(k->obj_tbl[i].type),
                    offset - k->obj_tbl[i].start_addr);
            break;
        }
    } else {
        snprintf(dbg_reg_buf, sizeof(dbg_reg_buf), "UNKNOWN");
    }
    return dbg_reg_buf;
}

#else
#define DPRINT(fmt, args...)            do { } while (0)
#define DPRINT_SMPL(fmt, args...)       do { } while (0)
#define ERRPRINT(fmt, args...)          \
    do { fprintf(stderr, "QEMU MXT ERROR: "fmt, ## args); } while (0)
#endif

static void
mxt_mouse_event(void *opaque, int x, int y, int z, int buttons_state)
{
    MXTState *s = (MXTState *)opaque;
    uint16_t x_new, y_new;
    unsigned i;
    bool state_changed = false;

    /* Check that message buffer is not full */
    if (s->fifo_lock ||
            ((s->fifo_add + 1) & MXT_MESSAGE_FIFO_MASK) == s->fifo_get) {
        return;
    }

    x_new = (uint16_t)((double)x * s->scale_x);
    y_new = (uint16_t)((double)y * s->scale_y);

    for (i = 1; i <= MXT_NUM_OF_TOUCHES; i++) {
        if ((s->touches & (1 << (i - 1))) != (buttons_state & (1 << (i - 1)))) {
            if (buttons_state & (1 << (i - 1))) {
                /* Generate press event message */
                s->msg_fifo[s->fifo_add].status =
                        MXT_T9_STAT_DETECT | MXT_T9_STAT_PRESS;
                s->msg_fifo[s->fifo_add].area = 0x20;
                s->msg_fifo[s->fifo_add].pressure = 0x10;
            } else {
                /* Generate release event message */
                s->msg_fifo[s->fifo_add].status = MXT_T9_STAT_RELEASE;
                s->msg_fifo[s->fifo_add].area = 0x0;
                s->msg_fifo[s->fifo_add].pressure = 0x0;
            }
        } else if ((s->touches & (1 << (i - 1))) &&
                (ABS(x_new - s->x_curr) >= s->x_threshold ||
                 ABS(y_new - s->y_curr) >= s->y_threshold)) {
            s->msg_fifo[s->fifo_add].status =
                    MXT_T9_STAT_DETECT | MXT_T9_STAT_MOVE;
            s->msg_fifo[s->fifo_add].area = 0x20;
            s->msg_fifo[s->fifo_add].pressure = 0x10;
        } else {
            continue;
        }

        s->msg_fifo[s->fifo_add].xpos_msh = x_new >> 4;
        s->msg_fifo[s->fifo_add].ypos_msh = y_new >> 4;
        s->msg_fifo[s->fifo_add].xypos_lsh =
                (y_new & 0xF) | ((x_new << 4) & 0xF0);
        s->msg_fifo[s->fifo_add].reportid = i;
        s->msg_fifo[s->fifo_add].checksum = 0;
        s->fifo_add = (s->fifo_add + 1) & MXT_MESSAGE_FIFO_MASK;
        state_changed = true;
    }

    if (state_changed) {
        s->touches = buttons_state;
        s->x_curr = x_new;
        s->y_curr = y_new;
        /* CHG line changes to low and new message is generated in
         * gen_message_t5 subsystem when touch event occurs. CHG line
         * changes back to high only after all messages have been read from
         * gen_message_t5 subsystem */
        if (s->fifo_add == ((s->fifo_get + 1) & MXT_MESSAGE_FIFO_MASK)) {
            qemu_irq_lower(s->nCHG);
        }

    }
}

/* Read field of current message in message FIFO */
static uint8_t mxt_read_message_field(MXTState *s, unsigned field)
{
    uint8_t ret;

    /* If there are no messages, return dummy message with REPORTID=0xFF */
    if (s->fifo_get == s->fifo_add) {
        s->fifo_lock = true;
        if (field == MXT_OBJ_T5_CHECKSUM) {
            qemu_irq_raise(s->nCHG);
            s->fifo_lock = false;
        }
        return 0xFF;
    }

    switch (field) {
    case MXT_OBJ_T5_REPORTID:
        return s->msg_fifo[s->fifo_get].reportid;
    case MXT_OBJ_T5_STATUS:
        return s->msg_fifo[s->fifo_get].status;
    case MXT_OBJ_T5_XPOSMSH:
        return s->msg_fifo[s->fifo_get].xpos_msh;
    case MXT_OBJ_T5_YPOSMSH:
        return s->msg_fifo[s->fifo_get].ypos_msh;
    case MXT_OBJ_T5_XYPOSLSH:
        return s->msg_fifo[s->fifo_get].xypos_lsh;
    case MXT_OBJ_T5_AREA:
        return s->msg_fifo[s->fifo_get].area;
    case MXT_OBJ_T5_PRESSURE:
        return s->msg_fifo[s->fifo_get].pressure;
    case MXT_OBJ_T5_UNKNOWN:
        return 0;
    case MXT_OBJ_T5_CHECKSUM:
        ret = s->msg_fifo[s->fifo_get].checksum;
        s->fifo_get = (s->fifo_get + 1) & MXT_MESSAGE_FIFO_MASK;
        return ret;
    }
    return 0;
}

static int mxt_read_info_reg(MXTClass *k, unsigned int offset)
{
    unsigned i;

    if (offset <= MXT_INFO_END) {
        return k->mxt_info[offset - MXT_INFO_START];
    } else if (offset <= MXT_OBJTBL_END(k)) {
        i = (offset - MXT_OBJTBL_START) / MXT_OBJTBL_ENTRY_LEN;
        switch ((offset - MXT_OBJTBL_START) % MXT_OBJTBL_ENTRY_LEN) {
        case MXT_OBJTBL_TYPE:
            return k->obj_tbl[i].type;
        case MXT_OBJTBL_START_LSB:
            return (uint8_t)k->obj_tbl[i].start_addr;
        case MXT_OBJTBL_START_MSB:
            return (uint8_t)(k->obj_tbl[i].start_addr >> 8);
        case MXT_OBJTBL_SIZE:
            return k->obj_tbl[i].size;
        case MXT_OBJTBL_INSTANCES:
            return k->obj_tbl[i].instances;
        case MXT_OBJTBL_REPORT_IDS:
            return k->obj_tbl[i].num_report_ids;
        }
    } else if (offset <= MXT_CRC_END(k)) {
        return k->crc[offset - MXT_CRC_START(k)];
    }

    return -1;
}

static inline void mxt_calc_x_scalecoef(MXTState *s)
{
    MXTClass *k = MXT_GET_CLASS(s);
    uint16_t div, tmp;
    uint8_t *t9 = &s->objects[k->t9_address - MXT_OBJECTS_START(k)];
    uint16_t x_max = t9[MXT_OBJ_T9_XRANGE_LSB] |
            (t9[MXT_OBJ_T9_XRANGE_MSB] << 8);
    uint16_t threshold = t9[MXT_OBJ_T9_XSIZE];

    if (x_max == 0) {
        if (t9[MXT_OBJ_T9_ORIENT] & MXT_T9_XY_SWITCH) {
            s->scale_y = 0;
        } else {
            s->scale_x = 0;
        }
        return;
    }

    div = MXT_QEMU_MAX_COORD / x_max + 1;
    tmp = x_max * div;
    /* Divide by 4 if XRANGE less then 1024 */
    if ((t9[MXT_OBJ_T9_YRANGE_MSB] & 0xC) == 0) {
        div >>= 2;
        div++;
        tmp = x_max * (div << 2);
        threshold <<= 2;
    }

    if (t9[MXT_OBJ_T9_ORIENT] & MXT_T9_XY_SWITCH) {
        s->scale_y = div ? ((double)tmp / MXT_QEMU_MAX_COORD) / (double)div : 0;
        s->y_threshold = threshold;
    } else {
        s->scale_x = div ? ((double)tmp / MXT_QEMU_MAX_COORD) / (double)div : 0;
        s->x_threshold = threshold;
    }
}

static inline void mxt_calc_y_scalecoef(MXTState *s)
{
    MXTClass *k = MXT_GET_CLASS(s);
    uint16_t div, tmp;
    uint8_t *t9 = &s->objects[k->t9_address - MXT_OBJECTS_START(k)];
    uint16_t y_max =
            t9[MXT_OBJ_T9_YRANGE_LSB] | (t9[MXT_OBJ_T9_YRANGE_MSB] << 8);
    uint16_t threshold = t9[MXT_OBJ_T9_YSIZE];

    if (y_max == 0) {
        if (t9[MXT_OBJ_T9_ORIENT] & MXT_T9_XY_SWITCH) {
            s->scale_x = 0;
        } else {
            s->scale_y = 0;
        }
        return;
    }

    div = MXT_QEMU_MAX_COORD / y_max + 1;
    tmp = y_max * div;
    /* Divide by 4 if YRANGE less then 1024 */
    if ((t9[MXT_OBJ_T9_YRANGE_MSB] & 0xC) == 0) {
        div >>= 2;
        div++;
        tmp = y_max * (div << 2);
        threshold <<= 2;
    }

    if (t9[MXT_OBJ_T9_ORIENT] & MXT_T9_XY_SWITCH) {
        s->scale_x = div ? ((double)tmp / MXT_QEMU_MAX_COORD) / (double)div : 0;
        s->x_threshold = threshold;
    } else {
        s->scale_y = div ? ((double)tmp / MXT_QEMU_MAX_COORD) / (double)div : 0;
        s->y_threshold = threshold;
    }
}

static void mxt_write_to_t9(MXTState *s, unsigned int offset, uint8_t val)
{
    MXTClass *k = MXT_GET_CLASS(s);
    uint16_t addr = k->t9_address - MXT_OBJECTS_START(k) + offset;

    s->objects[addr] = val;

    switch (offset) {
    case MXT_OBJ_T9_CTRL:
        if ((s->objects[addr] == 0x83) && !(s->mouse)) {
            s->mouse =
                qemu_add_mouse_event_handler(mxt_mouse_event, s, 1, "maxtouch");
            qemu_activate_mouse_event_handler(s->mouse);
        } else if (s->objects[addr] == 0 && s->mouse) {
            qemu_remove_mouse_event_handler(s->mouse);
            s->mouse = NULL;
        }
        break;
    case MXT_OBJ_T9_XSIZE:
    case MXT_OBJ_T9_XRANGE_LSB: case MXT_OBJ_T9_XRANGE_MSB:
        mxt_calc_x_scalecoef(s);
        break;
    case MXT_OBJ_T9_YSIZE:
    case MXT_OBJ_T9_YRANGE_LSB: case MXT_OBJ_T9_YRANGE_MSB:
        mxt_calc_y_scalecoef(s);
        break;
    case MXT_OBJ_T9_ORIENT:
        mxt_calc_x_scalecoef(s);
        mxt_calc_y_scalecoef(s);
        break;
    }
}

/* Atmel maXTouch i2c registers read byte sequence:
 * <Start bit>
 * [MXT i2c address(0x4A) with last bit 0(write data)]
 * [LSB of MXT register offset (starting from 0)]
 * [MSB of MXT register offset]
 * <Stop bit>
 * <Start bit>
 * [MXT address(0x4A) with last bit 1(read data)]
 * [MXT sends 0x0]
 * [MXT sends value of register offset]
 * [MXT sends value of register offset+1]
 * [MXT sends value of register offset+2]
 * [...........]
 * <Stop bit>
 *
 * Atmel maXTouch i2c registers write byte sequence:
 * <Start bit>
 * [MXT address(0x4A) with last bit 0(write data)]
 * [LSB of MXT register offset (starting from 0)]
 * [MSB of MXT register offset]
 * [value to write into register with specified offset]
 * [value to write into register with specified offset+1]
 * [value to write into register with specified offset+2]
 * [...........]
 * <Stop bit>
 */

static int mxt_i2c_read(I2CSlave *i2c)
{
    MXTState *s = MXT(i2c);
    MXTClass *k = MXT_GET_CLASS(s);
    int ret = -1;

    if (s->i2c_state != READING_DATA) {
        ERRPRINT("data read request in wrong state!\n");
        return ret;
    }

    if ((s->selected_reg >= k->t5_address) && (s->selected_reg <
            (k->t5_address + mxt_obj_sizes[MXT_GEN_MESSAGE_T5]))) {
        ret = mxt_read_message_field(s, s->selected_reg - k->t5_address);
    } else if (s->selected_reg <= MXT_CRC_END(k)) {
        ret = mxt_read_info_reg(k, s->selected_reg);
    } else if (s->selected_reg <= k->end_addr) {
        ret = s->objects[s->selected_reg - MXT_OBJECTS_START(k)];
    } else {
        ERRPRINT("register with address 0x%04x doesn't exist\n",
                s->selected_reg);
    }

    DPRINT("Sending %s(0x%02x) -> 0x%02x\n",
            mxt_get_reg_name(s, s->selected_reg), s->selected_reg, ret);
    s->selected_reg++;

    return ret;
}

static int mxt_i2c_write(I2CSlave *i2c, uint8_t data)
{
    MXTState *s = MXT(i2c);
    MXTClass *k = MXT_GET_CLASS(s);

    switch (s->i2c_state) {
    case STARTING_WRITE:
        s->selected_reg = (s->selected_reg & 0xFF00) | data;
        s->i2c_state = SELECTING_REGISTER;
        break;
    case SELECTING_REGISTER:
        s->selected_reg = (s->selected_reg & 0x00FF) | (data << 8);
        DPRINT("Selected register 0x%04x\n", s->selected_reg);
        s->i2c_state = WRITING_DATA;
        break;
    case WRITING_DATA:
        DPRINT("Writing %s <- 0x%02x\n",
                mxt_get_reg_name(s, s->selected_reg), data);
        if ((s->selected_reg >= k->t9_address) && (s->selected_reg <
                k->t9_address + mxt_obj_sizes[MXT_TOUCH_MULTI_T9])) {
            mxt_write_to_t9(s, s->selected_reg - k->t9_address, data);
        } else if ((s->selected_reg >= MXT_OBJECTS_START(k)) &&
            (s->selected_reg <= k->end_addr)) {
            s->objects[s->selected_reg - MXT_OBJECTS_START(k)] = data;
        } else {
            ERRPRINT("can't write to register with address 0x%04x\n",
                    s->selected_reg);
            return -1;
        }
        s->selected_reg++;
        break;
    default:
        ERRPRINT("data write request in wrong state!\n");
        return -1;
    }

    return 0;
}

static void mxt_i2c_event(I2CSlave *i2c, enum i2c_event event)
{
    MXTState *s = MXT(i2c);

    switch (event) {
    case I2C_START_RECV:
        DPRINT("I2C start bit appeared: reading data\n");
        s->i2c_state = READING_DATA;
        break;
    case I2C_START_SEND:
        DPRINT("I2C start bit appeared: writing data\n");
        s->i2c_state = STARTING_WRITE;
        break;
    case I2C_FINISH:
        DPRINT("I2C stop bit received\n");
        s->i2c_state = IDLE;
        break;
    default:
        break;
    }
}

static void mxt_initfn(Object *obj)
{
    MXTState *s = MXT(obj);
    MXTClass *k = MXT_GET_CLASS(obj);
    unsigned i;
    uint16_t objects_len = 0;

    for (i = 0; i < k->mxt_info[MXT_OBJ_NUM]; i++) {
        objects_len += (k->obj_tbl[i].size + 1) * (k->obj_tbl[i].instances + 1);
    }
    s->objects = g_malloc0(objects_len);
    s->obj_len = objects_len;

    s->i2c_state = IDLE;
    s->selected_reg = 0;
    s->fifo_add = s->fifo_get = 0;
    s->fifo_lock = false;
    s->scale_x = 0;
    s->scale_y = 0;
    s->x_threshold = 0;
    s->y_threshold = 0;
    s->x_curr = 0;
    s->y_curr = 0;
    s->touches = 0;
    s->mouse = NULL;

    qdev_init_gpio_out(DEVICE(obj), &s->nCHG, 1);
    qemu_irq_raise(s->nCHG);
}

static void mxt_uninitfn(Object *obj)
{
    MXTState *s = MXT(obj);

    if (s->mouse) {
        qemu_remove_mouse_event_handler(s->mouse);
        s->mouse = NULL;
    }

    g_free(s->objects);
}

static int mxt_i2c_init(I2CSlave *i2c)
{
    return 0;
}

static inline uint32_t mxt_crc24(uint32_t crc, uint8_t byte1, uint8_t byte2)
{
    uint32_t ret = (crc << 1) ^ ((byte2 << 8) | byte1);

    if (ret & 0x1000000) {
        ret ^= MXT_CRC_POLY;
    }

    return ret;
}

static void mxt_calculate_crc(MXTClass *k)
{
    unsigned i;
    uint32_t crc = 0;

    for (i = 0; i < MXT_OBJTBL_END(k); i += 2) {
        crc =
           mxt_crc24(crc, mxt_read_info_reg(k, i), mxt_read_info_reg(k, i + 1));
    }

    crc = mxt_crc24(crc, mxt_read_info_reg(k, i), 0) & 0x00FFFFFF;
    k->crc[0] = crc & 0xFF;
    k->crc[1] = (crc >> 8) & 0xFF;
    k->crc[2] = (crc >> 16) & 0xFF;
}

static void mxt_init_object_table(MXTClass *k, const ObjConfig *list)
{
    MXTObjTblEntry *tbl = k->obj_tbl;
    unsigned i, tbl_len = k->mxt_info[MXT_OBJ_NUM];

    for (i = 0; i < tbl_len; i++) {
        tbl[i].type = mxt_obj_types_list[list[i].type];
        tbl[i].size = mxt_obj_sizes[list[i].type] - 1;
        tbl[i].instances = list[i].instances;
        tbl[i].num_report_ids =
                (list[i].type == MXT_TOUCH_MULTI_T9) ? MXT_NUM_OF_TOUCHES : 0;
        if (i == 0) {
            tbl[i].start_addr = MXT_OBJTBL_START + tbl_len *
                    MXT_OBJTBL_ENTRY_LEN + MXT_CRC_SIZE;
        } else {
            tbl[i].start_addr = tbl[i-1].start_addr +
                    (tbl[i-1].size + 1) * (tbl[i-1].instances + 1);
        }
        if (list[i].type == MXT_GEN_MESSAGE_T5) {
            k->t5_address = tbl[i].start_addr;
        } else if (list[i].type == MXT_TOUCH_MULTI_T9) {
            k->t9_address = tbl[i].start_addr;
        }
    }

    k->end_addr = tbl[i-1].start_addr + tbl[i-1].size;
    /* T5 and T9 objects are mandatory */
    assert(k->t5_address);
    assert(k->t9_address);
}

static int mxt_post_load(void *opaque, int ver_id)
{
    MXTState *s = (MXTState *)opaque;
    MXTClass *k = MXT_GET_CLASS(s);
    uint16_t addr = k->t9_address - MXT_OBJECTS_START(k) + MXT_OBJ_T9_CTRL;

    if (s->fifo_get == s->fifo_add) {
        s->fifo_lock = true;
    }
    mxt_calc_x_scalecoef(s);
    mxt_calc_y_scalecoef(s);

    if ((s->objects[addr] == 0x83) && !(s->mouse)) {
        s->mouse =
            qemu_add_mouse_event_handler(mxt_mouse_event, s, 1, "maxtouch");
        qemu_activate_mouse_event_handler(s->mouse);
    }

    return 0;
}

static const VMStateDescription mxt_vmstate = {
    .name = "mxt",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = mxt_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_VBUFFER_UINT32(objects, MXTState, 1, NULL, 0, obj_len),
        VMSTATE_UINT8(i2c_state, MXTState),
        VMSTATE_STRUCT_ARRAY(msg_fifo, MXTState, MXT_MESSAGE_FIFO_LEN, 1,
                mxt_message_vmstate, MXTMessage),
        VMSTATE_UINT8(fifo_get, MXTState),
        VMSTATE_UINT8(fifo_add, MXTState),
        VMSTATE_UINT16(selected_reg, MXTState),
        VMSTATE_UINT16(x_curr, MXTState),
        VMSTATE_UINT16(y_curr, MXTState),
        VMSTATE_UINT8(touches, MXTState),
        VMSTATE_END_OF_LIST()
    }
};

static void maxtouch_class_init(ObjectClass *klass, void *data)
{
    I2CSlaveClass *i2c = I2C_SLAVE_CLASS(klass);
    MXTClass *k = MXT_CLASS(klass);
    const MXTVariantInfo *info = (const MXTVariantInfo *)data;

    DEVICE_CLASS(klass)->vmsd = &mxt_vmstate;
    i2c->init = mxt_i2c_init;
    i2c->event = mxt_i2c_event;
    i2c->recv = mxt_i2c_read;
    i2c->send = mxt_i2c_write;

    k->mxt_info = info->mxt_variant_info;
    k->obj_tbl = g_new0(MXTObjTblEntry, k->mxt_info[MXT_OBJ_NUM]);
    mxt_init_object_table(k, info->mxt_variant_obj_list);
    mxt_calculate_crc(k);
}

static void maxtouch_class_finalize(ObjectClass *klass, void *data)
{
    MXTClass *k = MXT_CLASS(klass);

    g_free(k->obj_tbl);
}

static void mxt_register_type(const MXTVariantInfo *info)
{
    TypeInfo type = {
        .name = info->name,
        .parent = TYPE_MAXTOUCH,
        .class_init = maxtouch_class_init,
        .class_finalize = maxtouch_class_finalize,
        .class_data = (void *)info
    };

    type_register(&type);
}

static const TypeInfo maxtouch_type_info = {
    .name = TYPE_MAXTOUCH,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(MXTState),
    .instance_init = mxt_initfn,
    .instance_finalize = mxt_uninitfn,
    .abstract = true,
    .class_size = sizeof(MXTClass),
};

static void mxt_register_types(void)
{
    unsigned i;

    type_register_static(&maxtouch_type_info);
    for (i = 0; i < MXT_NUM_OF_VARIANTS; i++) {
        mxt_register_type(&mxt_variants_info_array[i]);
    }
}

type_init(mxt_register_types)
