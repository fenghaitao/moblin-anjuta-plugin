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

#include "beaver-target.h"
#include "beaver-util.h"

G_DEFINE_TYPE (BeaverTarget, beaver_target, G_TYPE_OBJECT)

#define TARGET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), BEAVER_TYPE_TARGET, BeaverTargetPrivate))

#define ICON_FILE "anjuta-plugin-sdk.png"

#define LAUNCH_COMMAND "PATH=/usr/local/bin:/usr/local/sbin:$PATH DISPLAY=:0 dbus-launch"

typedef struct _BeaverTargetPrivate BeaverTargetPrivate;

struct _BeaverTargetPrivate
{
  AnjutaShell *shell;

  IAnjutaMessageView *msg_view;

  AnjutaLauncher *launcher;
  AnjutaLauncher *debug_launcher;

  gchar *debug_output; /* we need this for tracking if debugger is up and happy */
};

/* for signals */
enum {
  STATE_CHANGED,
  LAST_SIGNAL
};

enum
{
  PROP_0 = 0,
  PROP_SHELL
};

static guint signals[LAST_SIGNAL] = { 0 };

static const gchar *ssh_options[] = { 
  "ssh",
  "-t",
  "-l", "root", 
  "-o", "CheckHostIP no", 
  "-o", "StrictHostKeyChecking no", 
  "-o", "UserKnownHostsFile /dev/null",
  NULL };

static void launcher_child_exited_cb (AnjutaLauncher *launcher, gint child_pid,
    gint status, gulong time, gpointer userdata);
static void launcher_data_cb (AnjutaLauncher *launcher, 
    AnjutaLauncherOutputType type, const gchar *chars, gpointer userdata);
static void debug_launcher_data_cb (AnjutaLauncher *launcher, 
    AnjutaLauncherOutputType type, const gchar *chars, gpointer userdata);
static gboolean _beaver_target_run_remote_v (BeaverTarget *target, gchar **in_args, 
    GError **error);
static gboolean _beaver_target_run_remote (BeaverTarget *target, gchar *cmd, 
    GError **out_err);
static gboolean _beaver_target_remote_debug (BeaverTarget *target, gchar *cmd,
    gchar *cmd_args, GError **out_err);
static gboolean _beaver_target_remote_debug_stop (BeaverTarget *target, 
    GError **out_err);
static void _beaver_target_remote_stop (BeaverTarget *target);

