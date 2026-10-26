/* super.h stand-in for building kernellib finditem.c / CreateItem.c (the Portfolio-internal header is not available).
 * Only the two prototypes those files need. */
#ifndef __SUPER_H
#define __SUPER_H
extern Item SuperFindItem(int32 ctype, TagArg *tp);
extern Item SuperCreateSizedItem(int32 ctype, TagArg *p, int32 size);
#endif
