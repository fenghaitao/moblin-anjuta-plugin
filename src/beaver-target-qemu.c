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

#include "beaver-target-qemu.h"
#include "beaver-util.h"

G_DEFINE_TYPE (BeaverTargetQEMU, beaver_target_qemu, BEAVER_TYPE_TARGET)

#define TARGET_QEMU_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), BEAVER_TYPE_TARGET_QEMU, BeaverTargetQEMUPrivate))

#define QEMU_IP_ADDRESS "192.168.7.2"
#define QEMU_SCRIPT "poky-qemu"
#define ICON_FILE "anjuta-plugin-sdk.png"
#define SSH_OPTIONS "-o", "CheckHostIP no", "-o", \
    "StrictHostKeyChecking no", "-o", "UserKnownHostsFile /dev/null"

typedef struct _BeaverTargetQEMUPrivate BeaverTargetQEMUPrivate;

struct _BeaverTargetQEMUPrivate
{
  gchar *rootfs;
  gchar *kernel;

  BeaverTargetState state;
  
  AnjutaLauncher *launcher;
  IAnjutaMessageView *msg_view;
};

enum
{
  PROP_0 = 0,
  PROP_ROOTFS,
  PROP_KERNEL,
  PROP_IP_ADDRESS,
};

static BeaverTargetState beaver_target_qemu_get_state (BeaverTarget *target);
static void beaver_target_qemu_set_state (BeaverTarget *target, BeaverTargetState state);
static const gchar *beaver_target_qemu_get_ip_address (BeaverTarget *target);

static void launcher_data_cb (AnjutaLauncher *launcher, 
    AnjutaLauncherOutputType type, const gchar *chars, gpointer userdata);
static void launcher_child_exited_cb (AnjutaLauncher *launcher, gint child_pid,
    gint status, gulong time, gpointer userdata);

