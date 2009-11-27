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

#include <config.h>
#include <libanjuta/anjuta-shell.h>
#include <libanjuta/anjuta-debug.h>
#include <libanjuta/interfaces/ianjuta-document-manager.h>
#include <libanjuta/interfaces/ianjuta-preferences.h>
#include <libanjuta/interfaces/ianjuta-terminal.h>

#include <libgnomevfs/gnome-vfs.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/utsname.h>

#include "plugin.h"

/* Little hack so we can build on Glib 2.12 and below */
#if ! GLIB_CHECK_VERSION (2, 14, 0)
#define g_timeout_add_seconds(interval, function, data) g_timeout_add ((interval) * 1000, function, data)
#endif

#define UI_FILE ANJUTA_DATA_DIR"/ui/anjuta-plugin-sdk.ui"

#define ICON_FILE "anjuta-plugin-sdk.png"
#define ICON_PATH ANJUTA_IMAGE_DIR"/"ICON_FILE
#define SSH_OPTIONS "-o", "CheckHostIP no", "-o", \
    "StrictHostKeyChecking no", "-o", "UserKnownHostsFile /dev/null"

#define CONFIGURE_COMMAND "./configure --host=%s"
#define AUTOGEN_COMMAND "./autogen.sh --host=%s"

#define DEPLOY_COMMAND "rsync " \
  "-e 'ssh -o \"CheckHostIP no\" " \
  "-o \"StrictHostKeyChecking no\" " \
  "-o \"UserKnownHostsFile /dev/null\"' " \
  "-avv %s/usr/ root@%s:/usr"

#define LOCAL_GDB_COMMAND "%s-gdb -x %s %s"

#define GDB_SCRIPT "set solib-absolute-prefix %s\n" \
  "set debug-file-directory %s\n" \
  "target remote %s:2345\n"

#define OPROFILEUI_COMMAND "oprofile-viewer -h %s -s %s"

static gpointer anjuta_plugin_sdk_parent_class;

/* Callback prototypes needed for actions */
static void action_deploy_activate_cb (GtkAction *action, gpointer userdata);
static void action_start_qemu_activate_cb (GtkAction *action, 
    gpointer userdata);
static void action_shutdown_qemu_activate_cb (GtkAction *action, 
    gpointer userdata);
static void action_remote_run_activate_cb (GtkAction *action,
    gpointer userdata);
static void action_remote_debug_activate_cb (GtkAction *action,
    gpointer userdata);
static void action_remote_debug_stop_activate_cb (GtkAction *action,
    gpointer userdata);
static void action_remote_profile_activate_cb (GtkAction *action,
    gpointer userdata);
static void action_remote_stop_activate_cb (GtkAction *action,
    gpointer userdata);

/* actions */
static GtkActionEntry actions_sdk[] = {
  {
    "ActionMenuTools",  /* Action name */
    NULL,               /* Stock icon, if any */
    N_("_Tools"),       /* Display label */
    NULL,               /* Short-cut */
    NULL,               /* Tooltip */
    NULL                /* Callback */
  },
  {
    "ActionDeploy",     /* Action name */
    NULL,               /* Stock icon, if any */
    N_("Deploy"),    /* Display label */
    NULL,               /* short-cut */
    N_("Deploy"),    /* Tooltip */
    G_CALLBACK (action_deploy_activate_cb)  /* action callback */
  },
  {
    "ActionStartQemu",                        /* Action name */
    GTK_STOCK_EXECUTE,                        /* Stock icon, if any */
    N_("Start QEMU"),                         /* Display label */
    NULL,                                     /* short-cut */
    N_("Start QEMU"),                         /* Tooltip */
    G_CALLBACK (action_start_qemu_activate_cb)  /* action callback */
  },
  {
    "ActionShutdownQemu",
    GTK_STOCK_CLOSE,
    N_("Shutdown QEMU"),
    NULL,
    N_("Shutdown QEMU"),
    G_CALLBACK (action_shutdown_qemu_activate_cb)
  },
  {
    "ActionRunRemote",
    GTK_STOCK_EXECUTE,
    N_("Run remote..."),
    NULL,
    N_("Run remote.."),
    G_CALLBACK (action_remote_run_activate_cb)
  },
  {
    "ActionStopRemote",
    GTK_STOCK_CLOSE,
    N_("Stop remote"),
    NULL,
    N_("Stop remote"),
    G_CALLBACK (action_remote_stop_activate_cb)
  },
  {
    "ActionDebugRemote",
    GTK_STOCK_EXECUTE,
    N_("Debug remote..."),
    NULL,
    N_("Debug remote..."),
    G_CALLBACK (action_remote_debug_activate_cb)
  },
  {
    "ActionStopRemoteDebugger",
    GTK_STOCK_CLOSE,
    N_("Stop debugger..."),
    NULL,
    N_("Stop debugger..."),
    G_CALLBACK (action_remote_debug_stop_activate_cb)
  },
  {
    "ActionRemoteProfile",
    NULL,
    N_("Profile remote..."),
    NULL,
    N_("Profile remote..."),
    G_CALLBACK (action_remote_profile_activate_cb)
  }
};

/* Misc callback prototypes */
static void message_view_buffer_flushed_cb (IAnjutaMessageView *view, 
    gchar *data, gpointer userdata);
static void remote_launcher_child_exited_cb (AnjutaLauncher *launcher,
    gint child_pid, gint status, gulong time, gpointer userdata);
static void oprofileui_launcher_child_exited_cb (AnjutaLauncher *launcher,
    gint child_pid, gint status, gulong time, gpointer userdata);
static void target_state_changed_cb (BeaverTarget *target, gpointer userdata);

/* Prototypes for deployment related activities */
static void deploy_set_state (AnjutaPluginSdk *sp, DeployState deploy_state);
static void deploy_do_initial_state (AnjutaPluginSdk *sp);
static void deploy_do_local_install (AnjutaPluginSdk *sp);
static void deploy_do_copy (AnjutaPluginSdk *sp);
static void deploy_do_delete (AnjutaPluginSdk *sp);
static void deploy_do_finished (AnjutaPluginSdk *sp);
static void deploy_do_error (AnjutaPluginSdk *sp);

static gboolean
lookup_sysroot_dir (AnjutaPluginSdk *sp)
{
  gboolean has_sysroot_dir = FALSE;
  GError *error = NULL;
  GDir   *sysroot_dir = NULL;
  gchar  *tmp = g_build_filename (sp->sdk_root, sp->triplet, "sys-root", NULL);

  sysroot_dir = g_dir_open(tmp, 0, &error);
  if (sysroot_dir)
  {
    has_sysroot_dir = TRUE;
    g_dir_close(sysroot_dir);
  }
  g_free (tmp);

  return has_sysroot_dir;
}

/* Callback fired when the launcher finishes */
static void
deploy_launcher_child_exited_cb (AnjutaLauncher *launcher, gint child_pid,
    gint status, gulong time, gpointer userdata)
{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)userdata;

  switch (sp->deploy_state)
  {
    case DEPLOY_STATE_LOCAL_INSTALL:
      if (WEXITSTATUS (status) != 0)
        deploy_set_state (sp, DEPLOY_STATE_ERROR);
      else
        deploy_set_state (sp, DEPLOY_STATE_COPY);
      break;
    case DEPLOY_STATE_COPY:
      /*
       * don't check error code here because rysnc can sometimes get it
       * wrong
       */
      deploy_set_state (sp, DEPLOY_STATE_DELETE);
      break;
  }
}

/*
 * Callback for when data is received by the launcher
 */
static void
deploy_launcher_data_cb (AnjutaLauncher *launcher, 
    AnjutaLauncherOutputType type, const gchar *chars, gpointer userdata)
{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)userdata;
  GError *error = NULL;

  if (sp->deploy_msg_view)
  {
    /* Append to the buffer for the message view to deal with the newlines */
    ianjuta_message_view_buffer_append (sp->deploy_msg_view, chars, &error);

    if (error != NULL)
    {
      g_warning ("Error appending to message view: %s", error->message);
      g_clear_error (&error);
    }
  }
}

