/*
 * Copyright (C) 2007, 2008 OpenedHand Ltd.
 * Authored by: Rob Bradford <rob@o-hand.com>
 *
 * This is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, version 2 of the License.
 *
 * This software is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _BEAVER_UTIL
#define _BEAVER_UTIL

#include <config.h>
#include <glib-object.h>

#include <libanjuta/libanjuta.h>
#include <libanjuta/interfaces/ianjuta-message-manager.h>

void beaver_util_message_view_buffer_flushed_cb (IAnjutaMessageView *view, 
    gchar *data, gpointer userdata);
gchar **beaver_util_strv_concat (gchar **strv_1, gchar **strv_2);
gchar **beaver_util_strv_joinv (gchar **strv_1, ...);

#ifdef ANJUTA_CHECK_VERSION
#if LIBANJUTA_MAJOR_VERSION >= 2 && LIBANJUTA_MINOR_VERSION >= 23 
#if LIBANJUTA_MINOR_VERSION >= 28
#define ANJUTA_2_28_OR_HIGHER
#else
#define ANJUTA_2_23_TO_26
#endif
#endif
#endif

#endif /* _BEAVER_UTIL */
