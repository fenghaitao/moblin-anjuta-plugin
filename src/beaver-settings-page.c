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

#include "beaver-settings-page.h"
#include "beaver.h"

G_DEFINE_TYPE (BeaverSettingsPage, beaver_settings_page, GTK_TYPE_VBOX)

#define SETTINGS_PAGE_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), BEAVER_TYPE_SETTINGS_PAGE, BeaverSettingsPagePrivate))

typedef struct _BeaverSettingsPagePrivate BeaverSettingsPagePrivate;

struct _BeaverSettingsPagePrivate
{
  AnjutaShell *shell;
  AnjutaPreferences *prefs;

  GtkWidget *kernel_chooser;
  GtkWidget *rootfs_chooser;
};

enum
{
  PROP_0,
  PROP_SHELL
};

static void create_ui (BeaverSettingsPage *page);

static void
beaver_settings_page_get_property (GObject *object, guint property_id,
                              GValue *value, GParamSpec *pspec)
{
  switch (property_id) {
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
beaver_settings_page_set_property (GObject *object, guint property_id,
                              const GValue *value, GParamSpec *pspec)
{
  BeaverSettingsPagePrivate *priv = SETTINGS_PAGE_PRIVATE (object);

  switch (property_id) {
    case PROP_SHELL:
      priv->shell = g_value_get_object (value);
      priv->prefs = anjuta_shell_get_preferences (priv->shell, NULL);
      break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
beaver_settings_page_dispose (GObject *object)
{
  if (G_OBJECT_CLASS (beaver_settings_page_parent_class)->dispose)
    G_OBJECT_CLASS (beaver_settings_page_parent_class)->dispose (object);
}

static void
beaver_settings_page_finalize (GObject *object)
{
  G_OBJECT_CLASS (beaver_settings_page_parent_class)->finalize (object);
}

static void
beaver_settings_page_constructed (GObject *object)
{
  if (G_OBJECT_CLASS (beaver_settings_page_parent_class)->constructed)
    G_OBJECT_CLASS (beaver_settings_page_parent_class)->constructed (object);

  create_ui (BEAVER_SETTINGS_PAGE (object));
}

static void
beaver_settings_page_class_init (BeaverSettingsPageClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *pspec = NULL;

  g_type_class_add_private (klass, sizeof (BeaverSettingsPagePrivate));

  object_class->get_property = beaver_settings_page_get_property;
  object_class->set_property = beaver_settings_page_set_property;
  object_class->dispose = beaver_settings_page_dispose;
  object_class->finalize = beaver_settings_page_finalize;
  object_class->constructed = beaver_settings_page_constructed;

  pspec = g_param_spec_object ("shell", "shell", "shell", 
      ANJUTA_TYPE_SHELL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE);
  g_object_class_install_property (object_class, PROP_SHELL, pspec);
}

static void
beaver_settings_page_init (BeaverSettingsPage *self)
{
}

GtkWidget *
beaver_settings_page_new (AnjutaShell *shell)
{
  return g_object_new (BEAVER_TYPE_SETTINGS_PAGE, 
      "shell", shell,
      NULL);
}

static gboolean
preferences_timeout_cb (gpointer userdata)
{
  BeaverSettingsPage *page = BEAVER_SETTINGS_PAGE (userdata);
  BeaverSettingsPagePrivate *priv = SETTINGS_PAGE_PRIVATE (page);
  gchar *filename;

  filename = anjuta_preferences_get (priv->prefs, 
      PREFS_PROP_KERNEL);
  gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (priv->kernel_chooser), 
      filename);
  g_free (filename);

  filename = anjuta_preferences_get (priv->prefs, 
      PREFS_PROP_ROOTFS);
  gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (priv->rootfs_chooser), 
      filename);
  g_free (filename);

  return FALSE;
}

static void
radio_toggled_cb (GtkToggleButton *toggle, gpointer userdata)
{
  gtk_widget_set_sensitive (GTK_WIDGET (userdata),
      gtk_toggle_button_get_active (toggle));
}

static void
create_ui (BeaverSettingsPage *page)
{
  BeaverSettingsPagePrivate *priv = SETTINGS_PAGE_PRIVATE (page);

  GtkSizeGroup *opts_labels_group;
  GtkSizeGroup *opts_fields_group;
  GtkSizeGroup *target_labels_group;
  GtkSizeGroup *target_fields_group;

  GtkWidget *frame;
  GtkWidget *inner_vbox;
  GtkWidget *hbox;
  GtkWidget *label;
  GtkWidget *chooser;
  GtkWidget *entry;
  GtkWidget *inner_alignment;
  GtkWidget *qemu_vbox;

  GtkWidget *toolchain_radio;
  GtkWidget *full_radio;

  GtkWidget *qemu_radio;
  GtkWidget *device_radio;

  gboolean res;
  gchar *filename = NULL;

  gtk_box_set_spacing (GTK_BOX (page), 6);
  gtk_box_set_homogeneous (GTK_BOX (page), FALSE);
  gtk_container_set_border_width (GTK_CONTAINER (page), 6);

  /* Frame for options */
  frame = gtk_frame_new (_("<b>Cross-compiler Options</b>"));
  gtk_box_pack_start (GTK_BOX (page), frame, FALSE, FALSE, 2);
  label = gtk_frame_get_label_widget (GTK_FRAME (frame));
  gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
  gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_NONE);

  /* size groups for files */
  opts_labels_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
  opts_fields_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
  
  /* Pack inner vbox */
  inner_vbox = gtk_vbox_new (FALSE, 6);
  inner_alignment = gtk_alignment_new (0, 0.5, 1, 1);
  g_object_set (inner_alignment, "left-padding", 12, "top-padding", 6, NULL);
  gtk_container_add (GTK_CONTAINER (inner_alignment), inner_vbox);
  gtk_container_add (GTK_CONTAINER (frame), inner_alignment);

  /* Radio for external toolchain */
  toolchain_radio = gtk_radio_button_new_with_label (NULL, 
      _("Use an external toolchain:"));
  gtk_box_pack_start (GTK_BOX (inner_vbox), toolchain_radio, TRUE, FALSE, 0);

  /* Widgets for sdk root */
  inner_alignment = gtk_alignment_new (0, 0.5, 1, 1);
  g_object_set (inner_alignment, "left-padding", 24, NULL);
  gtk_box_pack_start (GTK_BOX (inner_vbox), inner_alignment, TRUE, FALSE, 0);

  hbox = gtk_hbox_new (FALSE, 6);
  gtk_container_add (GTK_CONTAINER (inner_alignment), hbox);
  g_signal_connect (toolchain_radio, "toggled", (GCallback)radio_toggled_cb, 
      hbox);

  /* label */
  label = gtk_label_new (_("SDK root: "));
  gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
  gtk_size_group_add_widget (opts_labels_group, label);
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);

  /* chooser */
  chooser = gtk_file_chooser_button_new (_("Select SDK root"), 
      GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
  gtk_size_group_add_widget (opts_fields_group, label);
  gtk_box_pack_start (GTK_BOX (hbox), chooser, TRUE, TRUE, 0);

  res = anjuta_preferences_register_property_raw (
      priv->prefs,
      chooser,
      PREFS_PROP_SDK_ROOT,
      NULL,
      0,
      ANJUTA_PROPERTY_OBJECT_TYPE_FOLDER,
      ANJUTA_PROPERTY_DATA_TYPE_TEXT);

  if (!res)
    g_warning ("Error adding preference for SDK root");

  /* Radio for poky tree */
  full_radio = gtk_radio_button_new_with_label_from_widget (
      GTK_RADIO_BUTTON (toolchain_radio), 
      _("Use a full Moblin tree"));
  gtk_box_pack_start (GTK_BOX (inner_vbox), full_radio, TRUE, FALSE, 0);

  /* Widgets for full tree */
  inner_alignment = gtk_alignment_new (0, 0.5, 1, 1);
  g_object_set (inner_alignment, "left-padding", 24, NULL);
  gtk_box_pack_start (GTK_BOX (inner_vbox), inner_alignment, TRUE, FALSE, 0);

  hbox = gtk_hbox_new (FALSE, 6);
  gtk_container_add (GTK_CONTAINER (inner_alignment), hbox);
  g_signal_connect (full_radio, "toggled", (GCallback)radio_toggled_cb, 
      hbox);

  /* Make the full options insensitive since by default we used external
   * toolchain mode */
  gtk_widget_set_sensitive (hbox, FALSE);

  /* label */
  label = gtk_label_new (_("Moblin root: "));
  gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
  gtk_size_group_add_widget (opts_labels_group, label);
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);

  /* chooser */
  chooser = gtk_file_chooser_button_new (_("Select Moblin root"), 
      GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);

  gtk_size_group_add_widget (opts_fields_group, label);
  gtk_box_pack_start (GTK_BOX (hbox), chooser, TRUE, TRUE, 0);

  res = anjuta_preferences_register_property_raw (
      priv->prefs,
      chooser,
      PREFS_PROP_POKY_ROOT,
      NULL,
      0,
      ANJUTA_PROPERTY_OBJECT_TYPE_FOLDER,
      ANJUTA_PROPERTY_DATA_TYPE_TEXT);

  if (!res)
    g_warning ("Error adding preference for Moblin root");

  /* Register a preference for the toggle */

  /* This is all a bit of a hack, we register the property on the second one
   * in the group. If the mode is 1 then this gets selected and the world is a
   * happy place. But if this is 0, then the group is in an inconsistent
   * state...Ah, but actually when the widgets are created the first is
   * toggled on by default.
   */
#ifdef ANJUTA_2_28_OR_HIGHER
  res = anjuta_preferences_register_property_raw (
      priv->prefs,
      full_radio,
      PREFS_PROP_POKY_MODE,
      NULL,
      0,
      ANJUTA_PROPERTY_OBJECT_TYPE_TOGGLE,
      ANJUTA_PROPERTY_DATA_TYPE_BOOL);
#else
  res = anjuta_preferences_register_property_raw (
      priv->prefs,
      full_radio,
      PREFS_PROP_POKY_MODE,
      NULL,
      0,
      ANJUTA_PROPERTY_OBJECT_TYPE_TOGGLE,
      ANJUTA_PROPERTY_DATA_TYPE_INT);
#endif

  if (!res)
    g_warning ("Error adding preference for mode of operation");

  /* Widgets for toolchain triplet */
  hbox = gtk_hbox_new (FALSE, 6);
  inner_alignment = gtk_alignment_new (0, 0.5, 1, 1);
  g_object_set (inner_alignment, "top-padding", 6, NULL);
  gtk_container_add (GTK_CONTAINER (inner_alignment), hbox);
  gtk_box_pack_start (GTK_BOX (inner_vbox), inner_alignment, TRUE, FALSE, 0);

  /* label */
  label = gtk_label_new (_("Toolchain triplet: "));
  gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
  gtk_size_group_add_widget (opts_labels_group, label);
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);

  /* entry */
  entry = gtk_entry_new ();
  gtk_box_pack_start (GTK_BOX (hbox), entry, TRUE, TRUE, 0);
  gtk_size_group_add_widget (opts_fields_group, label);

  /* register prop */
  res = anjuta_preferences_register_property_raw (priv->prefs,
      entry,
      PREFS_PROP_TRIPLET,
      NULL,
      0,
      ANJUTA_PROPERTY_OBJECT_TYPE_ENTRY,
      ANJUTA_PROPERTY_DATA_TYPE_TEXT);

  if (!res)
    g_warning ("Error adding preference for triplet");

  /* Frame for target */
  frame = gtk_frame_new (_("<b>Target Options</b>"));
  gtk_box_pack_start (GTK_BOX (page), frame, FALSE, FALSE, 2);
  gtk_label_set_use_markup (GTK_LABEL (gtk_frame_get_label_widget (GTK_FRAME (frame))), 
        TRUE);
  gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_NONE);

  /* Pack inner vbox */
  inner_vbox = gtk_vbox_new (FALSE, 6);
  inner_alignment = gtk_alignment_new (0, 0.5, 1, 1);
  g_object_set (inner_alignment, "left-padding", 12, "top-padding", 6, NULL);
  gtk_container_add (GTK_CONTAINER (inner_alignment), inner_vbox);
  gtk_container_add (GTK_CONTAINER (frame), inner_alignment);

  /* QEMU */
  qemu_radio = gtk_radio_button_new_with_label (NULL, _("Use QEMU Device Emulator"));
  gtk_box_pack_start (GTK_BOX (inner_vbox), qemu_radio, TRUE, FALSE, 2);
  
  inner_alignment = gtk_alignment_new (0, 0.5, 1, 1);
  g_object_set (inner_alignment, "left-padding", 24, NULL);
  
  qemu_vbox = gtk_vbox_new (FALSE, 6);
  gtk_container_add (GTK_CONTAINER (inner_alignment), qemu_vbox);
  gtk_box_pack_start (GTK_BOX (inner_vbox), inner_alignment, TRUE, FALSE, 2);

  g_signal_connect (qemu_radio, "toggled", (GCallback)radio_toggled_cb, 
      qemu_vbox);

  /* size groups for files */
  target_labels_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
  target_fields_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

  /* Widgets for kernel */
  hbox = gtk_hbox_new (FALSE, 6);
  gtk_box_pack_start (GTK_BOX (qemu_vbox), hbox, TRUE, FALSE, 0);

  /* label */
  label = gtk_label_new ("Kernel: ");
  gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
  gtk_size_group_add_widget (target_labels_group, label);
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);

  /* chooser */
  chooser = gtk_file_chooser_button_new (_("Select kernel file"), GTK_FILE_CHOOSER_ACTION_OPEN);
  gtk_box_pack_start (GTK_BOX (hbox), chooser, TRUE, TRUE, 0);
  gtk_size_group_add_widget (target_fields_group, chooser);

  anjuta_preferences_register_property_raw (
      priv->prefs,
      chooser,
      PREFS_PROP_KERNEL,
      NULL,
      0,
      ANJUTA_PROPERTY_OBJECT_TYPE_FILE,
      ANJUTA_PROPERTY_DATA_TYPE_TEXT);

  priv->kernel_chooser = chooser;

  /* Widgets for rootfs */
  hbox = gtk_hbox_new (FALSE, 6);
  gtk_box_pack_start (GTK_BOX (qemu_vbox), hbox, TRUE, FALSE, 0);

  /* label */
  label = gtk_label_new ("Root filesystem: ");
  gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
  gtk_size_group_add_widget (target_labels_group, label);
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);

  /* chooser */
  chooser = gtk_file_chooser_button_new (_("Select root filesystem file"), 
      GTK_FILE_CHOOSER_ACTION_OPEN);
  gtk_box_pack_start (GTK_BOX (hbox), chooser, TRUE, TRUE, 0);
  gtk_size_group_add_widget (target_fields_group, chooser);
  priv->rootfs_chooser = chooser;

  anjuta_preferences_register_property_raw (
      priv->prefs,
      chooser,
      PREFS_PROP_ROOTFS,
      NULL,
      0,
      ANJUTA_PROPERTY_OBJECT_TYPE_FILE,
      ANJUTA_PROPERTY_DATA_TYPE_TEXT);

  /* device */
  device_radio = gtk_radio_button_new_with_label_from_widget (
      GTK_RADIO_BUTTON (qemu_radio), _("Use an external device"));
  gtk_box_pack_start (GTK_BOX (inner_vbox), device_radio, TRUE, FALSE, 2);

  /* widgets for device entry */
  inner_alignment = gtk_alignment_new (0, 0.5, 1, 1);
  g_object_set (inner_alignment, "left-padding", 24, NULL);
  gtk_box_pack_start (GTK_BOX (inner_vbox), inner_alignment, TRUE, FALSE, 0);
  hbox = gtk_hbox_new (FALSE, 6);
  gtk_container_add (GTK_CONTAINER (inner_alignment), hbox);
  gtk_widget_set_sensitive (hbox, FALSE);

  g_signal_connect (device_radio, "toggled", (GCallback)radio_toggled_cb, 
      hbox);

  /* Register a preference for the toggle */

  /* This is all a bit of a hack, we register the property on the second one
   * in the group. If the mode is 1 then this gets selected and the world is a
   * happy place. But if this is 0, then the group is in an inconsistent
   * state...Ah, but actually when the widgets are created the first is
   * toggled on by default.
   */