static void
remote_gdb_launcher_data_cb (AnjutaLauncher *launcher,
    AnjutaLauncherOutputType type, const gchar *chars, gpointer userdata)
{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)userdata;
  GError *error = NULL;

  if (sp->remote_msg_view)
  {
    /* Append to the buffer for the message view to deal with the newlines */
    ianjuta_message_view_buffer_append (sp->remote_msg_view, chars, &error);

    if (error != NULL)
    {
      g_warning ("Error appending to message view: %s", error->message);
      g_clear_error (&error);
    }
  }
}

/* Update the current state */
static void
deploy_set_state (AnjutaPluginSdk *sp, DeployState deploy_state)
{
  sp->deploy_state = deploy_state;

  switch (deploy_state)
  {
    case DEPLOY_STATE_INITIAL:
      deploy_do_initial_state (sp);
      break;
    case DEPLOY_STATE_LOCAL_INSTALL:
      deploy_do_local_install (sp);
      break;
    case DEPLOY_STATE_COPY:
      deploy_do_copy (sp);
      break;
    case DEPLOY_STATE_DELETE:
      deploy_do_delete (sp);
      break;
    case DEPLOY_STATE_FINISHED:
      deploy_do_finished (sp);
      break;
    case DEPLOY_STATE_ERROR:
      deploy_do_error (sp);
      break;
  }
}

/* DEPLOY_STATE_INITIAL */
static void
deploy_do_initial_state (AnjutaPluginSdk *sp)
{
  GError *error = NULL;
  gchar *project_root_dir = NULL;
  IAnjutaMessageManager *msg_manager = NULL;

  if (sp->project_root_uri)
  {
    project_root_dir = g_filename_from_uri (sp->project_root_uri, NULL, &error);

    if (!project_root_dir)
    {
      g_warning ("Error converting uri to directory name: %s", error->message);
      g_clear_error (&error);
      goto error;
    }
  }
  
  /* get the message view manager */
  msg_manager = anjuta_shell_get_interface (ANJUTA_PLUGIN (sp)->shell,
      IAnjutaMessageManager, &error);

  if (!msg_manager)
  {
    g_warning ("Error getting implementation of IAnjutaMessageManager: %s",
        error->message);
    g_clear_error (&error);

    goto error;
  }

  if (project_root_dir)
  {
    /* Make the deploy menu option insensitive */
    gtk_action_set_sensitive (sp->deploy_action, FALSE);

    if (!sp->deploy_msg_view)
    {
      /* Create a new view */
      sp->deploy_msg_view = ianjuta_message_manager_add_view (msg_manager,
          _("Deploy"), ICON_FILE, &error);

      if (!sp->deploy_msg_view)
      {
        g_warning ("Error getting view: %s", error->message);
        g_clear_error (&error);

        goto error;
      }

      g_signal_connect (sp->deploy_msg_view, "buffer-flushed", 
          (GCallback)message_view_buffer_flushed_cb, sp);

      /* When the view is destroyed make the pointer to it null */
      g_object_add_weak_pointer (G_OBJECT (sp->deploy_msg_view), 
          (gpointer *)&sp->deploy_msg_view);
    }

    ianjuta_message_manager_set_current_view (msg_manager, sp->deploy_msg_view,
        &error);

    if (error != NULL)
    {
      g_warning ("Error setting current message view: %s", error->message);
      g_clear_error (&error);
      goto error;
    }

    if (!sp->deploy_launcher)
    {
      sp->deploy_launcher = anjuta_launcher_new ();

      g_signal_connect (sp->deploy_launcher, "child-exited",
          (GCallback)deploy_launcher_child_exited_cb, sp);
    }

    if (!sp->deploy_path)
      sp->deploy_path = g_build_filename (project_root_dir, ".deploy", NULL);

    deploy_set_state (sp, DEPLOY_STATE_LOCAL_INSTALL);
  } else {
    g_warning ("No project path. Unable to deploy.");
  }

  goto done;
error:

  g_free (project_root_dir);
  gtk_action_set_sensitive (sp->deploy_action, TRUE);

done:
  return;
}

/* DEPLOY_STATE_LOCAL_INSTALL */
static void
deploy_do_local_install (AnjutaPluginSdk *sp)
{
  gchar *deploy_cmd = NULL;

  deploy_cmd = g_strdup_printf ("make install DESTDIR=%s", 
      sp->deploy_path);

  ianjuta_message_view_append (sp->deploy_msg_view, IANJUTA_MESSAGE_VIEW_TYPE_INFO,
      _("Installing into local deployment area"), "", NULL);

  if (anjuta_launcher_execute (sp->deploy_launcher, deploy_cmd,
      deploy_launcher_data_cb, sp))
  {
    /* The next step in the state machine is dealt with by the callback */
  } else {
    g_warning ("Error launching make install");
    gtk_action_set_sensitive (sp->deploy_action, TRUE);
  }

  g_free (deploy_cmd);
}

/* DEPLOY_STATE_COPY */
static void
deploy_do_copy (AnjutaPluginSdk *sp)
{
  const gchar *ip_address = NULL;
  gchar *copy_cmd = NULL;

  ip_address = beaver_target_get_ip_address (sp->target);
  copy_cmd = g_strdup_printf (DEPLOY_COMMAND, sp->deploy_path, ip_address);

  ianjuta_message_view_append (sp->deploy_msg_view, IANJUTA_MESSAGE_VIEW_TYPE_INFO,
      _("Copying files to target"), "", NULL);

  if (anjuta_launcher_execute (sp->deploy_launcher, copy_cmd,
      deploy_launcher_data_cb, sp))
  {
    /* The next step in the state machine is dealt with by the callback */
  } else {
    g_warning ("Error launching rsync copy to target");
    gtk_action_set_sensitive (sp->deploy_action, TRUE);
  }

  g_free (copy_cmd);
}

/* DEPLOY_STATE_DELETE */
static void
deploy_do_delete (AnjutaPluginSdk *sp)
{
  GnomeVFSResult res;
  GnomeVFSURI *uri = NULL;
  GList *list = NULL;

  ianjuta_message_view_append (sp->deploy_msg_view, IANJUTA_MESSAGE_VIEW_TYPE_INFO,
      _("Deleting temporary deployment area"), "", NULL);

  if (sp->deploy_path)
  {
    uri = gnome_vfs_uri_new (sp->deploy_path);

    list = g_list_append (list, uri);

    res = gnome_vfs_xfer_delete_list (list, 
        GNOME_VFS_XFER_ERROR_MODE_ABORT, GNOME_VFS_XFER_DELETE_ITEMS,
        NULL,
        NULL);

    if (res != GNOME_VFS_OK)
    {
      g_warning ("Error whilst deleting temporary deployment area: %s",
          gnome_vfs_result_to_string (res));
      gnome_vfs_uri_unref (uri);
      deploy_set_state (sp, DEPLOY_STATE_ERROR);
    }
    gnome_vfs_uri_unref (uri);
  }

  deploy_set_state (sp, DEPLOY_STATE_FINISHED);
}


/* DEPLOY_STATE_FINISHED */
static void
deploy_do_finished (AnjutaPluginSdk *sp)
{
  ianjuta_message_view_append (sp->deploy_msg_view, IANJUTA_MESSAGE_VIEW_TYPE_INFO,
      _("Deployment finished"), "", NULL);

  gtk_action_set_sensitive (sp->deploy_action, TRUE);
}

/* DEPLOY_STATE_ERROR */
static void
deploy_do_error (AnjutaPluginSdk *sp)
{
  ianjuta_message_view_append (sp->deploy_msg_view, IANJUTA_MESSAGE_VIEW_TYPE_ERROR,
      _("Error during deployment"), "", NULL);

  gtk_action_set_sensitive (sp->deploy_action, TRUE);
}
/* End of deployment related functions */

/* Action callbacks */

static void
action_shutdown_qemu_activate_cb (GtkAction *actio, gpointer userdata)
{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)userdata;

  if (BEAVER_IS_TARGET_QEMU (sp->target))
  {
    beaver_target_qemu_shutdown (BEAVER_TARGET_QEMU (sp->target), NULL);
  }
}

static void
action_start_qemu_activate_cb (GtkAction *action, gpointer userdata)
{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)userdata;

  if (BEAVER_IS_TARGET_QEMU (sp->target))
  {
    beaver_target_qemu_start (BEAVER_TARGET_QEMU(sp->target), NULL);
  }
}

