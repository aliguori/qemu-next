#ifndef SPICE_RING_H
#define SPICE_RING_H


#define MSB_MASK4(x)        \
    (((x) & 0x8) ? 0x8 :    \
     ((x) & 0x4) ? 0x4 :    \
     ((x) & 0x2) ? 0x2 :    \
     ((x) & 0x1) ? 0x1 : 0)

#define MSB_MASK8(x) \
     (((x) & 0xf0) ? MSB_MASK4((x) >> 4) << 4 : MSB_MASK4(x))

#define MSB_MASK16(x) \
    (((x) & 0xff00) ? MSB_MASK8((x) >> 8) << 8 : MSB_MASK8(x))

#define MSB_MASK(x) \
    (((x) & 0xffff0000) ? MSB_MASK16((x) >> 16) << 16 : MSB_MASK16(x))

#define POWER2_ALIGN(x) MSB_MASK((x) * 2 - 1)


#define _TOSHIFT_4(x) \
     (((x) & 0x8) ? 3 :    \
      ((x) & 0x4) ? 2 :    \
      ((x) & 0x2) ? 1 : 0)

#define _TOSHIFT_8(x) \
    (((x) & 0xf0) ? _TOSHIFT_4((x) >> 4) + 4 : _TOSHIFT_4(x))

#define _TOSHIFT_16(x) \
    (((x) & 0xff00) ? _TOSHIFT_8((x) >> 8) + 8 : _TOSHIFT_8(x))

#define PAWER2_TO_SHIFT(x) \
    (((x) & 0xffff0000) ? _TOSHIFT_16((x) >> 16) + 16 : _TOSHIFT_16(x))



#define RING_DECLARE(name, el_type, size)               \
typedef struct ATTR_PACKED name##_ring_el {             \
    union {                                             \
        el_type el;                                     \
        UINT8 data[POWER2_ALIGN(sizeof(el_type))];      \
    } ;                                                 \
} name##_ring_el;                                       \
                                                        \
typedef struct ATTR_PACKED name {                       \
    UINT32 num_items;                                   \
    UINT32 prod;                                        \
    UINT32 notify_on_prod;                              \
    UINT32 cons;                                        \
    UINT32 notify_on_cons;                              \
    name##_ring_el items[POWER2_ALIGN(size)];           \
} name;


#define RING_INIT(r)                                                \
    (r)->num_items = sizeof((r)->items) >>                          \
                        PAWER2_TO_SHIFT(sizeof((r)->items[0]));     \
    (r)->prod = (r)->cons = 0;                                      \
    (r)->notify_on_prod = 1;                                        \
    (r)->notify_on_cons = 0;


#define RING_INDEX_MASK(r) ((r)->num_items - 1)

#define RING_IS_PACKED(r) (sizeof((r)->items[0]) == sizeof((r)->items[0]).el)

#define RING_IS_EMPTY(r) ((r)->cons == (r)->prod)

#define RING_IS_FULL(r) (((r)->prod - (r)->cons) == (r)->num_items)

#define RING_PROD_ITEM(r) (&(r)->items[(r)->prod & RING_INDEX_MASK(r)].el)

#define RING_PROD_WAIT(r, wait)                 \
    if (((wait) = RING_IS_FULL(r))) {           \
        (r)->notify_on_cons = (r)->cons + 1;    \
        mb();	                                \
        (wait) = RING_IS_FULL(r);               \
    }

#define RING_PUSH(r, notify)                    \
    (r)->prod++;                                \
    mb();                                       \
    (notify) = (r)->prod == (r)->notify_on_prod;


#define RING_CONS_ITEM(r) (&(r)->items[(r)->cons & RING_INDEX_MASK(r)].el)

#define RING_CONS_WAIT(r, wait)                 \
    if (((wait) = RING_IS_EMPTY(r))) {          \
        (r)->notify_on_prod = (r)->prod + 1;    \
        mb();                                   \
        (wait) = RING_IS_EMPTY(r);              \
    }

#define RING_POP(r, notify)                         \
    (r)->cons++;                                    \
    mb();                                           \
    (notify) = (r)->cons == (r)->notify_on_cons;



#endif
