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

#ifndef _BEAVER_TARGET_QEMU
#define _BEAVER_TARGET_QEMU

#include <config.h>
#include <glib-object.h>

#include <libanjuta/anjuta-plugin.h>
#include <libanjuta/interfaces/ianjuta-message-manager.h>
#include <libanjuta/anjuta-launcher.h>

#include "beaver-target.h"

G_BEGIN_DECLS

#define BEAVER_TYPE_TARGET_QEMU beaver_target_qemu_get_type()

#define BEAVER_TARGET_QEMU(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  BEAVER_TYPE_TARGET_QEMU, BeaverTargetQEMU))

#define BEAVER_TARGET_QEMU_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  BEAVER_TYPE_TARGET_QEMU, BeaverTargetQEMUClass))

#define BEAVER_IS_TARGET_QEMU(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  BEAVER_TYPE_TARGET_QEMU))

#define BEAVER_IS_TARGET_QEMU_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
  BEAVER_TYPE_TARGET_QEMU))

#define BEAVER_TARGET_QEMU_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
  BEAVER_TYPE_TARGET_QEMU, BeaverTargetQEMUClass))

typedef struct {
  BeaverTarget parent;
} BeaverTargetQEMU;

typedef struct {
  BeaverTargetClass parent_class;
} BeaverTargetQEMUClass;

GType beaver_target_qemu_get_type (void);

BeaverTarget *beaver_target_qemu_new (AnjutaShell *shell);
gboolean beaver_target_qemu_start (BeaverTargetQEMU *btq, GError **error);
gboolean beaver_target_qemu_shutdown (BeaverTargetQEMU *btq, GError **error);

G_END_DECLS

#endif /* _BEAVER_TARGET_QEMU */