static void
action_remote_debug_stop_activate_cb (GtkAction *action, gpointer userdata)
{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)userdata;

  if (!beaver_target_remote_debug_stop (sp->target, NULL))
  {
    g_warning ("Error whilst stopping remote debugger on target");
  }
}

static void
action_deploy_activate_cb (GtkAction *action, gpointer userdata)
{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)userdata;

  deploy_set_state (sp, DEPLOY_STATE_INITIAL);
}

static gint
remote_debug_dialog (AnjutaPluginSdk *sp)
{
  GtkWidget *dialog;
  GtkWidget *inner_vbox;
  GtkWidget *label;
  GtkWidget *entry;
  GtkWidget *hbox;
  GtkWidget *chooser;
  GtkSizeGroup *label_group;
  GtkSizeGroup *control_group;
  gint res;

  dialog = gtk_dialog_new_with_buttons (_("Debug remotely"),
      NULL,
      0,
      GTK_STOCK_CANCEL,
      GTK_RESPONSE_REJECT,
      GTK_STOCK_EXECUTE,
      GTK_RESPONSE_ACCEPT,
      NULL);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);

  label_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
  control_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

  inner_vbox = gtk_vbox_new (FALSE, 6);
  hbox = gtk_hbox_new (FALSE, 6);

  label = gtk_label_new (_("Local executable:"));
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
  gtk_size_group_add_widget (label_group, label);
  gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 2);

  chooser = gtk_file_chooser_button_new (_("Select the local executable"), 
      GTK_FILE_CHOOSER_ACTION_OPEN);
  gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (chooser), sp->gdb_local_path);

  /* Don't grab keyboard focus on click */
  gtk_file_chooser_button_set_focus_on_click (GTK_FILE_CHOOSER_BUTTON (chooser),
      FALSE);

  gtk_size_group_add_widget (control_group, chooser);
  gtk_box_pack_start (GTK_BOX (hbox), chooser, TRUE, TRUE, 2);

  gtk_box_pack_start (GTK_BOX (inner_vbox), hbox, TRUE, TRUE, 2);

  hbox = gtk_hbox_new (FALSE, 6);

  label = gtk_label_new (_("Remote command:"));
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
  gtk_size_group_add_widget (label_group, label);
  gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 2);

  entry = gtk_entry_new ();

  /* Make hitting enter in the entry do the window default action */
  gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);

  gtk_entry_set_text (GTK_ENTRY (entry), sp->gdb_remote_command);
  gtk_size_group_add_widget (control_group, entry);
  gtk_box_pack_start (GTK_BOX (hbox), entry, TRUE, TRUE, 2);

  gtk_box_pack_start (GTK_BOX (inner_vbox), hbox, TRUE, TRUE, 2);

  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), inner_vbox, 
      TRUE, TRUE, 2);

  /* 
   * Grab the focus away explicitly, otherwise it goes onto the file chooser
   * button and then we don't get a working default behaviour for the dialog
   */
  gtk_widget_grab_focus (entry);

  gtk_widget_show_all (inner_vbox);
  gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);

  res = gtk_dialog_run (GTK_DIALOG (dialog));

  switch (res)
  {
    case GTK_RESPONSE_ACCEPT:
      g_free (sp->gdb_remote_command);
      g_free (sp->gdb_local_path);
      sp->gdb_remote_command = g_strdup (gtk_entry_get_text (GTK_ENTRY (entry)));
      sp->gdb_local_path = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (chooser));
      break;
    default:
      break;
  }

  gtk_widget_destroy (dialog);
  return res;
}

static gchar *
remote_run_dialog (AnjutaPluginSdk *sp, gchar *prev_cmd)
{
  GtkWidget *dialog;
  GtkWidget *inner_vbox;
  GtkWidget *label;
  GtkWidget *hbox;
  GtkWidget *entry;
  gchar *ret = NULL;
  gint res;

  dialog = gtk_dialog_new_with_buttons (_("Run remotely"),
      NULL,
      0,
      GTK_STOCK_CANCEL,
      GTK_RESPONSE_REJECT,
      GTK_STOCK_EXECUTE,
      GTK_RESPONSE_ACCEPT,
      NULL);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);

  inner_vbox = gtk_vbox_new (FALSE, 6);
  hbox = gtk_hbox_new (FALSE, 6);

  label = gtk_label_new (_("Command to run: "));
  gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 2);

  entry = gtk_entry_new ();

  /* Make hitting enter in the entry do the window default action */
  gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);

  if (prev_cmd)
    gtk_entry_set_text (GTK_ENTRY (entry), prev_cmd);

  gtk_box_pack_start (GTK_BOX (hbox), entry, TRUE, TRUE, 2);

  gtk_box_pack_start (GTK_BOX (inner_vbox), hbox, TRUE, TRUE, 2);

  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), inner_vbox, 
      TRUE, TRUE, 2);

  gtk_widget_show_all (inner_vbox);
  gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);

  res = gtk_dialog_run (GTK_DIALOG (dialog));

  switch (res)
  {
    case GTK_RESPONSE_ACCEPT:
      if (strlen (gtk_entry_get_text (GTK_ENTRY (entry))) > 0)
        ret = g_strdup (gtk_entry_get_text (GTK_ENTRY (entry)));
      break;
    default:
      break;
  }

  gtk_widget_destroy (dialog);
  return ret;
}

static void
do_local_gdb (AnjutaPluginSdk *sp)
{
  gchar *script_name = NULL;
  gint fd = 0;
  gchar *script_contents = NULL;
  GIOChannel *channel = NULL;
  GIOStatus status;
  IAnjutaTerminal *terminal = NULL;
  gchar *cmd = NULL;
  GError *error = NULL;
  gchar *cur_dir = NULL;
  gchar *gdb_prefix = NULL;
  gchar *debug_file_dir = NULL;
  gboolean has_sysroot_dir;
  /* 
   * create a temporary files and write the client side gdb commands to it,
   * yes this is evil, no, i don't have a better solution right now
   */
  fd = g_file_open_tmp (NULL, &script_name, &error);

  if (fd == -1)
  {
    g_warning ("Error when opening temporary script file: %s", error->message);
    return;
  }

  has_sysroot_dir = lookup_sysroot_dir(sp);
  if (has_sysroot_dir)
  {
    gdb_prefix = g_build_filename (sp->sdk_root, sp->triplet, "sys-root", NULL);
    debug_file_dir = g_build_filename (sp->sdk_root, sp->triplet, "sys-root", 
            "usr", "lib", "debug", NULL);
  } else {
    gdb_prefix = g_build_filename (sp->sdk_root, sp->triplet, NULL);
    debug_file_dir = g_build_filename (sp->sdk_root, sp->triplet, 
            "usr", "lib", "debug", NULL);
  }
  script_contents = g_strdup_printf (GDB_SCRIPT, gdb_prefix, debug_file_dir,
      beaver_target_get_ip_address (sp->target));

  channel = g_io_channel_unix_new (fd);

  status = g_io_channel_write_chars (channel, script_contents, -1, NULL, &error);

  if (status != G_IO_STATUS_NORMAL)
  {
    g_warning ("Error writing script content: %s", error->message);
    g_clear_error (&error);
    return;
  }
  
  if (g_io_channel_shutdown (channel, TRUE, &error) != G_IO_STATUS_NORMAL)
  {
    g_warning ("Errow whilst shutting down channel: %s", error->message);
    g_clear_error (&error);
    return;
  }

  g_io_channel_unref (channel);
  g_free (script_contents);
  g_free (gdb_prefix);

  terminal = anjuta_shell_get_interface (ANJUTA_PLUGIN (sp)->shell, 
      IAnjutaTerminal, &error);

  if (terminal == NULL)
  {
    g_warning ("Error getting terminal interface from shell: %s", 
        error->message);
    g_clear_error (&error);
    return;
  }

  cmd = g_strdup_printf (LOCAL_GDB_COMMAND, sp->triplet, 
      script_name, sp->gdb_local_path);
  cur_dir = g_get_current_dir ();
#ifdef ANJUTA_2_23_OR_HIGHER
  ianjuta_terminal_execute_command (terminal, cur_dir, cmd, NULL, &error);
#else
  ianjuta_terminal_execute_command (terminal, cur_dir, cmd, NULL, &error);
#endif
  if (error != NULL)
  {
    g_warning ("Error whilst launching local gdb command: %s", error->message);
    g_clear_error (&error);
  }

  g_free (script_name);
  g_free (cmd);
  g_free (cur_dir);
}

