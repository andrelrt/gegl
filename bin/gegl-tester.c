/* This file is an image processing operation for GEGL
 *
 * GEGL is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * GEGL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GEGL; if not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright 2012 Ville Sokk <ville.sokk@gmail.com>
 */

#include <glib.h>
#include <gegl.h>
#include <gegl-plugin.h>
#include <string.h>


static GRegex   *regex;
static gchar    *data_dir      = NULL;
static gchar    *reference_dir = NULL;
static gchar    *output_dir    = NULL;
static gchar    *pattern       = "";
static gboolean *output_all    = FALSE;

static const GOptionEntry options[] =
{
  {"data-directory", 'd', 0, G_OPTION_ARG_FILENAME, &data_dir,
   "Root directory of files used in the composition", NULL},

  {"reference-directory", 'r', 0, G_OPTION_ARG_FILENAME, &reference_dir,
   "Directory where reference images are located", NULL},

  {"output-directory", 'o', 0, G_OPTION_ARG_FILENAME, &output_dir,
   "Directory where composition output and diff files are saved", NULL},

  {"pattern", 'p', 0, G_OPTION_ARG_STRING, &pattern,
   "Regular expression used to match names of operations to be tested", NULL},

  {"all", 'a', 0, G_OPTION_ARG_NONE, &output_all,
   "Create output for all operations using a standard composition "
   "if no composition is specified", NULL},

  { NULL }
};

/* convert operation name to output path */
static gchar*
operation_to_path (const gchar *op_name,
                   gboolean     diff)
{
  gchar *cleaned = g_strdup (op_name);
  gchar *filename, *output_path;

  g_strdelimit (cleaned, ":", '-');
  if (diff)
    filename = g_strconcat (cleaned, "-diff.png", NULL);
  else
    filename = g_strconcat (cleaned, ".png", NULL);
  output_path = g_build_path (G_DIR_SEPARATOR_S, output_dir, filename, NULL);

  g_free (cleaned);
  g_free (filename);

  return output_path;
}

