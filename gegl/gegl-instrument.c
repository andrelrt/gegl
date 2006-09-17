/* This file is part of GEGL
 *
 * GEGL is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * GEGL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GEGL; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Copyright 2006 Øyvind Kolås
 */

#include <glib.h>
#include <string.h>
#include "gegl-instrument.h"

long babl_ticks (void);
long gegl_ticks (void)
{
  return babl_ticks ();
}

typedef struct _Timing Timing;

struct _Timing {
  gchar   *name;
  long     usecs;
  Timing  *parent;
  Timing  *children;
  Timing  *next;
};

Timing *root = NULL;

static Timing *iter_next (Timing *iter)
{
  if (iter->children)
    {
      iter=iter->children;
    }
  else if (iter->next)
    {
      iter=iter->next;
    }
  else
    {
      while (iter && !iter->next)
        iter=iter->parent;
      if (iter && iter->next)
        iter=iter->next;
      else
        return NULL;
    }
  return iter;
}

static gint timing_depth (Timing *timing)
{
  Timing *iter = timing;
  gint    ret  = 0;

  while (iter && 
         iter->parent)
    {
      ret++;
      iter=iter->parent;
    }

  return ret;
}

Timing *timing_find (Timing      *root,
                     const gchar *name)
{
  Timing *iter = root;
  if (!iter)
    return NULL;

  while (iter)
    {
      if (!strcmp (iter->name, name))
        return iter;
      if (timing_depth (iter_next (iter)) <= timing_depth(root))
        return NULL;
      iter = iter_next (iter);
    }
  return NULL;
}

void
gegl_instrument (const gchar *parent_name,
                 const gchar *name,
                 long         usecs)
{
  Timing *iter;
  Timing *parent;
  if (root == NULL)
    {
      root = g_malloc0 (sizeof (Timing));
      root->name = g_strdup (parent_name);
    }
  parent = timing_find (root, parent_name);
  if (!parent)
    g_warning ("%s", parent_name);
  g_assert (parent);
  iter = timing_find (parent, name);
  if (!iter)
    {
      iter = g_malloc0 (sizeof (Timing));
      iter->name = g_strdup (name);
      iter->parent = parent;
      iter->next = parent->children;
      parent->children = iter;
    }
  iter->usecs += usecs;
}


static glong timing_child_sum (Timing *timing)
{
  Timing *iter = timing->children;
  glong sum = 0;

  while (iter)
    {
      sum += iter->usecs;
      iter = iter->next;
    }

  return sum;
}

static glong timing_other (Timing *timing)
{
  if (timing->children)
    return timing->usecs - timing_child_sum (timing);
  return 0;
}

static float seconds(glong usecs)
{
  return usecs / 1000000.0;
}

static float normalized(glong usecs)
{
  return 1.0 * usecs / root->usecs;
}

#include <string.h>

GString *
tab_to (GString *string, gint position)
{
  gchar *p;
  gint curcount = 0;
  gint i;
 
  p = strrchr (string->str, '\n');
  if (!p)
    {
      p=string->str;
      position-=1;
    }
  while (p && *p!='\0')
    {
      curcount++;
      p++;
    }

  if (curcount > position)
    {
      g_warning ("tab overflow");
    }
  else
    {
      for (i=0;i<=position-curcount;i++)
        string = g_string_append (string, " ");
    }
  return string; 
}

static gchar *eight[]={
" ",
"▏",
"▍",
"▌",
"▋",
"▊",
"▉",
"█"};

GString *
bar (GString *string, gint width, gfloat value)
{
  gboolean utf8 = TRUE;
  gint i;
  if (utf8)
    {
      gint blocks = width * 8 * value;

      for (i=0;i< blocks/8; i++)
        {
          string = g_string_append (string, "█");
        }
      string = g_string_append (string, eight[blocks%8]);
    }
  else
    {
      for (i=0;i<width;i++)
        {
          if (width * value > i)
            string = g_string_append (string, "X");
        }
    }
  return string;
}

gchar *
gegl_instrument_xhtml (void)
{
  GString *s = g_string_new ("");
  gchar  *ret;
  Timing *iter = root;

  while (iter)
    {
      gchar *buf;

      s = tab_to (s, timing_depth (iter) * 2);

      s = g_string_append (s, iter->name);

      s = tab_to (s, 23);
      buf = g_strdup_printf ("%f", seconds (iter->usecs));
      s = g_string_append (s, buf);
      g_free (buf);
      s = tab_to (s, 34);
      s = bar (s, 46, normalized (iter->usecs));
      s = g_string_append (s, "\n");

      if (timing_depth(iter_next (iter)) < timing_depth (iter))
        {
          s = tab_to (s, timing_depth (iter) * 2);
          s = g_string_append (s, "other");
          s = tab_to (s, 23);
          buf = g_strdup_printf ("%f", seconds (timing_other (iter->parent)));
          s = g_string_append (s, buf);
          g_free (buf);
          s = tab_to (s, 34);
          s = bar (s, 46, normalized (timing_other (iter->parent)));
          s = g_string_append (s, "\n");
        }

      iter = iter_next (iter);
    }
  
  ret = g_strdup (s->str);
  g_string_free (s, TRUE);
  return ret;
}