static void
do_remote_gdb (AnjutaPluginSdk *sp)
{
  IAnjutaMessageManager *msg_manager = NULL;
  GError *error = NULL;

  /* start the remote gdbserver */
  if (!beaver_target_remote_debug (sp->target, sp->gdb_remote_command, NULL, NULL))
  {
    g_warning ("Error starting remote gdbserver");
  }
}

static void
action_remote_debug_activate_cb (GtkAction *action,
    gpointer userdata)

{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)userdata;
  GError *error = NULL;
  gchar *cur_dir = NULL;
  gchar *cmd = NULL;

  gint res = 0;

  cur_dir = g_get_current_dir ();
  res = remote_debug_dialog (sp);

  switch (res)
  {
    case GTK_RESPONSE_ACCEPT:
      do_remote_gdb (sp);
      gtk_action_set_sensitive (sp->remote_debug_action, FALSE);
      break;
    default:
      break;
  }

  g_free (cmd);
  g_free (cur_dir);
}

static void
action_remote_run_activate_cb (GtkAction *action,
    gpointer userdata)
{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)userdata;
  GError *error = NULL;
  gchar *cmd = NULL;

  cmd = remote_run_dialog (sp, sp->remote_command);

  if (cmd)
  {
    if (!beaver_target_run_remote (sp->target, cmd, &error))
    {
      g_warning ("Error running remote command: %s", error->message);
      g_clear_error (&error);
      g_free (cmd);
    } else {
      g_free (sp->remote_command);
      sp->remote_command = cmd;
    }
  } else {
    g_warning ("No command to run given");
  }
}

static void
action_remote_stop_activate_cb (GtkAction *action,
    gpointer userdata)
{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)userdata;
  beaver_target_remote_stop (sp->target);
}

static void
action_remote_profile_activate_cb (GtkAction *action, gpointer userdata)
{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)userdata;
  gchar *cmd = NULL;
  gchar *search_path = NULL;

  if (!sp->oprofileui_launcher)
  {
    sp->oprofileui_launcher = anjuta_launcher_new ();
    g_signal_connect (sp->oprofileui_launcher, "child-exited", 
        (GCallback)oprofileui_launcher_child_exited_cb, sp);
  }

  search_path = g_build_filename (sp->sdk_root, sp->triplet, NULL);
  cmd = g_strdup_printf (OPROFILEUI_COMMAND, 
      beaver_target_get_ip_address (sp->target),
      search_path);

  if (anjuta_launcher_execute (sp->oprofileui_launcher, cmd, NULL, NULL))
  {
    gtk_action_set_sensitive (sp->remote_profile_action, FALSE);
  } else {
    g_warning ("Error launching OProfileUI");
  }

  g_free (cmd);
  g_free (search_path);
}

/* Callback for when qemu launcher finished */


static void
remote_launcher_child_exited_cb (AnjutaLauncher *launcher, gint child_pid,
    gint status, gulong time, gpointer userdata)
{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)userdata;

}

static void
remote_gdb_launcher_child_exited_cb (AnjutaLauncher *launcher, gint child_pid,
    gint status, gulong time, gpointer userdata)
{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)userdata;

  if (sp->triplet && sp->sdk_root && 
      beaver_target_get_state (sp->target) == TARGET_STATE_READY)
  {
    gtk_action_set_sensitive (sp->remote_debug_action, TRUE);
  }

  if (sp->remote_gdb_timeout)
  {
    g_source_remove (sp->remote_gdb_timeout);
    sp->remote_gdb_timeout = 0;
  }
}

static void
oprofileui_launcher_child_exited_cb (AnjutaLauncher *launcher, gint child_pid,
    gint status, gulong time, gpointer userdata)
{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)userdata;

  if (beaver_target_get_state (sp->target) == TARGET_STATE_READY)
  {
    if (anjuta_util_prog_is_installed ("oprofile-viewer", FALSE))
    {
      gtk_action_set_sensitive (sp->remote_profile_action, TRUE);
    }
  }
}

/* 
 * Callback that gets fired when data gets flushed in the view because it's
 * the end of line
 */
static void
message_view_buffer_flushed_cb (IAnjutaMessageView *view, gchar *data, 
    gpointer userdata)
{
  /* Append to the message view */
  ianjuta_message_view_append (view, IANJUTA_MESSAGE_VIEW_TYPE_NORMAL,
    data, "", NULL);
}

static gchar *
get_host_component ()
{
  struct utsname res;
  gchar *os = NULL;
  gchar *host_component = NULL;

  uname (&res);
  os = g_ascii_strdown (res.sysname, -1);
  host_component = g_strdup_printf("%s-%s", res.machine, os);

  g_free (os);

  return host_component;
}

/* Update the path to remove or include our sdk bin directory */
static void
update_path (AnjutaPluginSdk *sp)
{
  char *path = NULL;
  gchar *new_path_component = NULL;
  GArray *path_array = NULL;
  char **pathv = NULL;
  int i = 0;
  gchar *poky_scripts_dir = NULL;
  gchar *poky_host_staging_bin_dir = NULL;
  gchar *poky_host_staging_usr_bin_dir = NULL;
  gchar *poky_cross_dir = NULL;
  gchar *host_component = NULL;

  /* Create new versions of path bits */
  if (sp->poky_mode == POKY_MODE_TOOLCHAIN)
  {
    if (sp->triplet != NULL && sp->sdk_root != NULL)
      new_path_component = g_build_filename (sp->sdk_root, "bin", NULL);
  } else {
    if (sp->poky_root)
    {
      host_component = get_host_component ();
      poky_scripts_dir = g_build_filename (sp->poky_root, "scripts", NULL);
      poky_host_staging_usr_bin_dir = g_build_filename (sp->poky_root, "build", "tmp", 
          "staging", host_component, "usr", "bin", NULL);
      poky_host_staging_bin_dir = g_build_filename (sp->poky_root, "build", "tmp", 
          "staging", host_component, "bin", NULL);
      poky_cross_dir = g_build_filename (sp->poky_root, "build", "tmp", 
          "cross", "bin", NULL);
      g_free (host_component);
    }
  }

  /* get current path. do not free */
  path = getenv ("PATH");

  /* split old path up */
  pathv = g_strsplit (path, ":", -1);

  /* Convert it into a GArray */
  path_array = g_array_sized_new (TRUE, FALSE, sizeof (gchar *),
      g_strv_length (pathv));
  path_array = g_array_insert_vals (path_array, 0, pathv, 
      g_strv_length (pathv));

  /* Remove old versions */
  for (i = 0; i < path_array->len; i++)
  {
    gchar *tmp = g_array_index (path_array, gchar *, i);

    if ((sp->path_component && g_str_equal (tmp, sp->path_component)) ||
      (sp->poky_scripts_dir && g_str_equal (tmp, sp->poky_scripts_dir)) ||
      (sp->poky_host_staging_bin_dir 
        && g_str_equal (tmp, sp->poky_host_staging_bin_dir)) ||
      (sp->poky_host_staging_usr_bin_dir 
        && g_str_equal (tmp, sp->poky_host_staging_usr_bin_dir)) ||
      (sp->poky_cross_dir && g_str_equal (tmp, sp->poky_cross_dir)))
    {
      path_array = g_array_remove_index (path_array, i);
      i--; /* because we've deleted something */
    }
  }

  if (sp->poky_mode == POKY_MODE_TOOLCHAIN)
  {
    /* Add the new path component */
    if (new_path_component)
      path_array = g_array_prepend_val (path_array, new_path_component);
  } else {
    if (poky_scripts_dir)
      path_array = g_array_prepend_val (path_array, poky_scripts_dir);

    if (poky_cross_dir)
      path_array = g_array_prepend_val (path_array, poky_cross_dir);

    if (poky_host_staging_bin_dir)
      path_array = g_array_prepend_val (path_array, poky_host_staging_bin_dir);

    if (poky_host_staging_usr_bin_dir)
      path_array = g_array_prepend_val (path_array, poky_host_staging_usr_bin_dir);
  }

  /* Create our new path */
  path = g_strjoinv (":", (gchar **)path_array->data);
  setenv ("PATH", path, 1);

  /* Save the components */
  g_free (sp->path_component);
  g_free (sp->poky_scripts_dir);
  g_free (sp->poky_cross_dir);
  g_free (sp->poky_host_staging_bin_dir);
  g_free (sp->poky_host_staging_usr_bin_dir);
  
  sp->path_component = new_path_component;
  sp->poky_scripts_dir = poky_scripts_dir;
  sp->poky_cross_dir = poky_cross_dir;
  sp->poky_host_staging_bin_dir = poky_host_staging_bin_dir;
  sp->poky_host_staging_usr_bin_dir = poky_host_staging_usr_bin_dir;

  g_array_free (path_array, TRUE);
  g_strfreev (pathv);
}

