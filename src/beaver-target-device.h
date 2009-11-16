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

#ifndef _BEAVER_TARGET_DEVICE
#define _BEAVER_TARGET_DEVICE

#include <glib-object.h>

#include "beaver-target.h"

G_BEGIN_DECLS

#define BEAVER_TYPE_TARGET_DEVICE beaver_target_device_get_type()

#define BEAVER_TARGET_DEVICE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  BEAVER_TYPE_TARGET_DEVICE, BeaverTargetDevice))

#define BEAVER_TARGET_DEVICE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  BEAVER_TYPE_TARGET_DEVICE, BeaverTargetDeviceClass))

#define BEAVER_IS_TARGET_DEVICE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  BEAVER_TYPE_TARGET_DEVICE))

#define BEAVER_IS_TARGET_DEVICE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
  BEAVER_TYPE_TARGET_DEVICE))

#define BEAVER_TARGET_DEVICE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
  BEAVER_TYPE_TARGET_DEVICE, BeaverTargetDeviceClass))

typedef struct {
  BeaverTarget parent;
} BeaverTargetDevice;

typedef struct {
  BeaverTargetClass parent_class;
} BeaverTargetDeviceClass;

GType beaver_target_device_get_type (void);

BeaverTarget *beaver_target_device_new (AnjutaShell *shell);

G_END_DECLS

#endif /* _BEAVER_TARGET_DEVICE */