static gboolean
process_operations (GType type)
{
  GType    *operations;
  gboolean  result = TRUE;
  guint     count;
  gint      i;

  operations = g_type_children (type, &count);

  if (!operations)
    {
      g_free (operations);
      return TRUE;
    }

  for (i = 0; i < count; i++)
    {
      GeglOperationClass *operation_class;
      const gchar        *image, *xml, *name;

      operation_class = g_type_class_ref (operations[i]);
      image           = gegl_operation_class_get_key (operation_class, "reference-image");
      xml             = gegl_operation_class_get_key (operation_class, "reference-composition");
      name            = gegl_operation_class_get_key (operation_class, "name");

      if (name && image && xml && g_regex_match (regex, name, 0, NULL))
        {
          gchar    *image_path  = g_build_path (G_DIR_SEPARATOR_S, reference_dir, image, NULL);
          gchar    *output_path = operation_to_path (name, FALSE);
          GeglNode *composition;

          g_printf ("%s: ", name);

          composition = gegl_node_new_from_xml (xml, data_dir);
          if (!composition)
            {
              g_printf ("\nComposition graph is flawed\n");
              result = FALSE;
            }
          else
            {
              GeglRectangle  ref_bounds, comp_bounds;
              GeglNode      *output, *ref_img;
              gint           ref_pixels;

              output = gegl_node_new_child (composition,
                                            "operation", "gegl:save",
                                            "path", output_path,
                                            NULL);
              gegl_node_link (composition, output);
              gegl_node_process (output);

              ref_img = gegl_node_new_child (composition,
                                             "operation", "gegl:load",
                                             "path", image_path,
                                             NULL);

              ref_bounds  = gegl_node_get_bounding_box (ref_img);
              comp_bounds = gegl_node_get_bounding_box (composition);
              ref_pixels  = ref_bounds.width * ref_bounds.height;

              if (ref_bounds.width != comp_bounds.width ||
                  ref_bounds.height != comp_bounds.height)
                {
                  g_printf ("FAIL\n  Reference and composition differ in size\n");
                  result = FALSE;
                }
              else
                {
                  GeglNode *comparison;
                  gdouble   max_diff;

                  comparison = gegl_node_create_child (composition, "gegl:image-compare");

                  gegl_node_link (composition, comparison);
                  gegl_node_connect_to (ref_img, "output", comparison, "aux");
                  gegl_node_process (comparison);
                  gegl_node_get (comparison, "max diff", &max_diff, NULL);

                  if (max_diff < 1.0)
                    {
                      g_printf ("PASS\n");
                      result = result && TRUE;
                    }
                  else
                    {
                      gdouble  avg_diff_wrong, avg_diff_total;
                      gint     wrong_pixels;

                      gegl_node_get (comparison, "avg_diff_wrong", &avg_diff_wrong,
                                     "avg_diff_total", &avg_diff_total, "wrong_pixels",
                                     &wrong_pixels, NULL);

                      g_printf ("FAIL\n  Reference image and composition differ\n"
                                "    wrong pixels : %i/%i (%2.2f%%)\n"
                                "    max Δe       : %2.3f\n"
                                "    avg Δe       : %2.3f (wrong) %2.3f (total)\n",
                                wrong_pixels, ref_pixels, (wrong_pixels * 100.0 / ref_pixels),
                                max_diff,
                                avg_diff_wrong, avg_diff_total);

                      g_free (output_path);
                      output_path = operation_to_path (name, TRUE);

                      gegl_node_set (output, "path", output_path, NULL);
                      gegl_node_link (comparison, output);
                      gegl_node_process (output);

                      result = FALSE;
                    }
                }
            }

          g_object_unref (composition);
          g_free (image_path);
          g_free (output_path);
        }
      else if (name && output_all && g_regex_match (regex, name, 0, NULL))
        {
          /* use standard composition and images, don't test */
          GeglNode *composition, *input, *aux, *operation, *output;
          gchar    *input_path  = g_build_path (G_DIR_SEPARATOR_S, data_dir,
                                              "standard-input.png", NULL);
          gchar    *aux_path    = g_build_path (G_DIR_SEPARATOR_S, data_dir,
                                              "standard-aux.png", NULL);
          gchar    *output_path = operation_to_path (name, FALSE);

          composition = gegl_node_new ();
          input = gegl_node_new_child (composition,
                                       "operation", "gegl:load",
                                       "path", input_path,
                                       NULL);
          aux = gegl_node_new_child (composition,
                                     "operation", "gegl:load",
                                     "path", aux_path,
                                     NULL);
          operation = gegl_node_create_child (composition, name);
          output = gegl_node_new_child (composition,
                                        "operation", "gegl:save",
                                        "path", output_path,
                                        NULL);

          gegl_node_link (operation, output);

          if (gegl_node_has_pad (operation, "input"))
            gegl_node_link (input, operation);

          if (gegl_node_has_pad (operation, "aux"))
            gegl_node_connect_to (aux, "output", operation, "aux");

          gegl_node_process (output);

          g_free (input_path);
          g_free (aux_path);
          g_free (output_path);
          g_object_unref (composition);
        }

      result = result && process_operations(operations[i]);
    }

  g_free (operations);

  return result;
}

gint
main (gint    argc,
      gchar **argv)
{
  gboolean        result;
  GError         *error = NULL;
  GOptionContext *context;

  g_thread_init (NULL);

  reference_dir = g_get_current_dir ();

  context = g_option_context_new (NULL);
  g_option_context_add_main_entries (context, options, NULL);
  g_option_context_add_group (context, gegl_get_option_group ());

  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_printf ("%s\n", error->message);
      g_error_free (error);
      exit (1);
    }
  else if (!(data_dir && output_dir))
    {
      g_printf ("Data and output directories must be specified\n");
      exit (1);
    }
  else
    {
      regex = g_regex_new (pattern, 0, 0, NULL);

      result = process_operations (GEGL_TYPE_OPERATION);

      g_regex_unref (regex);
      gegl_exit ();

      return result;
    }
}