/* 
 * Add/remove/update the environment to reflect changes to the sdk root or
 * triplet
 */
static void
cleanup_environment (AnjutaPluginSdk *sp)
{
  update_path (sp);
  
  /* unset environment keys */
  unsetenv ("PKG_CONFIG_SYSROOT_DIR");
  unsetenv ("PKG_CONFIG_PATH");
  unsetenv ("CONFIG_SITE");
}

static void
update_environment (AnjutaPluginSdk *sp)
{
  gchar *tmp = NULL;
  gchar *pkg_config_usr_path = NULL;
  gchar *pkg_config_path = NULL;


  if (sp->triplet == NULL ||
      (sp->poky_mode == POKY_MODE_TOOLCHAIN && sp->sdk_root == NULL) ||
      (sp->poky_mode == POKY_MODE_FULL && sp->poky_root == NULL))
  {
    cleanup_environment (sp);
    return;
  }

  update_path (sp);

  if (sp->poky_mode == POKY_MODE_TOOLCHAIN)
  {
    gboolean has_sysroot_dir = lookup_sysroot_dir(sp);

    if (has_sysroot_dir)
    {
      tmp = g_build_filename (sp->sdk_root, sp->triplet, "sys-root", NULL);
    } else {
      tmp = g_build_filename (sp->sdk_root, sp->triplet, NULL);
    }
    setenv ("PKG_CONFIG_SYSROOT_DIR", tmp, 1);
    g_free (tmp);

    if (has_sysroot_dir)
    {
      gchar *pkg_config_sysroot_lib_path = g_build_filename (sp->sdk_root, 
              sp->triplet, "sys-root", "usr", "lib", "pkgconfig", NULL);
      gchar *pkg_config_sysroot_share_path = g_build_filename (sp->sdk_root, 
              sp->triplet, "sys-root", "usr", "share", "pkgconfig", NULL);
      tmp = g_strdup_printf ("%s:%s", pkg_config_sysroot_lib_path, 
              pkg_config_sysroot_share_path);
      setenv ("PKG_CONFIG_PATH", tmp, 1);
      g_free (pkg_config_sysroot_lib_path);
      g_free (pkg_config_sysroot_share_path);
      g_free (tmp);
    } else {
      pkg_config_path = g_build_filename (sp->sdk_root, sp->triplet, "lib", 
          "pkgconfig", NULL);
      pkg_config_usr_path = g_build_filename (sp->sdk_root, sp->triplet, "usr",
              "lib", "pkgconfig", NULL);
      tmp = g_strdup_printf ("%s:%s", pkg_config_usr_path, pkg_config_path);
      setenv ("PKG_CONFIG_PATH", tmp, 1);
      g_free (pkg_config_path);
      g_free (pkg_config_usr_path);
      g_free (tmp);
    }

    tmp = g_build_filename (sp->sdk_root, "site-config", NULL);
    setenv ("CONFIG_SITE", tmp, 1);
    g_free (tmp);
  } else {
    tmp = g_build_filename (sp->poky_root, "build", "tmp", "staging",
        sp->triplet, NULL);
    setenv ("PKG_CONFIG_SYSROOT_DIR", tmp, 1);
    g_free (tmp);

    pkg_config_usr_path = g_build_filename (sp->poky_root, "build", "tmp", 
        "staging", sp->triplet, "usr", "lib", "pkgconfig", NULL);
    pkg_config_path = g_build_filename (sp->poky_root, "build", "tmp", 
        "staging", sp->triplet, "lib", "pkgconfig", NULL);
    tmp = g_strdup_printf ("%s:%s", pkg_config_usr_path, pkg_config_path);
    setenv ("PKG_CONFIG_PATH", tmp, 1);
    g_free (pkg_config_path);
    g_free (pkg_config_usr_path);
    g_free (tmp);

    unsetenv ("CONFIG_SITE");
  }
}

static void
update_state (AnjutaPluginSdk *sp)
{
  BeaverTargetState state;

  state = beaver_target_get_state (sp->target);

  if (BEAVER_IS_TARGET_QEMU (sp->target))
  {
    switch (state)
    {
      case TARGET_STATE_UNKNOWN:
        gtk_action_set_sensitive (sp->qemu_start_action, FALSE);
        break;
      case TARGET_STATE_STOPPED:
        gtk_action_set_sensitive (sp->qemu_start_action, TRUE);
        break;
      case TARGET_STATE_READY:
        gtk_action_set_sensitive (sp->qemu_start_action, FALSE);
        gtk_action_set_sensitive (sp->qemu_shutdown_action, TRUE);
        break;
      case TARGET_STATE_BUSY:
        gtk_action_set_sensitive (sp->qemu_shutdown_action, FALSE);
      default:
        break;
    }
  } else {
     gtk_action_set_sensitive (sp->qemu_start_action, FALSE);
     gtk_action_set_sensitive (sp->qemu_shutdown_action, FALSE);
  }

  switch (state)
  {
    case TARGET_STATE_REMOTE_RUNNING:
      gtk_action_set_sensitive (sp->deploy_action, FALSE);
      gtk_action_set_sensitive (sp->remote_profile_action, FALSE);
      gtk_action_set_sensitive (sp->qemu_shutdown_action, FALSE);
      gtk_action_set_sensitive (sp->remote_run_action, FALSE);
      gtk_action_set_sensitive (sp->remote_debug_action, FALSE);
      gtk_action_set_sensitive (sp->remote_run_action, FALSE);
      gtk_action_set_sensitive (sp->remote_stop_action, TRUE);
      break;
    case TARGET_STATE_UNKNOWN:
    case TARGET_STATE_STOPPED:
    case TARGET_STATE_BUSY:
      gtk_action_set_sensitive (sp->deploy_action, FALSE);
      gtk_action_set_sensitive (sp->remote_profile_action, FALSE);
      gtk_action_set_sensitive (sp->qemu_shutdown_action, FALSE);
      gtk_action_set_sensitive (sp->remote_run_action, FALSE);
      gtk_action_set_sensitive (sp->remote_debug_action, FALSE);
      gtk_action_set_sensitive (sp->remote_run_action, FALSE);
      gtk_action_set_sensitive (sp->remote_stop_action, FALSE);
      break;
    case TARGET_STATE_READY:
      /* Can only turn on if have a project */
      if (sp->project_root_uri)
        gtk_action_set_sensitive (sp->deploy_action, TRUE);

      if (anjuta_util_prog_is_installed ("oprofile-viewer", FALSE))
        gtk_action_set_sensitive (sp->remote_profile_action, TRUE);

      gtk_action_set_sensitive (sp->remote_run_action, TRUE);
      gtk_action_set_sensitive (sp->remote_debug_action, TRUE);

      gtk_action_set_sensitive (sp->remote_debug_stop_action, FALSE);
      gtk_action_set_sensitive (sp->remote_stop_action, FALSE);
      break;
    case TARGET_STATE_DEBUGGER_READY:
      do_local_gdb (sp);
      gtk_action_set_sensitive (sp->remote_debug_stop_action, TRUE);
      break;
  }
}