static void
beaver_target_qemu_get_property (GObject *object, guint property_id,
                              GValue *value, GParamSpec *pspec)
{
  BeaverTargetQEMUPrivate *priv = TARGET_QEMU_PRIVATE (object);

  switch (property_id) {
    case PROP_ROOTFS:
      g_value_set_string (value, priv->rootfs);
      break;
    case PROP_KERNEL:
      g_value_set_string (value, priv->kernel);
      break;
    case PROP_IP_ADDRESS:
      g_value_set_string (value, QEMU_IP_ADDRESS);
      break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
beaver_target_qemu_set_property (GObject *object, guint property_id,
                              const GValue *value, GParamSpec *pspec)
{
  BeaverTargetQEMUPrivate *priv = TARGET_QEMU_PRIVATE (object);
  gchar *old_rootfs = NULL;
  gchar *old_kernel = NULL;

  switch (property_id) {
    case PROP_ROOTFS:
      old_rootfs = priv->rootfs;
      priv->rootfs = g_value_dup_string (value);
      break;
    case PROP_KERNEL:
      old_kernel = priv->kernel;
      priv->kernel = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }

  /* don't do anything if we are running */
  if (!priv->launcher || anjuta_launcher_is_busy (priv->launcher))
  {
    if ((old_kernel && old_rootfs) && 
        !(priv->rootfs && priv->kernel))
    {
      beaver_target_qemu_set_state (BEAVER_TARGET (object), 
          TARGET_STATE_UNKNOWN);
    } else if (priv->rootfs && priv->kernel) {
      beaver_target_qemu_set_state (BEAVER_TARGET (object), 
          TARGET_STATE_STOPPED);
    }
  }

  g_free (old_rootfs);
  g_free (old_kernel);
}

static void
beaver_target_qemu_dispose (GObject *object)
{
  BeaverTargetQEMUPrivate *priv = TARGET_QEMU_PRIVATE (object);
  IAnjutaMessageManager *msg_manager = NULL;

  if (priv->launcher)
  {
    g_object_unref (priv->launcher);
    priv->launcher = NULL;
  }

  if (priv->msg_view)
  {
    msg_manager = beaver_target_get_message_manager (BEAVER_TARGET (object));
    ianjuta_message_manager_remove_view (msg_manager, priv->msg_view, NULL);
    priv->msg_view = NULL;
  }

  if (G_OBJECT_CLASS (beaver_target_qemu_parent_class)->dispose)
    G_OBJECT_CLASS (beaver_target_qemu_parent_class)->dispose (object);
}

static void
beaver_target_qemu_finalize (GObject *object)
{
  G_OBJECT_CLASS (beaver_target_qemu_parent_class)->finalize (object);
}

static void
beaver_target_qemu_class_init (BeaverTargetQEMUClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  BeaverTargetClass *target_class = BEAVER_TARGET_CLASS (klass);
  GParamSpec *pspec = NULL;

  g_type_class_add_private (klass, sizeof (BeaverTargetQEMUPrivate));

  object_class->get_property = beaver_target_qemu_get_property;
  object_class->set_property = beaver_target_qemu_set_property;
  object_class->dispose = beaver_target_qemu_dispose;
  object_class->finalize = beaver_target_qemu_finalize;

  target_class->get_state = beaver_target_qemu_get_state;
  target_class->set_state = beaver_target_qemu_set_state;
  target_class->get_ip_address = beaver_target_qemu_get_ip_address;

  pspec = g_param_spec_string ("rootfs", "rootfs", "rootfs", NULL,
      G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_ROOTFS, pspec);

  pspec = g_param_spec_string ("kernel", "kernel", "kernel", NULL,
      G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_KERNEL, pspec);

  pspec = g_param_spec_string ("ip-address", "ip-address", "ip-address", NULL,
      G_PARAM_READABLE);
  g_object_class_install_property (object_class, PROP_IP_ADDRESS, pspec);
}

static void
beaver_target_qemu_init (BeaverTargetQEMU *self)
{
}

BeaverTarget *
beaver_target_qemu_new (AnjutaShell *shell)
{
  return g_object_new (BEAVER_TYPE_TARGET_QEMU,
      "shell", shell,
      NULL);
}

gboolean
beaver_target_qemu_start (BeaverTargetQEMU *btq, GError **out_err)
{
  BeaverTargetQEMUPrivate *priv = TARGET_QEMU_PRIVATE (btq);
  GError *error = NULL;
  IAnjutaMessageManager *msg_manager = NULL;

  if (!priv->msg_view)
  {
    /* Create a new view */
    msg_manager = beaver_target_get_message_manager (BEAVER_TARGET (btq));
    priv->msg_view = ianjuta_message_manager_add_view (msg_manager,
        _("QEMU"), ICON_FILE, &error);

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

  if (!priv->launcher)
  {
    priv->launcher = anjuta_launcher_new ();
    g_signal_connect (priv->launcher, "child-exited", 
        (GCallback)launcher_child_exited_cb, btq);
  }

  if (priv->kernel && priv->rootfs)
  {
    gchar *args[] = {QEMU_SCRIPT, priv->kernel, priv->rootfs, NULL};
#ifdef ANJUTA_2_28_OR_HIGHER
    if (!anjuta_launcher_execute_v (priv->launcher,
                                    NULL,
                                    args,
                                    NULL,
                                    launcher_data_cb,
                                    btq))
    {
      g_warning ("Error launching QEMU");
      return FALSE;
#else
#ifdef ANJUTA_2_23_TO_26
    if (!anjuta_launcher_execute_v (priv->launcher,
                                    args,
                                    NULL,
                                    launcher_data_cb,
                                    btq))
    {
      g_warning ("Error launching QEMU");
      return FALSE;
#else
    if (!anjuta_launcher_execute_v (priv->launcher,
                                    args,
                                    launcher_data_cb,
                                    btq))
    {
      g_warning ("Error launching QEMU");
      return FALSE;
#endif
#endif
    } else {
      beaver_target_qemu_set_state (BEAVER_TARGET (btq), 
          TARGET_STATE_READY);
    }
  }

  return TRUE;
}

gboolean
beaver_target_qemu_shutdown (BeaverTargetQEMU *btq, GError **out_err)
{
  gchar *args[] = { "reboot", NULL };

  return beaver_target_run_remote_v (BEAVER_TARGET (btq), args, NULL);
}

static BeaverTargetState
beaver_target_qemu_get_state (BeaverTarget *target)
{
  BeaverTargetQEMUPrivate *priv = TARGET_QEMU_PRIVATE (target);

  return priv->state;
}

static const gchar *
beaver_target_qemu_get_ip_address (BeaverTarget *target)
{
  return QEMU_IP_ADDRESS;
}

static void
beaver_target_qemu_set_state (BeaverTarget *target, BeaverTargetState state)
{
  BeaverTargetQEMU *btq = BEAVER_TARGET_QEMU (target);
  BeaverTargetQEMUPrivate *priv = TARGET_QEMU_PRIVATE (btq);

  /* We've been asked to go into READY state, but are we really ready... */
  if (state == TARGET_STATE_READY && 
      !anjuta_launcher_is_busy (priv->launcher))
  {
    /* do nothing */
  } else {
    priv->state = state;
    g_signal_emit_by_name (btq, "state-changed");
  }
}

static void
launcher_data_cb (AnjutaLauncher *launcher, 
    AnjutaLauncherOutputType type, const gchar *chars, gpointer userdata)
{
  BeaverTargetQEMUPrivate *priv = TARGET_QEMU_PRIVATE (userdata);
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
launcher_child_exited_cb (AnjutaLauncher *launcher, gint child_pid,
    gint status, gulong time, gpointer userdata)
{
  BeaverTargetQEMUPrivate *priv = TARGET_QEMU_PRIVATE (userdata);
  
  /* check we're still valid */
  if (priv->rootfs && priv->kernel)
  {
    beaver_target_qemu_set_state (BEAVER_TARGET (userdata), TARGET_STATE_STOPPED);
  } else {
    beaver_target_qemu_set_state (BEAVER_TARGET (userdata), TARGET_STATE_UNKNOWN);
  }
}
