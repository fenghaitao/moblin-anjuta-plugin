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

#ifndef _BEAVER_TARGET
#define _BEAVER_TARGET

#include <glib-object.h>

#include <libanjuta/anjuta-plugin.h>
#include <libanjuta/interfaces/ianjuta-message-manager.h>
#include <libanjuta/anjuta-launcher.h>

G_BEGIN_DECLS

#define BEAVER_TYPE_TARGET beaver_target_get_type()

#define BEAVER_TARGET(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  BEAVER_TYPE_TARGET, BeaverTarget))

#define BEAVER_TARGET_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  BEAVER_TYPE_TARGET, BeaverTargetClass))

#define BEAVER_IS_TARGET(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  BEAVER_TYPE_TARGET))

#define BEAVER_IS_TARGET_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
  BEAVER_TYPE_TARGET))

#define BEAVER_TARGET_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
  BEAVER_TYPE_TARGET, BeaverTargetClass))

typedef enum
{
  TARGET_STATE_UNKNOWN,
  TARGET_STATE_STOPPED,
  TARGET_STATE_READY,
  TARGET_STATE_BUSY,
  TARGET_STATE_REMOTE_RUNNING,
  TARGET_STATE_DEBUGGER_READY
} BeaverTargetState;

typedef struct {
  GObject parent;
} BeaverTarget;

typedef struct {
  GObjectClass parent_class;

  /* for signals */
  void (* state_changed) (BeaverTarget *target);

  /* for vfuncs */
  BeaverTargetState (* get_state) (BeaverTarget *target);
  void (* set_state) (BeaverTarget *target, BeaverTargetState state);
  const gchar * (* get_ip_address) (BeaverTarget *target);

  gboolean (* run_remote_v) (BeaverTarget *target, gchar **args_in, 
      GError **error);
  gboolean (* run_remote) (BeaverTarget *target, gchar *cmd,
      GError **error);
  gboolean (* remote_debug) (BeaverTarget *target, gchar *cmd, gchar *args,
      GError **error);
  gboolean (* remote_debug_stop) (BeaverTarget *target, GError **error);
  void (* remote_stop) (BeaverTarget *target);
} BeaverTargetClass;

GType beaver_target_get_type (void);

BeaverTargetState beaver_target_get_state (BeaverTarget *target);
void beaver_target_set_state (BeaverTarget *target, BeaverTargetState state);
const gchar *beaver_target_get_ip_address (BeaverTarget *target);
IAnjutaMessageManager *beaver_target_get_message_manager (BeaverTarget *target);
gboolean beaver_target_run_remote_v (BeaverTarget *target, gchar **in_args,
    GError **error);
gboolean beaver_target_remote_debug (BeaverTarget *target, gchar *cmd, 
    gchar *args, GError **error);
gboolean beaver_target_remote_debug_stop (BeaverTarget *target, GError **error);
void beaver_target_remote_stop (BeaverTarget *target);
G_END_DECLS

#endif /* _BEAVER_TARGET */