static void
setup_target (AnjutaPluginSdk *sp)
{
  switch (sp->target_mode)
  {
    case TARGET_MODE_QEMU:
      {
        gchar *kernel = NULL;
        gchar *rootfs = NULL;

        if (sp->target &&
            BEAVER_IS_TARGET_QEMU (sp->target))
          return;

        if (sp->target)
          g_object_unref (sp->target);

        sp->target = beaver_target_qemu_new (ANJUTA_PLUGIN (sp)->shell);

        kernel = anjuta_preferences_get (sp->prefs, PREFS_PROP_KERNEL);
        rootfs = anjuta_preferences_get (sp->prefs, PREFS_PROP_ROOTFS);
        g_object_set (sp->target, "kernel", kernel, "rootfs", rootfs, NULL);
        g_free (kernel);
        g_free (rootfs);
        break;
      }
    case TARGET_MODE_DEVICE:
      {
        gchar *ip_address = NULL;
        if (sp->target &&
            BEAVER_IS_TARGET_DEVICE (sp->target))
          return;

        if (sp->target)
          g_object_unref (sp->target);

        sp->target = beaver_target_device_new (ANJUTA_PLUGIN (sp)->shell);

        ip_address = anjuta_preferences_get (sp->prefs, PREFS_PROP_TARGET_IP);
        g_object_set (sp->target, "ip-address", ip_address, NULL);
        g_free (ip_address);

        break;
      }
  }

  g_signal_connect (sp->target, "state-changed", 
      (GCallback)target_state_changed_cb, sp);

  update_state (sp);
}


static void
setup_buildable (AnjutaPluginSdk *sp)
{
  gchar *command = NULL;
  GError *error = NULL;

  if (!sp->buildable)
  {
    sp->buildable = anjuta_shell_get_interface (ANJUTA_PLUGIN (sp)->shell, IAnjutaBuildable, 
        &error);

    if (!sp->buildable)
    {
      g_warning ("Error whilst getting buildable interface: %s", error->message);
      g_clear_error (&error);
      return;
    }
  }

  /* For the configure option in the menu */
  command = g_strdup_printf (CONFIGURE_COMMAND, sp->triplet);

  ianjuta_buildable_set_command (sp->buildable, 
      IANJUTA_BUILDABLE_COMMAND_CONFIGURE, command, &error);

  if (error)
  {
    g_warning ("Error setting configure command: %s", error->message);
    g_clear_error (&error);
  }

  g_free (command);

  /* For the generate option in the menu */
  command = g_strdup_printf (AUTOGEN_COMMAND, sp->triplet);

  ianjuta_buildable_set_command (sp->buildable, 
      IANJUTA_BUILDABLE_COMMAND_GENERATE, command, &error);

  if (error)
  {
    g_warning ("Error setting autogen command: %s", error->message);
    g_clear_error (&error);
  }

  g_free (command);
}


#ifdef ANJUTA_2_28_OR_HIGHER
/* Callbacks fired when preferences changed */
static void
sdk_root_preference_notify_cb (AnjutaPreferences *pref, const gchar *key, 
    const gchar *value, gpointer userdata)
{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)userdata;

  g_free (sp->sdk_root);
  sp->sdk_root = anjuta_preferences_get (sp->prefs, PREFS_PROP_SDK_ROOT);

  update_environment (sp);
}

static void
triplet_preference_notify_cb (AnjutaPreferences *pref, const gchar *key, 
    const gchar *value, gpointer userdata)
{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)userdata;

  g_free (sp->triplet);
  sp->triplet = anjuta_preferences_get (sp->prefs, PREFS_PROP_TRIPLET);

  update_environment (sp);
  setup_buildable (sp);
}

static void
rootfs_preference_notify_cb (AnjutaPreferences *pref, const gchar *key, 
    const gchar *value, gpointer userdata)
{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)userdata;
  gchar *rootfs = NULL;

  if (BEAVER_IS_TARGET_QEMU (sp->target))
  {
    rootfs = anjuta_preferences_get (sp->prefs, PREFS_PROP_ROOTFS);
    g_object_set (sp->target, "rootfs", rootfs, NULL);
    g_free (rootfs);
  }
}

static void
kernel_preference_notify_cb (AnjutaPreferences *pref, const gchar *key, 
    const gchar *value, gpointer userdata)
{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)userdata;
  gchar *kernel = NULL;

  if (BEAVER_IS_TARGET_QEMU (sp->target))
  {
    kernel = anjuta_preferences_get (sp->prefs, PREFS_PROP_KERNEL);
    g_object_set (sp->target, "kernel", kernel, NULL);
    g_free (kernel);
  }
}

static void
poky_mode_preference_notify_cb (AnjutaPreferences *pref, const gchar *key, 
    const gboolean value, gpointer userdata)
{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)userdata;

  sp->poky_mode = anjuta_preferences_get_bool (sp->prefs, PREFS_PROP_POKY_MODE);

  update_environment (sp);
}

static void
poky_root_preference_notify_cb (AnjutaPreferences *pref, const gchar *key, 
    const gchar *value, gpointer userdata)
{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)userdata;

  g_free (sp->poky_root);
  sp->poky_root = anjuta_preferences_get (sp->prefs, PREFS_PROP_POKY_ROOT);

  update_environment (sp);
}

static void
target_mode_preference_notify_cb (AnjutaPreferences *pref, const gchar *key, 
    const gboolean value, gpointer userdata)
{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)userdata;
  
  sp->target_mode = anjuta_preferences_get_bool (sp->prefs, PREFS_PROP_TARGET_MODE);

  setup_target (sp);
}

static void
target_ip_preference_notify_cb (AnjutaPreferences *pref, const gchar *key, 
    const gchar *value, gpointer userdata)
{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)userdata;
  gchar *ip_address = NULL;

  if (sp->target &&
      BEAVER_IS_TARGET_DEVICE (sp->target))
  {
    ip_address = anjuta_preferences_get (sp->prefs, PREFS_PROP_TARGET_IP);
    g_object_set (sp->target, "ip-address", ip_address, NULL);
    g_free (ip_address);
  }
}
#else
/* Callbacks fired when preferences changed */
static void
sdk_root_preference_notify_cb (GConfClient *client, guint cnxn_id, 
    GConfEntry *entry, gpointer userdata)
{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)userdata;

  g_free (sp->sdk_root);
  sp->sdk_root = anjuta_preferences_get (sp->prefs, PREFS_PROP_SDK_ROOT);

  update_environment (sp);
}

static void
triplet_preference_notify_cb (GConfClient *client, guint cnxn_id, 
    GConfEntry *entry, gpointer userdata)
{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)userdata;

  g_free (sp->triplet);
  sp->triplet = anjuta_preferences_get (sp->prefs, PREFS_PROP_TRIPLET);

  update_environment (sp);
  setup_buildable (sp);
}

static void
rootfs_preference_notify_cb (GConfClient *client, guint cnxn_id, 
    GConfEntry *entry, gpointer userdata)
{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)userdata;
  gchar *rootfs = NULL;

  if (BEAVER_IS_TARGET_QEMU (sp->target))
  {
    rootfs = anjuta_preferences_get (sp->prefs, PREFS_PROP_ROOTFS);
    g_object_set (sp->target, "rootfs", rootfs, NULL);
    g_free (rootfs);
  }
}

static void
kernel_preference_notify_cb (GConfClient *client, guint cnxn_id, 
    GConfEntry *entry, gpointer userdata)
{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)userdata;
  gchar *kernel = NULL;

  if (BEAVER_IS_TARGET_QEMU (sp->target))
  {
    kernel = anjuta_preferences_get (sp->prefs, PREFS_PROP_KERNEL);
    g_object_set (sp->target, "kernel", kernel, NULL);
    g_free (kernel);
  }
}

static void
poky_mode_preference_notify_cb (GConfClient *client, guint cnxn_id,
    GConfEntry *entry, gpointer userdata)
{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)userdata;

  sp->poky_mode = anjuta_preferences_get_int (sp->prefs, PREFS_PROP_POKY_MODE);

  update_environment (sp);
}

static void
poky_root_preference_notify_cb (GConfClient *client, guint cnxn_id,
    GConfEntry *entry, gpointer userdata)
{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)userdata;

  g_free (sp->poky_root);
  sp->poky_root = anjuta_preferences_get (sp->prefs, PREFS_PROP_POKY_ROOT);

  update_environment (sp);
}

static void
target_mode_preference_notify_cb (GConfClient *client, guint cnxn_id,
    GConfEntry *entry, gpointer userdata)
{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)userdata;
  
  sp->target_mode = anjuta_preferences_get_int (sp->prefs, PREFS_PROP_TARGET_MODE);

  setup_target (sp);
}