#ifdef ANJUTA_2_28_OR_HIGHER
  res = anjuta_preferences_register_property_raw (
      priv->prefs,
      device_radio,
      PREFS_PROP_TARGET_MODE,
      NULL,
      0,
      ANJUTA_PROPERTY_OBJECT_TYPE_TOGGLE,
      ANJUTA_PROPERTY_DATA_TYPE_BOOL);
#else
  res = anjuta_preferences_register_property_raw (
      priv->prefs,
      device_radio,
      PREFS_PROP_TARGET_MODE,
      NULL,
      0,
      ANJUTA_PROPERTY_OBJECT_TYPE_TOGGLE,
      ANJUTA_PROPERTY_DATA_TYPE_INT);
#endif

  if (!res)
    g_warning ("Error adding preference for mode of operation");

  /* label */
  label = gtk_label_new ("IP address:");
  gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
  gtk_size_group_add_widget (target_labels_group, label);
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);

  /* entry */
  entry = gtk_entry_new ();
  gtk_box_pack_start (GTK_BOX (hbox), entry, TRUE, TRUE, 0);
  gtk_size_group_add_widget (target_fields_group, label);

  /* register prop */
  res = anjuta_preferences_register_property_raw (priv->prefs,
      entry,
      PREFS_PROP_TARGET_IP,
      NULL,
      0,
      ANJUTA_PROPERTY_OBJECT_TYPE_ENTRY,
      ANJUTA_PROPERTY_DATA_TYPE_TEXT);

  if (!res)
    g_warning ("Error adding preference for target IP");

  /* add page */
  gtk_widget_show_all (GTK_WIDGET (page));

  /* 
   * This is a horrible hack around some kind of race condition that seems to
   * mean that the GtkFileChooserButton doesn't reflect the filename it is
   * given by the prefences code in Anjuta. Even if it just gets set here it
   * isn't guaranteed to work. This is the best option i've come up with.
   *
   * After 800ms fire a timeout that reads the values for the preferences and
   * then sets the widgets. It only needs to be done for those that are of
   * ANJUTA_PROPERTY_OBJECT_TYPE_FILE type.
   */
  g_timeout_add (800, preferences_timeout_cb, page);
}

