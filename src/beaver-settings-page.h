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

#ifndef _BEAVER_SETTINGS_PAGE
#define _BEAVER_SETTINGS_PAGE

#include <glib-object.h>
#include <gtk/gtk.h>
#include <libanjuta/anjuta-shell.h>

G_BEGIN_DECLS

#define BEAVER_TYPE_SETTINGS_PAGE beaver_settings_page_get_type()

#define BEAVER_SETTINGS_PAGE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  BEAVER_TYPE_SETTINGS_PAGE, BeaverSettingsPage))

#define BEAVER_SETTINGS_PAGE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  BEAVER_TYPE_SETTINGS_PAGE, BeaverSettingsPageClass))

#define BEAVER_IS_SETTINGS_PAGE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  BEAVER_TYPE_SETTINGS_PAGE))

#define BEAVER_IS_SETTINGS_PAGE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
  BEAVER_TYPE_SETTINGS_PAGE))

#define BEAVER_SETTINGS_PAGE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
  BEAVER_TYPE_SETTINGS_PAGE, BeaverSettingsPageClass))

typedef struct {
  GtkVBox parent;
} BeaverSettingsPage;

typedef struct {
  GtkVBoxClass parent_class;
} BeaverSettingsPageClass;

GType beaver_settings_page_get_type (void);

GtkWidget *beaver_settings_page_new (AnjutaShell *shell);

G_END_DECLS

#endif /* _BEAVER_SETTINGS_PAGE */