static void
target_ip_preference_notify_cb (GConfClient *client, guint cnxn_id,
    GConfEntry *entry, gpointer userdata)
{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)userdata;
  gchar *ip_address = NULL;

  if (sp->target &&
      BEAVER_IS_TARGET_DEVICE (sp->target))
  {
    ip_address = anjuta_preferences_get (sp->prefs, PREFS_PROP_TARGET_IP);
    g_object_set (sp->target, "ip-address", ip_address, NULL);
    g_free (ip_address);
  }
}
#endif

/* 
 * Callbacks for when a value for "project_root_uri" is added to the shell aka
 * when a project is opened
 */

static void
project_root_uri_value_added (AnjutaPlugin *plugin, const gchar *name, 
    const GValue *value, gpointer userdata)
{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)plugin;

  g_free (sp->project_root_uri);
  sp->project_root_uri = g_value_dup_string (value);

  if (beaver_target_get_state (sp->target) == TARGET_STATE_READY)
  {
    gtk_action_set_sensitive (sp->deploy_action, TRUE);
  }
}

static void
project_root_uri_value_removed (AnjutaPlugin *plugin, const gchar *name,
    gpointer userdata)
{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)plugin;

  g_free (sp->project_root_uri);
  sp->project_root_uri = NULL;

  gtk_action_set_sensitive (sp->deploy_action, FALSE);
}

static void
shell_session_load_cb (AnjutaShell *shell, AnjutaSessionPhase phase, 
    AnjutaSession *session, gpointer userdata)
{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)userdata;

  sp->remote_command = anjuta_session_get_string (session, "SDK",
      "Remote command");
  sp->gdb_local_path = anjuta_session_get_string (session, "SDK",
      "Remote gdb local path");
  sp->gdb_remote_command = anjuta_session_get_string (session, "SDK",
      "Remote gdb remote command");
}

static void
shell_session_save_cb (AnjutaShell *shell, AnjutaSessionPhase phase, 
    AnjutaSession *session, gpointer userdata)
{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)userdata;

  anjuta_session_set_string (session, "SDK", "Remote command",
      sp->remote_command);
  anjuta_session_set_string (session, "SDK", "Remote gdb local path",
      sp->gdb_local_path);
  anjuta_session_set_string (session, "SDK", "Remote gdb remote command",
      sp->gdb_remote_command);
}

static void
target_state_changed_cb (BeaverTarget *target, gpointer userdata)
{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)userdata;
  update_state (sp);
}

static gboolean
anjuta_plugin_sdk_activate (AnjutaPlugin *plugin)
{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)plugin;
  AnjutaUI *ui;
  GError *error = NULL;
  gchar *kernel = NULL;
  gchar *rootfs = NULL;

  ui = anjuta_shell_get_ui (ANJUTA_PLUGIN (plugin)->shell, NULL);

  sp->action_group = anjuta_ui_add_action_group_entries (ui, "ActionGroupSdk",
      _("SDK Operations"),
      actions_sdk,
      G_N_ELEMENTS (actions_sdk),
      GETTEXT_PACKAGE,
      TRUE,
      sp);

  sp->uiid = anjuta_ui_merge (ui, UI_FILE);
  sp->prefs = anjuta_shell_get_preferences (ANJUTA_PLUGIN (sp)->shell, NULL);
  sp->prefs_icon = gdk_pixbuf_new_from_file (ICON_PATH, NULL);

  /* Get actions, we need them for turning them on/off */
  sp->qemu_start_action = anjuta_ui_get_action (ui, "ActionGroupSdk", 
      "ActionStartQemu");
  sp->qemu_shutdown_action = anjuta_ui_get_action (ui, "ActionGroupSdk", 
      "ActionShutdownQemu");
  sp->deploy_action = anjuta_ui_get_action (ui, "ActionGroupSdk", 
      "ActionDeploy");
  sp->remote_run_action = anjuta_ui_get_action (ui, "ActionGroupSdk",
      "ActionRunRemote");
  sp->remote_debug_action = anjuta_ui_get_action (ui, "ActionGroupSdk",
      "ActionDebugRemote");
  sp->remote_debug_stop_action = anjuta_ui_get_action (ui, "ActionGroupSdk",
      "ActionStopRemoteDebugger");
  sp->remote_profile_action = anjuta_ui_get_action (ui, "ActionGroupSdk",
      "ActionRemoteProfile");
  sp->remote_stop_action = anjuta_ui_get_action (ui, "ActionGroupSdk",
      "ActionStopRemote");

#ifndef GDB_INTEGRATION
  gtk_action_set_visible (sp->remote_debug_action, FALSE);
  gtk_action_set_visible (sp->remote_debug_stop_action, FALSE);
#endif

  gtk_action_set_sensitive (sp->qemu_start_action, FALSE);
  gtk_action_set_sensitive (sp->qemu_shutdown_action, FALSE);
  gtk_action_set_sensitive (sp->deploy_action, FALSE);
  gtk_action_set_sensitive (sp->remote_run_action, FALSE);
  gtk_action_set_sensitive (sp->remote_debug_action, FALSE);
  gtk_action_set_sensitive (sp->remote_debug_stop_action, FALSE);
  gtk_action_set_sensitive (sp->remote_profile_action, FALSE);
  gtk_action_set_sensitive (sp->remote_stop_action, FALSE);

#ifdef ANJUTA_2_28_OR_HIGHER
  sp->sdk_root_notifyid = anjuta_preferences_notify_add_string (sp->prefs,
      PREFS_PROP_SDK_ROOT, sdk_root_preference_notify_cb, sp, NULL);
  sp->triplet_notifyid = anjuta_preferences_notify_add_string (sp->prefs,
      PREFS_PROP_TRIPLET, triplet_preference_notify_cb, sp, NULL);
  sp->rootfs_notifyid = anjuta_preferences_notify_add_string (sp->prefs,
      PREFS_PROP_ROOTFS, rootfs_preference_notify_cb, sp, NULL);
  sp->kernel_notifyid = anjuta_preferences_notify_add_string (sp->prefs,
      PREFS_PROP_KERNEL, kernel_preference_notify_cb, sp, NULL);
  sp->poky_root_notifyid = anjuta_preferences_notify_add_string (sp->prefs,
      PREFS_PROP_POKY_ROOT, poky_root_preference_notify_cb, sp, NULL);
  sp->poky_mode_notifyid = anjuta_preferences_notify_add_bool (sp->prefs,
      PREFS_PROP_POKY_MODE, poky_mode_preference_notify_cb, sp, NULL);
  sp->target_mode_notifyid = anjuta_preferences_notify_add_bool (sp->prefs,
      PREFS_PROP_TARGET_MODE, target_mode_preference_notify_cb, sp, NULL);
  sp->target_ip_notifyid = anjuta_preferences_notify_add_string (sp->prefs,
      PREFS_PROP_TARGET_IP, target_ip_preference_notify_cb, sp, NULL);
#else
  sp->sdk_root_notifyid = anjuta_preferences_notify_add (sp->prefs,
      PREFS_PROP_SDK_ROOT, sdk_root_preference_notify_cb, sp, NULL);
  sp->triplet_notifyid = anjuta_preferences_notify_add (sp->prefs,
      PREFS_PROP_TRIPLET, triplet_preference_notify_cb, sp, NULL);
  sp->rootfs_notifyid = anjuta_preferences_notify_add (sp->prefs,
      PREFS_PROP_ROOTFS, rootfs_preference_notify_cb, sp, NULL);
  sp->kernel_notifyid = anjuta_preferences_notify_add (sp->prefs,
      PREFS_PROP_KERNEL, kernel_preference_notify_cb, sp, NULL);
  sp->poky_root_notifyid = anjuta_preferences_notify_add (sp->prefs,
      PREFS_PROP_POKY_ROOT, poky_root_preference_notify_cb, sp, NULL);
  sp->poky_mode_notifyid = anjuta_preferences_notify_add (sp->prefs,
      PREFS_PROP_POKY_MODE, poky_mode_preference_notify_cb, sp, NULL);
  sp->target_mode_notifyid = anjuta_preferences_notify_add (sp->prefs,
      PREFS_PROP_TARGET_MODE, target_mode_preference_notify_cb, sp, NULL);
  sp->target_ip_notifyid = anjuta_preferences_notify_add (sp->prefs,
      PREFS_PROP_TARGET_IP, target_ip_preference_notify_cb, sp, NULL);
