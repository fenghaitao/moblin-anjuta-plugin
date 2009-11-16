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

#include "beaver-target-device.h"

G_DEFINE_TYPE (BeaverTargetDevice, beaver_target_device, BEAVER_TYPE_TARGET)

#define TARGET_DEVICE_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), BEAVER_TYPE_TARGET_DEVICE, BeaverTargetDevicePrivate))

typedef struct _BeaverTargetDevicePrivate BeaverTargetDevicePrivate;

struct _BeaverTargetDevicePrivate {
  BeaverTargetState state;
  gchar *ip_address;
};

enum
{
  PROP_0 = 0,
  PROP_IP_ADDRESS
};

static void
beaver_target_device_get_property (GObject *object, guint property_id,
                              GValue *value, GParamSpec *pspec)
{
  BeaverTargetDevicePrivate *priv = TARGET_DEVICE_PRIVATE (object);

  switch (property_id) {
    case PROP_IP_ADDRESS:
      g_value_set_string (value, priv->ip_address);
      break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
beaver_target_device_set_property (GObject *object, guint property_id,
                              const GValue *value, GParamSpec *pspec)
{
  BeaverTargetDevicePrivate *priv = TARGET_DEVICE_PRIVATE (object);

  switch (property_id) {
    case PROP_IP_ADDRESS:
      g_free (priv->ip_address);

      priv->ip_address = g_value_dup_string (value);

      if (priv->ip_address)
        priv->state = TARGET_STATE_READY;
      break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
beaver_target_device_dispose (GObject *object)
{
  if (G_OBJECT_CLASS (beaver_target_device_parent_class)->dispose)
    G_OBJECT_CLASS (beaver_target_device_parent_class)->dispose (object);
}

static void
beaver_target_device_finalize (GObject *object)
{
  if (G_OBJECT_CLASS (beaver_target_device_parent_class)->finalize)
    G_OBJECT_CLASS (beaver_target_device_parent_class)->finalize (object);
}

static void
beaver_target_device_set_state (BeaverTarget *target, BeaverTargetState state)
{
  BeaverTargetDevicePrivate *priv = TARGET_DEVICE_PRIVATE (target);

  priv->state = state;
  g_signal_emit_by_name (target, "state-changed", state);
}

static BeaverTargetState
beaver_target_device_get_state (BeaverTarget *target)
{
  BeaverTargetDevicePrivate *priv = TARGET_DEVICE_PRIVATE (target);

  return priv->state;
}

static const gchar *
beaver_target_device_get_ip_address (BeaverTarget *target)
{
  BeaverTargetDevicePrivate *priv = TARGET_DEVICE_PRIVATE (target);

  return priv->ip_address;
}

static void
beaver_target_device_class_init (BeaverTargetDeviceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  BeaverTargetClass *target_class = BEAVER_TARGET_CLASS (klass);
  GParamSpec *pspec = NULL;

  g_type_class_add_private (klass, sizeof (BeaverTargetDevicePrivate));

  object_class->get_property = beaver_target_device_get_property;
  object_class->set_property = beaver_target_device_set_property;
  object_class->dispose = beaver_target_device_dispose;
  object_class->finalize = beaver_target_device_finalize;

  target_class->get_state = beaver_target_device_get_state;
  target_class->set_state = beaver_target_device_set_state;
  target_class->get_ip_address = beaver_target_device_get_ip_address;

  pspec = g_param_spec_string ("ip-address", "ip-address", "ip-address",
      NULL, G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_IP_ADDRESS, pspec);
}

static void
beaver_target_device_init (BeaverTargetDevice *self)
{
}

BeaverTarget *
beaver_target_device_new (AnjutaShell *shell)
{
  return g_object_new (BEAVER_TYPE_TARGET_DEVICE, 
      "shell", shell,
      NULL);
}

