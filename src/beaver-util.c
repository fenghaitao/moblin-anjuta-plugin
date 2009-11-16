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

#include "beaver-util.h"

/* 
 * Callback that gets fired when data gets flushed in the view because it's
 * the end of line
 */
void
beaver_util_message_view_buffer_flushed_cb (IAnjutaMessageView *view, gchar *data, 
    gpointer userdata)
{
  /* Append to the message view */
  ianjuta_message_view_append (view, IANJUTA_MESSAGE_VIEW_TYPE_NORMAL,
    data, "", NULL);
}

/* do not free the strings in the returned vector but do free the vector */
gchar **
beaver_util_strv_concat (gchar **strv_1, gchar **strv_2)
{
  gchar **res = NULL;
  gchar **p = NULL;
  
  res = g_new0 (gchar *, g_strv_length (strv_1) + g_strv_length (strv_2) + 1);
  p = res;

  while (*strv_1 != NULL)
  {
    *p = *strv_1;

    p++;
    strv_1++;
  }

  while (*strv_2 != NULL)
  {
    *p = *strv_2;

    p++;
    strv_2++;
  }

  *p = NULL;

  return res;
}

gchar **
beaver_util_strv_joinv (gchar **strv_1, ...)
{
  gchar **res;
  gchar **strv_j;

  gint i;
  gint items;

  va_list args;
  va_start (args, strv_1);

  items = g_strv_length (strv_1);

  res = g_malloc0 ((items + 1) * sizeof (gchar *));
  i = 0;

  while (*strv_1 != NULL)
  {
    res[i] = *strv_1;

    i++;
    strv_1++;
  }

  while ((strv_j = va_arg (args, gchar **)) != NULL)
  {
    items += g_strv_length (strv_j);
    res = g_realloc (res, (items + 1) * sizeof (gchar *));

    while (*strv_j != NULL)
    {
      res[i] = *strv_j;

      i++;
      strv_j++;
    }
  }

  va_end (args);

  res[i] = NULL;

  return res;
}
