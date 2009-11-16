/*
 * Copyright (C) 2007 OpenedHand Ltd.
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

#ifndef _ANJUTA_PLUGIN_SDK_H_
#define _ANJUTA_PLUGIN_SDK_H_

#include <libanjuta/anjuta-plugin.h>
#include <libanjuta/interfaces/ianjuta-message-manager.h>
#include <libanjuta/interfaces/ianjuta-buildable.h>
#include <libanjuta/anjuta-launcher.h>

#include "beaver.h"

typedef struct _AnjutaPluginSdk AnjutaPluginSdk;
typedef struct _AnjutaPluginSdkClass AnjutaPluginSdkClass;

/*
 * State enum for the deployment state machine
 */
typedef enum
{
  DEPLOY_STATE_INITIAL,
  DEPLOY_STATE_LOCAL_INSTALL,
  DEPLOY_STATE_COPY,
  DEPLOY_STATE_DELETE,
  DEPLOY_STATE_FINISHED,
  DEPLOY_STATE_ERROR
} DeployState;

typedef enum
{
  POKY_MODE_TOOLCHAIN = 0,
  POKY_MODE_FULL
} PokyMode;

typedef enum
{
  TARGET_MODE_QEMU,
  TARGET_MODE_DEVICE
} TargetMode;

struct _AnjutaPluginSdk
{
  AnjutaPlugin parent;

  AnjutaPreferences *prefs;

  GtkActionGroup *action_group;

  guint sdk_root_notifyid;
  guint triplet_notifyid;
  guint kernel_notifyid;
  guint rootfs_notifyid;
  guint poky_root_notifyid;
  guint poky_mode_notifyid;
  guint target_mode_notifyid;
  guint target_ip_notifyid;

  guint remote_gdb_timeout;

  gchar *triplet;
  gchar *sdk_root;
  gchar *poky_root;
  PokyMode poky_mode;

  TargetMode target_mode;

  gchar *path_component;
  gchar *poky_scripts_dir;
  gchar *poky_host_staging_usr_bin_dir;
  gchar *poky_host_staging_bin_dir;
  gchar *poky_cross_dir;

  gint uiid;

  guint project_root_uri_watch;

  gchar *project_root_uri;

  IAnjutaMessageView *deploy_msg_view;
  IAnjutaMessageView *remote_msg_view;

  AnjutaLauncher *deploy_launcher;
  AnjutaLauncher *remote_launcher;
  AnjutaLauncher *remote_gdb_launcher;
  AnjutaLauncher *oprofileui_launcher;

  gchar *deploy_path;
  DeployState deploy_state;

  GtkAction *deploy_action;
  GtkAction *qemu_start_action;
  GtkAction *qemu_shutdown_action;
  GtkAction *remote_run_action;
  GtkAction *remote_debug_action;
  GtkAction *remote_debug_stop_action;
  GtkAction *remote_profile_action;
  GtkAction *remote_stop_action;

  AnjutaSession *session;
  AnjutaPluginManager *plugin_manager;

  GtkWidget *dialog;
  GdkPixbuf *prefs_icon;

  gchar *gdb_local_path;
  gchar *gdb_remote_command;
  gchar *remote_command;

  IAnjutaBuildable *buildable;

  BeaverTarget *target;
};

struct _AnjutaPluginSdkClass
{
  AnjutaPluginClass parent_class;
};
#endif