#endif
  sp->sdk_root = anjuta_preferences_get (sp->prefs, PREFS_PROP_SDK_ROOT);
  sp->triplet = anjuta_preferences_get (sp->prefs, PREFS_PROP_TRIPLET);

  sp->poky_root = anjuta_preferences_get (sp->prefs, PREFS_PROP_POKY_ROOT);

#ifdef ANJUTA_2_28_OR_HIGHER
  sp->poky_mode = anjuta_preferences_get_bool (sp->prefs, PREFS_PROP_POKY_MODE);
  sp->target_mode = anjuta_preferences_get_bool (sp->prefs, PREFS_PROP_TARGET_MODE);
#else
  sp->poky_mode = anjuta_preferences_get_int (sp->prefs, PREFS_PROP_POKY_MODE);
  sp->target_mode = anjuta_preferences_get_int (sp->prefs, PREFS_PROP_TARGET_MODE);
#endif
  setup_target (sp);
  update_environment (sp);
  setup_buildable (sp);

  sp->project_root_uri_watch = anjuta_plugin_add_watch (plugin, 
      "project_root_uri", 
      (AnjutaPluginValueAdded) project_root_uri_value_added,
      (AnjutaPluginValueRemoved) project_root_uri_value_removed,
      sp);

  g_signal_connect (plugin->shell, "load-session",
      (GCallback)shell_session_load_cb, sp);
  g_signal_connect (plugin->shell, "save-session",
      (GCallback)shell_session_save_cb, sp);

  return TRUE;
}

static gboolean
anjuta_plugin_sdk_deactivate (AnjutaPlugin *plugin)
{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)plugin;
  AnjutaUI *ui = NULL;
  GError *error = NULL;
  IAnjutaMessageManager *msg_manager = NULL;

  cleanup_environment (sp);

  ui = anjuta_shell_get_ui (plugin->shell, NULL);
  anjuta_ui_remove_action_group (ui, sp->action_group);
  anjuta_ui_unmerge (ui, sp->uiid);

  if (sp->deploy_msg_view)
  {
    ianjuta_message_manager_remove_view (msg_manager, sp->deploy_msg_view, NULL);
    sp->deploy_msg_view = NULL;
  }

  if (sp->remote_msg_view)
  {
    ianjuta_message_manager_remove_view (msg_manager, sp->remote_msg_view, NULL);
    sp->remote_msg_view = NULL;
  }

  if (sp->buildable)
  {
    ianjuta_buildable_reset_commands (sp->buildable, &error);

    if (error)
    {
      g_warning ("Error whilst resetting buildable commands: %s", 
          error->message);
      g_clear_error (&error);
    }
  }

  anjuta_preferences_notify_remove (sp->prefs, sp->sdk_root_notifyid);
  anjuta_preferences_notify_remove (sp->prefs, sp->triplet_notifyid);
  anjuta_preferences_notify_remove (sp->prefs, sp->rootfs_notifyid);
  anjuta_preferences_notify_remove (sp->prefs, sp->kernel_notifyid);
  anjuta_preferences_notify_remove (sp->prefs, sp->poky_root_notifyid);
  anjuta_preferences_notify_remove (sp->prefs, sp->poky_mode_notifyid);
  anjuta_preferences_notify_remove (sp->prefs, sp->target_mode_notifyid);
  anjuta_preferences_notify_remove (sp->prefs, sp->target_ip_notifyid);

  anjuta_plugin_remove_watch (plugin, sp->project_root_uri_watch, FALSE);

  g_signal_handlers_disconnect_by_func (plugin->shell, shell_session_load_cb, sp);
  g_signal_handlers_disconnect_by_func (plugin->shell, shell_session_save_cb, sp);

  /* 
   * Do lots of things you'd normally expect to see in a dispose/finalize,
   * i.e. to free up memory but because a plugin won't get disposed/finalized
   * when being turned off we want to be nice and free up memory.
   */

  if (sp->deploy_launcher)
  {
    g_object_unref (sp->deploy_launcher);
    sp->deploy_launcher = NULL;
  }

  if (sp->remote_launcher)
  {
    g_object_unref (sp->remote_launcher);
    sp->remote_launcher = NULL;
  }

  if (sp->remote_gdb_launcher)
  {
    g_object_unref (sp->remote_gdb_launcher);
    sp->remote_gdb_launcher = NULL;
  }

  if (sp->oprofileui_launcher)
  {
    g_object_unref (sp->oprofileui_launcher);
    sp->oprofileui_launcher = NULL;
  }

  if (sp->prefs_icon)
  {
    g_object_unref (sp->prefs_icon);
    sp->prefs_icon = NULL;
  }

  if (sp->target)
  {
    g_object_unref (sp->target);
    sp->target = NULL;
  }

  /* 
   * Must set these to NULL because the plugin object might get reused after
   * deactivation.
   */

  g_free (sp->sdk_root);
  g_free (sp->triplet);
  g_free (sp->poky_root);

  g_free (sp->project_root_uri);
  g_free (sp->path_component);
  g_free (sp->gdb_local_path);
  g_free (sp->gdb_remote_command);
  g_free (sp->remote_command);
  
  sp->sdk_root = NULL;
  sp->triplet = NULL;
  sp->poky_root = NULL;

  sp->project_root_uri = NULL;
  sp->path_component = NULL;
  sp->gdb_local_path = NULL;
  sp->gdb_remote_command = NULL;
  sp->remote_command = NULL;

  return TRUE;
}

static void
anjuta_plugin_sdk_finalize (GObject *obj)
{
  if (G_OBJECT_CLASS (anjuta_plugin_sdk_parent_class)->finalize)
    G_OBJECT_CLASS (anjuta_plugin_sdk_parent_class)->finalize (obj);
}

static void
anjuta_plugin_sdk_dispose (GObject *obj)
{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)obj;

  if (G_OBJECT_CLASS (anjuta_plugin_sdk_parent_class)->dispose)
    G_OBJECT_CLASS (anjuta_plugin_sdk_parent_class)->dispose (obj);
}

static void
anjuta_plugin_sdk_instance_init (GObject *obj)
{
}

static void
anjuta_plugin_sdk_class_init (GObjectClass *klass) 
{
  AnjutaPluginClass *plugin_class = ANJUTA_PLUGIN_CLASS (klass);

  anjuta_plugin_sdk_parent_class = g_type_class_peek_parent (klass);

  plugin_class->activate = anjuta_plugin_sdk_activate;
  plugin_class->deactivate = anjuta_plugin_sdk_deactivate;
  klass->finalize = anjuta_plugin_sdk_finalize;
  klass->dispose = anjuta_plugin_sdk_dispose;
}

static void
ipreferences_merge (IAnjutaPreferences *ipref, AnjutaPreferences *prefs,
    GError **error)
{
  AnjutaPluginSdk *sp = (AnjutaPluginSdk *)ipref;
  GtkWidget *dialog = NULL;
  GtkWidget *page = NULL;

  dialog = anjuta_preferences_get_dialog (prefs);
  page = beaver_settings_page_new (ANJUTA_PLUGIN (sp)->shell);
  anjuta_preferences_dialog_add_page (ANJUTA_PREFERENCES_DIALOG (dialog),
      _("Moblin SDK"), _("Moblin SDK"),
      sp->prefs_icon,
      page);
}

static void
ipreferences_unmerge (IAnjutaPreferences *ipref, AnjutaPreferences *prefs,
    GError **error)
{
  GtkWidget *dialog = NULL;

  dialog = anjuta_preferences_get_dialog (prefs);

  /* remove page */
  anjuta_preferences_dialog_remove_page (ANJUTA_PREFERENCES_DIALOG (dialog),
      _("Moblin SDK"));
}

static void
ipreferences_iface_init (IAnjutaPreferencesIface* iface)
{
  iface->merge = ipreferences_merge;
  iface->unmerge = ipreferences_unmerge;	
}

ANJUTA_PLUGIN_BEGIN (AnjutaPluginSdk, anjuta_plugin_sdk);
ANJUTA_PLUGIN_ADD_INTERFACE(ipreferences, IANJUTA_TYPE_PREFERENCES);
ANJUTA_PLUGIN_END;


ANJUTA_SIMPLE_PLUGIN (AnjutaPluginSdk, anjuta_plugin_sdk);