static void
beaver_target_get_property (GObject *object, guint property_id,
                              GValue *value, GParamSpec *pspec)
{
  switch (property_id) {
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
beaver_target_set_property (GObject *object, guint property_id,
                              const GValue *value, GParamSpec *pspec)
{
  BeaverTargetPrivate *priv = TARGET_PRIVATE (object);

  switch (property_id) {
    case PROP_SHELL:
      priv->shell = g_value_get_object (value);

      /* When the shell is destroyed make the pointer to it null */
      g_object_add_weak_pointer (G_OBJECT (priv->shell), 
          (gpointer *)&priv->shell);

      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
beaver_target_dispose (GObject *object)
{
  BeaverTargetPrivate *priv = TARGET_PRIVATE (object);

  if (priv->launcher)
  {
    g_object_unref (priv->launcher);
    priv->launcher = NULL;
  }

  if (priv->debug_launcher)
  {
    g_object_unref (priv->debug_launcher);
    priv->debug_launcher = NULL;
  }

  if (G_OBJECT_CLASS (beaver_target_parent_class)->dispose)
    G_OBJECT_CLASS (beaver_target_parent_class)->dispose (object);
}

static void
beaver_target_finalize (GObject *object)
{
  BeaverTargetPrivate *priv = TARGET_PRIVATE (object);

  g_free (priv->debug_output);

  G_OBJECT_CLASS (beaver_target_parent_class)->finalize (object);
}

static void
beaver_target_class_init (BeaverTargetClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *pspec = NULL;

  g_type_class_add_private (klass, sizeof (BeaverTargetPrivate));

  object_class->get_property = beaver_target_get_property;
  object_class->set_property = beaver_target_set_property;
  object_class->dispose = beaver_target_dispose;
  object_class->finalize = beaver_target_finalize;

  klass->run_remote_v = _beaver_target_run_remote_v;
  klass->run_remote = _beaver_target_run_remote;
  klass->remote_debug = _beaver_target_remote_debug;
  klass->remote_debug_stop = _beaver_target_remote_debug_stop;
  klass->remote_stop = _beaver_target_remote_stop;

  signals[STATE_CHANGED] = g_signal_new ("state-changed",
      G_OBJECT_CLASS_TYPE (object_class),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (BeaverTargetClass, state_changed),
      NULL, NULL,
      g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE, 0);

  pspec = g_param_spec_object ("shell", "shell", "shell", 
      ANJUTA_TYPE_SHELL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE);
  g_object_class_install_property (object_class, PROP_SHELL, pspec);
}

static void
beaver_target_init (BeaverTarget *self)
{
  BeaverTargetPrivate *priv = TARGET_PRIVATE (self);

  priv->launcher = anjuta_launcher_new ();
  g_signal_connect (priv->launcher, "child-exited",
      (GCallback)launcher_child_exited_cb, self);

  priv->debug_launcher = anjuta_launcher_new ();
  g_signal_connect (priv->debug_launcher, "child-exited",
      (GCallback)launcher_child_exited_cb, self);
}

BeaverTargetState
beaver_target_get_state (BeaverTarget *target)
{
  return BEAVER_TARGET_GET_CLASS (target)->get_state (target);
}

void
beaver_target_set_state (BeaverTarget *target, BeaverTargetState state)
{
  BEAVER_TARGET_GET_CLASS (target)->set_state (target, state);
}

const gchar *
beaver_target_get_ip_address (BeaverTarget *target)
{
  return BEAVER_TARGET_GET_CLASS (target)->get_ip_address (target);
}

IAnjutaMessageManager *
beaver_target_get_message_manager (BeaverTarget *target)
{
  BeaverTargetPrivate *priv = TARGET_PRIVATE (target);
  IAnjutaMessageManager *msg_manager = NULL;
  GError *error = NULL;

  /* Get the message view manager */
  msg_manager = anjuta_shell_get_interface (priv->shell,
      IAnjutaMessageManager, &error);
  
  if (!msg_manager)
  {
    g_warning ("Error getting implementation of IAnjutaMessageManager: %s",
        error->message);
    g_clear_error (&error);
  }

  return msg_manager;
}

gboolean
beaver_target_run_remote_v (BeaverTarget *target, gchar **in_args,
    GError **error)
{
  return BEAVER_TARGET_GET_CLASS (target)->run_remote_v (target, in_args, error);
}

gboolean
beaver_target_run_remote (BeaverTarget *target, gchar *cmd, GError **error)
{
  return BEAVER_TARGET_GET_CLASS (target)->run_remote (target, cmd, error);
}

void
beaver_target_remote_stop (BeaverTarget *target)
{
  BEAVER_TARGET_GET_CLASS (target)->remote_stop (target);
}

gboolean
beaver_target_remote_debug (BeaverTarget *target, gchar *cmd, gchar *cmd_args, 
    GError **error)
{
  return BEAVER_TARGET_GET_CLASS (target)->remote_debug (target, cmd, cmd_args, error);
}

gboolean
beaver_target_remote_debug_stop (BeaverTarget *target, GError **error)
{
  return BEAVER_TARGET_GET_CLASS (target)->remote_debug_stop (target, error);
}

/* default implementations */
static gboolean
_beaver_target_run_remote (BeaverTarget *target, gchar *cmd, GError **out_err)
{
  GError *error = NULL;
  gchar *args[2] = {NULL, NULL};
  gchar *cmdline = NULL;
  gboolean res = FALSE;
  
  cmdline = g_strdup_printf (LAUNCH_COMMAND " " "%s", cmd);
  args[0] = cmdline;
  args[1] = NULL;

  res = beaver_target_run_remote_v (target, args, &error);

  g_free (cmdline);

  return res;
}

static gboolean
_beaver_target_run_remote_v (BeaverTarget *target, gchar **in_args, 
    GError **out_err)
{
  BeaverTargetPrivate *priv = TARGET_PRIVATE (target);
  gchar **args = NULL;
  gchar *ip_args[] = {(gchar *)beaver_target_get_ip_address (target), NULL};
  GError *error = NULL;
  IAnjutaMessageManager *msg_manager = NULL;

  args = beaver_util_strv_joinv ((gchar **)ssh_options, ip_args, in_args, NULL);

  if (!priv->msg_view)
  {
    /* Create a new view */
    msg_manager = beaver_target_get_message_manager (target);
    priv->msg_view = ianjuta_message_manager_add_view (msg_manager,
        _("Remote"), ICON_FILE, &error);

    if (!priv->msg_view)
    {
      g_warning ("Error getting view: %s", error->message);
      g_clear_error (&error);
    } else {
      g_signal_connect (priv->msg_view, "buffer-flushed", 
          (GCallback)beaver_util_message_view_buffer_flushed_cb, NULL);

      /* When the view is destroyed make the pointer to it null */
      g_object_add_weak_pointer (G_OBJECT (priv->msg_view), 
          (gpointer *)&priv->msg_view);

      ianjuta_message_manager_set_current_view (msg_manager, priv->msg_view,
          &error);

      if (error != NULL)
      {
        g_warning ("Error setting current message view: %s", error->message);
        g_clear_error (&error);
      }
    }
  }

#ifdef ANJUTA_2_28_OR_HIGHER
  if (anjuta_launcher_execute_v (priv->launcher,
                                 NULL,
                                 args,
                                 NULL,
                                 launcher_data_cb,
                                 target))
  {
    beaver_target_set_state (target, TARGET_STATE_REMOTE_RUNNING);
    return TRUE;
#else
#ifdef ANJUTA_2_23_TO_26
  if (anjuta_launcher_execute_v (priv->launcher,
                                 args,
                                 NULL,
                                 launcher_data_cb,
                                 target))
  {
    beaver_target_set_state (target, TARGET_STATE_REMOTE_RUNNING);
    return TRUE;
#else
  if (anjuta_launcher_execute_v (priv->launcher,
                                 args,
                                 launcher_data_cb,
                                 target))
  {
    beaver_target_set_state (target, TARGET_STATE_REMOTE_RUNNING);
    return TRUE;
#endif
#endif
  } else {
    gchar *debug_str = NULL;

    debug_str = g_strjoinv (" ", args);
    g_warning ("Error whilst launching command: %s", debug_str);
    g_free (debug_str);
    g_free (args);

    return FALSE;
  }
}

/* 
 * Used for starting remote gdb server on the target. cmd is the program name
 * and args are the args. We check to see if cmd is a full path and if not put
 * through backticks with which to find it.
 */
static gboolean
_beaver_target_remote_debug (BeaverTarget *target, gchar *cmd, gchar *cmd_args,
    GError **out_err)
{
  BeaverTargetPrivate *priv = TARGET_PRIVATE (target);

  IAnjutaMessageManager *msg_manager = NULL;
  GError *error = NULL;

  gchar **args = NULL;
  gchar *gdb_args[2] = {NULL, NULL};
  gchar *ip_args[] = {(gchar *)beaver_target_get_ip_address (target), NULL};
  gchar *real_cmd = NULL;

  /* clear out the old 'log' */
  g_free (priv->debug_output);
  priv->debug_output = NULL;

  /* Don't think this should ever happen since the text of the widget is "" */
  if (cmd_args == NULL)
    cmd_args = "";

  /* We need to this because gdbserver is pretty rubbish. */
  if (g_str_has_prefix (cmd, "/"))
  {
    real_cmd = g_strdup (cmd);
  } else {
    real_cmd = g_strdup_printf ("`which %s`", cmd);
  }

  gdb_args[0] = g_strdup_printf (LAUNCH_COMMAND " " "gdbserver 0.0.0.0:2345 %s %s", 
      real_cmd, cmd_args);
  g_free (real_cmd);

  gdb_args[1] = NULL;

  args = beaver_util_strv_joinv ((gchar **)ssh_options, ip_args, gdb_args, NULL);

  if (!priv->msg_view)
  {
    /* Create a new view */
    msg_manager = beaver_target_get_message_manager (target);
    priv->msg_view = ianjuta_message_manager_add_view (msg_manager,
        _("Remote"), ICON_FILE, &error);

    if (!priv->msg_view)
    {
      g_warning ("Error getting view: %s", error->message);
      g_propagate_error (out_err, error);
      return FALSE;
    } else {
      g_signal_connect (priv->msg_view, "buffer-flushed", 
          (GCallback)beaver_util_message_view_buffer_flushed_cb, NULL);

      /* When the view is destroyed make the pointer to it null */
      g_object_add_weak_pointer (G_OBJECT (priv->msg_view), 
          (gpointer *)&priv->msg_view);

      ianjuta_message_manager_set_current_view (msg_manager, priv->msg_view,
          &error);

      if (error != NULL)
      {
        g_warning ("Error setting current message view: %s", error->message);
        g_clear_error (&error);
      }
    }
  }

#ifdef ANJUTA_2_28_OR_HIGHER
  if (anjuta_launcher_execute_v (priv->debug_launcher,
                                 NULL,
                                 args,
                                 NULL,
                                 debug_launcher_data_cb,
                                 target))
  {
    beaver_target_set_state (target, TARGET_STATE_BUSY);
    return TRUE;
#else
#ifdef ANJUTA_2_23_TO_26
  if (anjuta_launcher_execute_v (priv->debug_launcher,
                                 args,
                                 NULL,
                                 debug_launcher_data_cb,
                                 target))
  {
    beaver_target_set_state (target, TARGET_STATE_BUSY);
    return TRUE;
#else
  if (anjuta_launcher_execute_v (priv->debug_launcher,
                                 args,
                                 debug_launcher_data_cb,
                                 target))
  {
    beaver_target_set_state (target, TARGET_STATE_BUSY);
    return TRUE;
#endif
#endif
  } else {
    gchar *debug_str = NULL;

    debug_str = g_strjoinv (" ", args);
    g_warning ("Error whilst launching command: %s", debug_str);
    g_free (debug_str);
    g_free (args);

    return FALSE;
  }
}

static gboolean
_beaver_target_remote_debug_stop (BeaverTarget *target, GError **out_err)
{
  gchar *args[] = { "killall gdbserver", NULL };

  return beaver_target_run_remote_v (target, args, NULL);
}

static void
_beaver_target_remote_stop (BeaverTarget *target)
{
  BeaverTargetPrivate *priv = TARGET_PRIVATE (target);

  anjuta_launcher_reset (priv->launcher);
}

static void
launcher_child_exited_cb (AnjutaLauncher *launcher, gint child_pid,
    gint status, gulong time, gpointer userdata)
{
  BeaverTarget *target = BEAVER_TARGET (userdata);

  beaver_target_set_state (target, TARGET_STATE_READY);
}

static void
launcher_data_cb (AnjutaLauncher *launcher, 
    AnjutaLauncherOutputType type, const gchar *chars, gpointer userdata)
{
  BeaverTargetPrivate *priv = TARGET_PRIVATE (userdata);
  GError *error = NULL;

  if (priv->msg_view)
  {
    /* Append to the buffer for the message view to deal with the newlines */
    ianjuta_message_view_buffer_append (priv->msg_view, chars, &error);

    if (error != NULL)
    {
      g_warning ("Error appending to message view: %s", error->message);
      g_clear_error (&error);
    }
  } else {
    g_warning ("No message view to append to.");
  }
}

static void
debug_launcher_data_cb (AnjutaLauncher *launcher, 
    AnjutaLauncherOutputType type, const gchar *chars, gpointer userdata)
{
  BeaverTarget *target = BEAVER_TARGET (userdata);
  BeaverTargetPrivate *priv = TARGET_PRIVATE (userdata);
  GError *error = NULL;
  gchar *old_debug_output = NULL;
  gchar *chars_copy = NULL;

  if (priv->msg_view)
  {
    /* Append to the buffer for the message view to deal with the newlines */
    ianjuta_message_view_buffer_append (priv->msg_view, chars, &error);

    if (error != NULL)
    {
      g_warning ("Error appending to message view: %s", error->message);
      g_clear_error (&error);
    }
  } else {
    g_warning ("No message view to append to.");
  }

  /* 
   * Keep tabs on the output, we need to search this to find out whether our
   * attempts to start the gdbserver have failed or whether the world is a
   * happy place.
   */
  chars_copy = g_strdup (chars);
  g_strstrip (chars_copy);
  
  if (priv->debug_output)
  {
    old_debug_output = priv->debug_output;
    priv->debug_output = g_strconcat (priv->debug_output, chars_copy, NULL);
    g_free (old_debug_output);
    g_free (chars_copy);
  } else {
    priv->debug_output = chars_copy;
  }

  if (strstr (priv->debug_output, "Listening on port") &&
      strstr (priv->debug_output, "Child exited with retcode"))
  {
    if (!strstr (priv->debug_output, "retcode = 0"))
    {
      beaver_target_remote_debug_stop (target, NULL);
    }

    anjuta_launcher_reset (priv->debug_launcher);
    beaver_target_set_state (target, TARGET_STATE_READY);

    /* tidy up */
    g_free (priv->debug_output);
    priv->debug_output = NULL;
  } else if (strstr (priv->debug_output, "Listening on port")) {

    if (!strstr (priv->debug_output, "Terminated"))
    {
      /* FIXME: Should filter this out in the get/set state implementation */
      if (beaver_target_get_state (target) != TARGET_STATE_DEBUGGER_READY)
      {
        beaver_target_set_state (target, TARGET_STATE_DEBUGGER_READY);
      }
    }
  } else {
    /* do nothing */
  }
}

