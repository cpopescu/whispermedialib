/* synaescope.h
 * Copyright (C) 1999,2002 Richard Boulton <richard@tartarus.org>
 *
 * Much code copied from Synaesthesia - a program to display sound
 * graphically, by Paul Francis Harrison <pfh@yoyo.cc.monash.edu.au>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#ifndef _SYNAESCOPE_H
#define _SYNAESCOPE_H

#include <glib.h>

typedef struct syn_instance syn_instance;

void synaesthesia_init ();
syn_instance *synaesthesia_new (guint32 resx, guint32 resy);
void synaesthesia_close (syn_instance *si);
guint32 * synaesthesia_update (syn_instance *si, gint16 data [2][512]);

#endif
