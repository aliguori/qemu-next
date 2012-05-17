/*
 * OpenRISC helper defines
 *
 *  Copyright (c) 2011-2012 Jia Liu <proljc@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
 */

#include "def-helper.h"

/* exception */
DEF_HELPER_FLAGS_2(exception, 0, void, env, i32)

/* float */
DEF_HELPER_FLAGS_2(itofd, 0, tl, env, tl)
DEF_HELPER_FLAGS_2(itofs, 0, tl, env, tl)
DEF_HELPER_FLAGS_2(ftoid, 0, tl, env, tl)
DEF_HELPER_FLAGS_2(ftois, 0, tl, env, tl)

/* int */
DEF_HELPER_FLAGS_1(ff1, 0, tl, tl)
DEF_HELPER_FLAGS_1(fl1, 0, tl, tl)
DEF_HELPER_FLAGS_3(add, 0, tl, env, tl, tl)
DEF_HELPER_FLAGS_3(addc, 0, tl, env, tl, tl)
DEF_HELPER_FLAGS_3(sub, 0, tl, env, tl, tl)

/* interrupt */
DEF_HELPER_FLAGS_1(rfe, 0, void, env)

/* sys */
DEF_HELPER_FLAGS_4(mtspr, 0, void, env, tl, tl, tl)
DEF_HELPER_FLAGS_4(mfspr, 0, void, env, tl, tl, tl)

#include "def-helper.h"
