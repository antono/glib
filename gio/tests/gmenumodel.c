#include <gio/gio.h>

/* TestItem {{{1 */

/* This utility struct is used by both the RandomMenu and MirrorMenu
 * class implementations below.
 */
typedef struct {
  GHashTable *attributes;
  GHashTable *links;
} TestItem;

static TestItem *
test_item_new (GHashTable *attributes,
               GHashTable *links)
{
  TestItem *item;

  item = g_slice_new (TestItem);
  item->attributes = g_hash_table_ref (attributes);
  item->links = g_hash_table_ref (links);

  return item;
}

static void
test_item_free (gpointer data)
{
  TestItem *item = data;

  g_hash_table_unref (item->attributes);
  g_hash_table_unref (item->links);

  g_slice_free (TestItem, item);
}

/* RandomMenu {{{1 */
#define MAX_ITEMS 5
#define TOP_ORDER 4

typedef struct {
  GMenuModel parent_instance;

  GSequence *items;
  gint order;
} RandomMenu;

typedef GMenuModelClass RandomMenuClass;

static GType random_menu_get_type (void);
G_DEFINE_TYPE (RandomMenu, random_menu, G_TYPE_MENU_MODEL);

static gboolean
random_menu_is_mutable (GMenuModel *model)
{
  return TRUE;
}

static gint
random_menu_get_n_items (GMenuModel *model)
{
  RandomMenu *menu = (RandomMenu *) model;

  return g_sequence_get_length (menu->items);
}

static void
random_menu_get_item_attributes (GMenuModel  *model,
                                 gint         position,
                                 GHashTable **table)
{
  RandomMenu *menu = (RandomMenu *) model;
  TestItem *item;

  item = g_sequence_get (g_sequence_get_iter_at_pos (menu->items, position));
  *table = g_hash_table_ref (item->attributes);
}

static void
random_menu_get_item_links (GMenuModel  *model,
                            gint         position,
                            GHashTable **table)
{
  RandomMenu *menu = (RandomMenu *) model;
  TestItem *item;

  item = g_sequence_get (g_sequence_get_iter_at_pos (menu->items, position));
  *table = g_hash_table_ref (item->links);
}

static void
random_menu_finalize (GObject *object)
{
  RandomMenu *menu = (RandomMenu *) object;

  g_sequence_free (menu->items);

  G_OBJECT_CLASS (random_menu_parent_class)
    ->finalize (object);
}

static void
random_menu_init (RandomMenu *menu)
{
}

static void
random_menu_class_init (GMenuModelClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  class->is_mutable = random_menu_is_mutable;
  class->get_n_items = random_menu_get_n_items;
  class->get_item_attributes = random_menu_get_item_attributes;
  class->get_item_links = random_menu_get_item_links;

  object_class->finalize = random_menu_finalize;
}

static RandomMenu * random_menu_new (GRand *rand, gint order);

static void
random_menu_change (RandomMenu *menu,
                    GRand      *rand)
{
  gint position, removes, adds;
  GSequenceIter *point;
  gint n_items;
  gint i;

  n_items = g_sequence_get_length (menu->items);

  do
    {
      position = g_rand_int_range (rand, 0, n_items + 1);
      removes = g_rand_int_range (rand, 0, n_items - position + 1);
      adds = g_rand_int_range (rand, 0, MAX_ITEMS - (n_items - removes) + 1);
    }
  while (removes == 0 && adds == 0);

  point = g_sequence_get_iter_at_pos (menu->items, position + removes);

  if (removes)
    {
      GSequenceIter *start;

      start = g_sequence_get_iter_at_pos (menu->items, position);
      g_sequence_remove_range (start, point);
    }

  for (i = 0; i < adds; i++)
    {
      const gchar *label;
      GHashTable *links;
      GHashTable *attributes;

      attributes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_variant_unref);
      links = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_object_unref);

      if (menu->order > 0 && g_rand_boolean (rand))
        {
          RandomMenu *child;
	  const gchar *subtype;

          child = random_menu_new (rand, menu->order - 1);

          if (g_rand_boolean (rand))
            {
              subtype = G_MENU_LINK_SECTION;
              /* label some section headers */
              if (g_rand_boolean (rand))
                label = "Section";
              else
                label = NULL;
            }
          else
            {
              /* label all submenus */
              subtype = G_MENU_LINK_SUBMENU;
              label = "Submenu";
            }

          g_hash_table_insert (links, g_strdup (subtype), child);
        }
      else
        /* label all terminals */
        label = "Menu Item";

      if (label)
        g_hash_table_insert (attributes, g_strdup ("label"), g_variant_ref_sink (g_variant_new_string (label)));

      g_sequence_insert_before (point, test_item_new (attributes, links));
      g_hash_table_unref (links);
      g_hash_table_unref (attributes);
    }

  g_menu_model_items_changed (G_MENU_MODEL (menu), position, removes, adds);
}

static RandomMenu *
random_menu_new (GRand *rand,
                 gint   order)
{
  RandomMenu *menu;

  menu = g_object_new (random_menu_get_type (), NULL);
  menu->items = g_sequence_new (test_item_free);
  menu->order = order;

  random_menu_change (menu, rand);

  return menu;
}

/* MirrorMenu {{{1 */
typedef struct {
  GMenuModel parent_instance;

  GMenuModel *clone_of;
  GSequence *items;
  gulong handler_id;
} MirrorMenu;

typedef GMenuModelClass MirrorMenuClass;

static GType mirror_menu_get_type (void);
G_DEFINE_TYPE (MirrorMenu, mirror_menu, G_TYPE_MENU_MODEL);

static gboolean
mirror_menu_is_mutable (GMenuModel *model)
{
  MirrorMenu *menu = (MirrorMenu *) model;

  return menu->handler_id != 0;
}

static gint
mirror_menu_get_n_items (GMenuModel *model)
{
  MirrorMenu *menu = (MirrorMenu *) model;

  return g_sequence_get_length (menu->items);
}

static void
mirror_menu_get_item_attributes (GMenuModel  *model,
                                 gint         position,
                                 GHashTable **table)
{
  MirrorMenu *menu = (MirrorMenu *) model;
  TestItem *item;

  item = g_sequence_get (g_sequence_get_iter_at_pos (menu->items, position));
  *table = g_hash_table_ref (item->attributes);
}

static void
mirror_menu_get_item_links (GMenuModel  *model,
                            gint         position,
                            GHashTable **table)
{
  MirrorMenu *menu = (MirrorMenu *) model;
  TestItem *item;

  item = g_sequence_get (g_sequence_get_iter_at_pos (menu->items, position));
  *table = g_hash_table_ref (item->links);
}

static void
mirror_menu_finalize (GObject *object)
{
  MirrorMenu *menu = (MirrorMenu *) object;

  if (menu->handler_id)
    g_signal_handler_disconnect (menu->clone_of, menu->handler_id);

  g_sequence_free (menu->items);
  g_object_unref (menu->clone_of);

  G_OBJECT_CLASS (mirror_menu_parent_class)
    ->finalize (object);
}

static void
mirror_menu_init (MirrorMenu *menu)
{
}

static void
mirror_menu_class_init (GMenuModelClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  class->is_mutable = mirror_menu_is_mutable;
  class->get_n_items = mirror_menu_get_n_items;
  class->get_item_attributes = mirror_menu_get_item_attributes;
  class->get_item_links = mirror_menu_get_item_links;

  object_class->finalize = mirror_menu_finalize;
}

static MirrorMenu * mirror_menu_new (GMenuModel *clone_of);

static void
mirror_menu_changed (GMenuModel *model,
                     gint        position,
                     gint        removed,
                     gint        added,
                     gpointer    user_data)
{
  MirrorMenu *menu = user_data;
  GSequenceIter *point;
  gint i;

  g_assert (model == menu->clone_of);

  point = g_sequence_get_iter_at_pos (menu->items, position + removed);

  if (removed)
    {
      GSequenceIter *start;

      start = g_sequence_get_iter_at_pos (menu->items, position);
      g_sequence_remove_range (start, point);
    }

  for (i = position; i < position + added; i++)
    {
      GMenuAttributeIter *attr_iter;
      GMenuLinkIter *link_iter;
      GHashTable *links;
      GHashTable *attributes;
      const gchar *name;
      GMenuModel *child;
      GVariant *value;

      attributes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_variant_unref);
      links = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_object_unref);

      attr_iter = g_menu_model_iterate_item_attributes (model, i);
      while (g_menu_attribute_iter_get_next (attr_iter, &name, &value))
        {
          g_hash_table_insert (attributes, g_strdup (name), value);
        }
      g_object_unref (attr_iter);

      link_iter = g_menu_model_iterate_item_links (model, i);
      while (g_menu_link_iter_get_next (link_iter, &name, &child))
        {
          g_hash_table_insert (links, g_strdup (name), mirror_menu_new (child));
          g_object_unref (child);
        }
      g_object_unref (link_iter);

      g_sequence_insert_before (point, test_item_new (attributes, links));
      g_hash_table_unref (attributes);
      g_hash_table_unref (links);
    }

  g_menu_model_items_changed (G_MENU_MODEL (menu), position, removed, added);
}

static MirrorMenu *
mirror_menu_new (GMenuModel *clone_of)
{
  MirrorMenu *menu;

  menu = g_object_new (mirror_menu_get_type (), NULL);
  menu->items = g_sequence_new (test_item_free);
  menu->clone_of = g_object_ref (clone_of);

  if (g_menu_model_is_mutable (clone_of))
    menu->handler_id = g_signal_connect (clone_of, "items-changed", G_CALLBACK (mirror_menu_changed), menu);
  mirror_menu_changed (clone_of, 0, 0, g_menu_model_get_n_items (clone_of), menu);

  return menu;
}

/* check_menus_equal(), assert_menus_equal() {{{1 */
static gboolean
check_menus_equal (GMenuModel *a,
                   GMenuModel *b)
{
  gboolean equal = TRUE;
  gint a_n, b_n;
  gint i;

  a_n = g_menu_model_get_n_items (a);
  b_n = g_menu_model_get_n_items (b);

  if (a_n != b_n)
    return FALSE;

  for (i = 0; i < a_n; i++)
    {
      GMenuAttributeIter *attr_iter;
      GVariant *a_value, *b_value;
      GMenuLinkIter *link_iter;
      GMenuModel *a_menu, *b_menu;
      const gchar *name;

      attr_iter = g_menu_model_iterate_item_attributes (a, i);
      while (g_menu_attribute_iter_get_next (attr_iter, &name, &a_value))
        {
          b_value = g_menu_model_get_item_attribute_value (b, i, name, NULL);
          equal &= b_value && g_variant_equal (a_value, b_value);
          if (b_value)
            g_variant_unref (b_value);
          g_variant_unref (a_value);
        }
      g_object_unref (attr_iter);

      attr_iter = g_menu_model_iterate_item_attributes (b, i);
      while (g_menu_attribute_iter_get_next (attr_iter, &name, &b_value))
        {
          a_value = g_menu_model_get_item_attribute_value (a, i, name, NULL);
          equal &= a_value && g_variant_equal (a_value, b_value);
          if (a_value)
            g_variant_unref (a_value);
          g_variant_unref (b_value);
        }
      g_object_unref (attr_iter);

      link_iter = g_menu_model_iterate_item_links (a, i);
      while (g_menu_link_iter_get_next (link_iter, &name, &a_menu))
        {
          b_menu = g_menu_model_get_item_link (b, i, name);
          equal &= b_menu && check_menus_equal (a_menu, b_menu);
          if (b_menu)
            g_object_unref (b_menu);
          g_object_unref (a_menu);
        }
      g_object_unref (link_iter);

      link_iter = g_menu_model_iterate_item_links (b, i);
      while (g_menu_link_iter_get_next (link_iter, &name, &b_menu))
        {
          a_menu = g_menu_model_get_item_link (a, i, name);
          equal &= a_menu && check_menus_equal (a_menu, b_menu);
          if (a_menu)
            g_object_unref (a_menu);
          g_object_unref (b_menu);
        }
      g_object_unref (link_iter);
    }

  return equal;
}

static void
assert_menus_equal (GMenuModel *a,
                    GMenuModel *b)
{
  if (!check_menus_equal (a, b))
    {
      GString *string;

      string = g_string_new ("\n  <a>\n");
      g_menu_markup_print_string (string, G_MENU_MODEL (a), 4, 2);
      g_string_append (string, "  </a>\n\n-------------\n  <b>\n");
      g_menu_markup_print_string (string, G_MENU_MODEL (b), 4, 2);
      g_string_append (string, "  </b>\n");
      g_error ("%s", string->str);
    }
}

/* Test cases {{{1 */
static void
test_equality (void)
{
  GRand *randa, *randb;
  guint32 seed;
  gint i;

  seed = g_test_rand_int ();

  randa = g_rand_new_with_seed (seed);
  randb = g_rand_new_with_seed (seed);

  for (i = 0; i < 500; i++)
    {
      RandomMenu *a, *b;

      a = random_menu_new (randa, TOP_ORDER);
      b = random_menu_new (randb, TOP_ORDER);
      assert_menus_equal (G_MENU_MODEL (a), G_MENU_MODEL (b));
      g_object_unref (b);
      g_object_unref (a);
    }

  g_rand_int (randa);

  for (i = 0; i < 500;)
    {
      RandomMenu *a, *b;

      a = random_menu_new (randa, TOP_ORDER);
      b = random_menu_new (randb, TOP_ORDER);
      if (check_menus_equal (G_MENU_MODEL (a), G_MENU_MODEL (b)))
        {
          /* by chance, they may really be equal.  double check. */
          GString *as, *bs;

          as = g_menu_markup_print_string (NULL, G_MENU_MODEL (a), 4, 2);
          bs = g_menu_markup_print_string (NULL, G_MENU_MODEL (b), 4, 2);
          g_assert_cmpstr (as->str, ==, bs->str);
          g_string_free (bs, TRUE);
          g_string_free (as, TRUE);

          /* we're here because randa and randb just generated equal
           * menus.  they may do it again, so throw away randb and make
           * a fresh one.
           */
          g_rand_free (randb);
          randb = g_rand_new_with_seed (g_rand_int (randa));
        }
      else
        /* make sure we get enough unequals (ie: no GRand failure) */
        i++;

      g_object_unref (b);
      g_object_unref (a);
    }

  g_rand_free (randb);
  g_rand_free (randa);
}

static void
test_random (void)
{
  RandomMenu *random;
  MirrorMenu *mirror;
  GRand *rand;
  gint i;

  rand = g_rand_new_with_seed (g_test_rand_int ());
  random = random_menu_new (rand, TOP_ORDER);
  mirror = mirror_menu_new (G_MENU_MODEL (random));

  for (i = 0; i < 500; i++)
    {
      assert_menus_equal (G_MENU_MODEL (random), G_MENU_MODEL (mirror));
      random_menu_change (random, rand);
    }

  g_object_unref (mirror);
  g_object_unref (random);

  g_rand_free (rand);
}

struct roundtrip_state
{
  RandomMenu *random;
  MirrorMenu *proxy_mirror;
  GDBusMenuModel *proxy;
  GMainLoop *loop;
  GRand *rand;
  gint success;
  gint count;
};

static gboolean
roundtrip_step (gpointer data)
{
  struct roundtrip_state *state = data;

  if (check_menus_equal (G_MENU_MODEL (state->random), G_MENU_MODEL (state->proxy)) &&
      check_menus_equal (G_MENU_MODEL (state->random), G_MENU_MODEL (state->proxy_mirror)))
    {
      state->success++;
      state->count = 0;

      if (state->success < 100)
        random_menu_change (state->random, state->rand);
      else
        g_main_loop_quit (state->loop);
    }
  else if (state->count == 100)
    {
      assert_menus_equal (G_MENU_MODEL (state->random), G_MENU_MODEL (state->proxy));
      g_assert_not_reached ();
    }
  else
    state->count++;

  return G_SOURCE_CONTINUE;
}

static void
test_dbus_roundtrip (void)
{
  struct roundtrip_state state;
  GDBusConnection *bus;
  guint export_id;
  guint id;

  bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);

  state.rand = g_rand_new_with_seed (g_test_rand_int ());

  state.random = random_menu_new (state.rand, 2);
  export_id = g_dbus_connection_export_menu_model (bus, "/", G_MENU_MODEL (state.random), NULL);
  state.proxy = g_dbus_menu_model_get (bus, g_dbus_connection_get_unique_name (bus), "/");
  state.proxy_mirror = mirror_menu_new (G_MENU_MODEL (state.proxy));
  state.count = 0;
  state.success = 0;

  id = g_timeout_add (10, roundtrip_step, &state);

  state.loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (state.loop);

  g_main_loop_unref (state.loop);
  g_source_remove (id);
  g_object_unref (state.proxy);
  g_dbus_connection_unexport_menu_model (bus, export_id);
  g_object_unref (state.random);
  g_object_unref (state.proxy_mirror);
  g_rand_free (state.rand);
  g_object_unref (bus);
}

static gint items_changed_count;

static void
items_changed (GMenuModel *model,
               gint        position,
               gint        removed,
               gint        added,
               gpointer    data)
{
  items_changed_count++;
}

static gboolean
stop_loop (gpointer data)
{
  GMainLoop *loop = data;

  g_main_loop_quit (loop);

  return G_SOURCE_REMOVE;
}

static void
test_dbus_subscriptions (void)
{
  GDBusConnection *bus;
  GMenu *menu;
  GDBusMenuModel *proxy;
  GMainLoop *loop;
  GError *error = NULL;
  guint export_id;

  loop = g_main_loop_new (NULL, FALSE);

  bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);

  menu = g_menu_new ();

  export_id = g_dbus_connection_export_menu_model (bus, "/", G_MENU_MODEL (menu), &error);
  g_assert_no_error (error);

  proxy = g_dbus_menu_model_get (bus, g_dbus_connection_get_unique_name (bus), "/");
  items_changed_count = 0;
  g_signal_connect (proxy, "items-changed",
                    G_CALLBACK (items_changed), NULL);

  g_menu_append (menu, "item1", NULL);
  g_menu_append (menu, "item2", NULL);
  g_menu_append (menu, "item3", NULL);

  g_assert_cmpint (items_changed_count, ==, 0);

  g_timeout_add (100, stop_loop, loop);
  g_main_loop_run (loop);

  g_menu_model_get_n_items (G_MENU_MODEL (proxy));

  g_timeout_add (100, stop_loop, loop);
  g_main_loop_run (loop);

  g_assert_cmpint (items_changed_count, ==, 1);
  g_assert_cmpint (g_menu_model_get_n_items (G_MENU_MODEL (proxy)), ==, 3);

  g_timeout_add (100, stop_loop, loop);
  g_main_loop_run (loop);

  g_menu_append (menu, "item4", NULL);
  g_menu_append (menu, "item5", NULL);
  g_menu_append (menu, "item6", NULL);
  g_menu_remove (menu, 0);
  g_menu_remove (menu, 0);

  g_timeout_add (200, stop_loop, loop);
  g_main_loop_run (loop);

  g_assert_cmpint (items_changed_count, ==, 6);

  g_assert_cmpint (g_menu_model_get_n_items (G_MENU_MODEL (proxy)), ==, 4);
  g_object_unref (proxy);

  g_timeout_add (100, stop_loop, loop);
  g_main_loop_run (loop);

  g_menu_remove (menu, 0);
  g_menu_remove (menu, 0);

  g_timeout_add (100, stop_loop, loop);
  g_main_loop_run (loop);

  g_assert_cmpint (items_changed_count, ==, 6);

  g_dbus_connection_unexport_menu_model (bus, export_id);
  g_object_unref (menu);

  g_main_loop_unref (loop);
}

static gpointer
do_modify (gpointer data)
{
  RandomMenu *menu = data;
  GRand *rand;
  gint i;

  rand = g_rand_new_with_seed (g_test_rand_int ());

  for (i = 0; i < 10000; i++)
    {
      random_menu_change (menu, rand);
    }

  return NULL;
}

static gpointer
do_export (gpointer data)
{
  GMenuModel *menu = data;
  gint i;
  GDBusConnection *bus;
  gchar *path;
  GError *error = NULL;
  guint id;

  bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
  path = g_strdup_printf ("/%p", data);

  for (i = 0; i < 10000; i++)
    {
      id = g_dbus_connection_export_menu_model (bus, path, menu, &error);
      g_assert_no_error (error);
      g_dbus_connection_unexport_menu_model (bus, id);
      while (g_main_context_iteration (NULL, FALSE));
    }

  g_free (path);

  g_object_unref (bus);

  return NULL;
}

static void
test_dbus_threaded (void)
{
  RandomMenu *menu[10];
  GThread *call[10];
  GThread *export[10];
  gint i;

  for (i = 0; i < 10; i++)
    {
      menu[i] = random_menu_new (g_rand_new_with_seed (g_test_rand_int ()), 2);
      call[i] = g_thread_new ("call", do_modify, menu[i]);
      export[i] = g_thread_new ("export", do_export, menu[i]);
    }

  for (i = 0; i < 10; i++)
    {
      g_thread_join (call[i]);
      g_thread_join (export[i]);
    }

  for (i = 0; i < 10; i++)
    g_object_unref (menu[i]);
}

typedef struct {
  GMenu *menu;
  GHashTable *objects;
} ParserData;

static void
start_element (GMarkupParseContext *context,
               const gchar         *element_name,
               const gchar        **attribute_names,
               const gchar        **attribute_values,
               gpointer             user_data,
               GError             **error)
{
  ParserData *data = user_data;

  if (g_strcmp0 (element_name, "menu") == 0)
    g_menu_markup_parser_start_menu (context, "domain", data->objects);
}

static void
end_element (GMarkupParseContext *context,
             const gchar         *element_name,
             gpointer             user_data,
             GError             **error)
{
  ParserData *data = user_data;

  if (g_strcmp0 (element_name, "menu") == 0)
    data->menu = g_menu_markup_parser_end_menu (context);
}

static GMenuModel *
parse_menu_string (const gchar *string, GHashTable *objects, GError **error)
{
  const GMarkupParser parser = {
    start_element, end_element, NULL, NULL, NULL
  };
  GMarkupParseContext *context;
  ParserData data;

  data.menu = NULL;
  data.objects = objects;

  context = g_markup_parse_context_new (&parser, 0, &data, NULL);
  g_markup_parse_context_parse (context, string, -1, error);
  g_markup_parse_context_free (context);

  return (GMenuModel*)data.menu;
}

static gchar *
menu_to_string (GMenuModel *menu)
{
  GString *s;

  s = g_string_new ("<menu>\n");
  g_menu_markup_print_string (s, menu, 2, 2);
  g_string_append (s, "</menu>\n");

  return g_string_free (s, FALSE);
}

const gchar menu_data[] =
  "<menu id='edit-menu'>\n"
  "  <section>\n"
  "    <item action='undo'>\n"
  "      <attribute name='label' translatable='yes' context='Stock label'>'_Undo'</attribute>\n"
  "    </item>\n"
  "    <item label='Redo' action='redo'/>\n"
  "  </section>\n"
  "  <section></section>\n"
  "  <section label='Copy &amp; Paste'>\n"
  "    <item label='Cut' action='cut'/>\n"
  "    <item label='Copy' action='copy'/>\n"
  "    <item label='Paste' action='paste'/>\n"
  "  </section>\n"
  "  <item><link name='section' id='blargh'>\n"
  "    <item label='Bold' action='bold'/>\n"
  "    <submenu label='Language'>\n"
  "      <item label='Latin' action='lang' target='latin'/>\n"
  "      <item label='Greek' action='lang' target='greek'/>\n"
  "      <item label='Urdu'  action='lang' target='urdu'/>\n"
  "    </submenu>\n"
  "    <item name='test unusual attributes'>\n"
  "      <attribute name='action' type='s'>'quite-some-action'</attribute>\n"
  "      <attribute name='target' type='i'>36</attribute>\n"
  "      <attribute name='chocolate-thunda' type='as'>['a','b']</attribute>\n"
  "      <attribute name='thing1' type='g'>'s(uu)'</attribute>\n"
  "      <attribute name='icon' type='s'>'small blue thing'</attribute>\n"
  "   </item>\n"
  "  </link></item>\n"
  "</menu>\n";

static void
test_markup_roundtrip (void)
{
  GError *error = NULL;
  GMenuModel *a;
  GMenuModel *b;
  gchar *s;
  gchar *s2;

  a = parse_menu_string (menu_data, NULL, &error);
  g_assert_no_error (error);
  g_assert (G_IS_MENU_MODEL (a));

  /* normalized representation */
  s = menu_to_string (a);

  b = parse_menu_string (s, NULL, &error);
  g_assert_no_error (error);
  g_assert (G_IS_MENU_MODEL (b));

  assert_menus_equal (G_MENU_MODEL (a), G_MENU_MODEL (b));

  s2 = menu_to_string (b);

  g_assert_cmpstr (s, ==, s2);

  g_object_unref (a);
  g_object_unref (b);
  g_free (s);
  g_free (s2);
}

static void
test_markup_objects (void)
{
  GMenuModel *a, *b;
  GHashTable *objects;
  GError *error = NULL;

  objects = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
  a = parse_menu_string (menu_data, objects, &error);
  g_assert_no_error (error);
  g_assert (G_IS_MENU_MODEL (a));
  g_assert_cmpint (g_hash_table_size (objects), ==, 1);
  b = g_hash_table_lookup (objects, "blargh");
  g_assert (G_IS_MENU_MODEL (b));
  g_object_unref (a);
  g_hash_table_unref (objects);
}

const gchar menu_data2[] =
  "<menu>"
  "  <section>"
  "    <item label='Redo' action='redo'/>"
  "  </section>"
  "  <section></section>\n"
  "  <section label='Copy &amp; Paste'>"
  "    <item label='Cut' action='cut'/>"
  "  </section>"
  "  <section id='section1'>"
  "    <item label='Bold' action='bold'/>"
  "    <submenu label='Language' id='submenu1'>"
  "      <section id='section2'>"
  "        <item label='Urdu'  action='lang' target='urdu'/>"
  "      </section>"
  "    </submenu>"
  "  </section>"
  "</menu>";
static void
test_markup_ids (void)
{
  GMenuModel *a, *b;
  GHashTable *objects;
  GError *error = NULL;

  objects = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
  a = parse_menu_string (menu_data2, objects, &error);
  g_assert_no_error (error);
  g_assert (G_IS_MENU_MODEL (a));
  g_assert_cmpint (g_hash_table_size (objects), ==, 3);
  b = g_hash_table_lookup (objects, "section1");
  g_assert (G_IS_MENU_MODEL (b));
  b = g_hash_table_lookup (objects, "section2");
  g_assert (G_IS_MENU_MODEL (b));
  b = g_hash_table_lookup (objects, "submenu1");
  g_assert (G_IS_MENU_MODEL (b));
  g_object_unref (a);
  g_hash_table_unref (objects);
}

static void
test_attributes (void)
{
  GMenu *menu;
  GMenuItem *item;
  GVariant *v;

  menu = g_menu_new ();

  item = g_menu_item_new ("test", NULL);
  g_menu_item_set_attribute_value (item, "boolean", g_variant_new_boolean (FALSE));
  g_menu_item_set_attribute_value (item, "string", g_variant_new_string ("bla"));
  g_menu_item_set_attribute_value (item, "double", g_variant_new_double (1.5));
  v = g_variant_new_parsed ("[('one', 1), ('two', %i), (%s, 3)]", 2, "three");
  g_menu_item_set_attribute_value (item, "complex", v);
  g_menu_item_set_attribute_value (item, "test-123", g_variant_new_string ("test-123"));

  g_menu_append_item (menu, item);

  g_assert_cmpint (g_menu_model_get_n_items (G_MENU_MODEL (menu)), ==, 1);

  v = g_menu_model_get_item_attribute_value (G_MENU_MODEL (menu), 0, "boolean", NULL);
  g_assert (g_variant_is_of_type (v, G_VARIANT_TYPE_BOOLEAN));
  g_variant_unref (v);

  v = g_menu_model_get_item_attribute_value (G_MENU_MODEL (menu), 0, "string", NULL);
  g_assert (g_variant_is_of_type (v, G_VARIANT_TYPE_STRING));
  g_variant_unref (v);

  v = g_menu_model_get_item_attribute_value (G_MENU_MODEL (menu), 0, "double", NULL);
  g_assert (g_variant_is_of_type (v, G_VARIANT_TYPE_DOUBLE));
  g_variant_unref (v);

  v = g_menu_model_get_item_attribute_value (G_MENU_MODEL (menu), 0, "complex", NULL);
  g_assert (g_variant_is_of_type (v, G_VARIANT_TYPE("a(si)")));
  g_variant_unref (v);

  g_object_unref (menu);
}

static void
test_links (void)
{
  GMenu *menu;
  GMenuModel *m;
  GMenuModel *x;
  GMenuItem *item;

  m = G_MENU_MODEL (g_menu_new ());
  g_menu_append (G_MENU (m), "test", NULL);

  menu = g_menu_new ();

  item = g_menu_item_new ("test1", NULL);
  g_menu_item_set_link (item, "section", m);
  g_menu_append_item (menu, item);

  item = g_menu_item_new ("test2", NULL);
  g_menu_item_set_link (item, "submenu", m);
  g_menu_append_item (menu, item);

  item = g_menu_item_new ("test3", NULL);
  g_menu_item_set_link (item, "wallet", m);
  g_menu_append_item (menu, item);

  item = g_menu_item_new ("test4", NULL);
  g_menu_item_set_link (item, "purse", m);
  g_menu_item_set_link (item, "purse", NULL);
  g_menu_append_item (menu, item);

  g_assert_cmpint (g_menu_model_get_n_items (G_MENU_MODEL (menu)), ==, 4);

  x = g_menu_model_get_item_link (G_MENU_MODEL (menu), 0, "section");
  g_assert (x == m);
  g_object_unref (x);

  x = g_menu_model_get_item_link (G_MENU_MODEL (menu), 1, "submenu");
  g_assert (x == m);
  g_object_unref (x);

  x = g_menu_model_get_item_link (G_MENU_MODEL (menu), 2, "wallet");
  g_assert (x == m);
  g_object_unref (x);

  x = g_menu_model_get_item_link (G_MENU_MODEL (menu), 3, "purse");
  g_assert (x == NULL);

  g_object_unref (m);
  g_object_unref (menu);
}

static void
test_mutable (void)
{
  GMenu *menu;

  menu = g_menu_new ();
  g_menu_append (menu, "test", "test");

  g_assert (g_menu_model_is_mutable (G_MENU_MODEL (menu)));
  g_menu_freeze (menu);
  g_assert (!g_menu_model_is_mutable (G_MENU_MODEL (menu)));

  g_object_unref (menu);
}

static void
test_misc (void)
{
  /* trying to use most of the GMenu api for constructing the
   * same menu two different ways
   */
  GMenu *a, *m, *m2;
  GMenuModel *b;
  GMenuItem *item;
  const gchar *s;

  a = g_menu_new ();
  item = g_menu_item_new ("test1", "action1::target1");
  g_menu_prepend_item (a, item);
  g_object_unref (item);

  m = g_menu_new ();
  g_menu_prepend (m, "test2a", "action2");
  g_menu_append (m, "test2c", NULL);
  g_menu_insert (m, 1, "test2b", NULL);

  item = g_menu_item_new_submenu ("test2", G_MENU_MODEL (m));
  g_menu_append_item (a, item);
  g_object_unref (item);
  g_object_unref (m);

  m = g_menu_new ();

  m2 = g_menu_new ();
  g_menu_append (m2, "x", NULL);
  g_menu_prepend_section (m, "test3a", G_MENU_MODEL (m2));
  g_object_unref (m2);

  item = g_menu_item_new_section ("test3", G_MENU_MODEL (m));
  g_menu_insert_item (a, -1, item);
  g_object_unref (item);
  g_object_unref (m);

  s = ""
"<menu>"
"  <item target='target1' action='action1' label='test1'/>"
"  <item label='test2'>"
"    <link name='submenu'>"
"      <item action='action2' label='test2a'/>"
"      <item label='test2b'/>"
"      <item label='test2c'/>"
"    </link>"
"  </item>"
"  <item label='test3'>"
"    <link name='section'>"
"      <item label='test3a'>"
"        <link name='section'>"
"          <item label='x'/>"
"        </link>"
"      </item>"
"    </link>"
"  </item>"
"</menu>";

  b = parse_menu_string (s, NULL, NULL);

  assert_menus_equal (G_MENU_MODEL (a), G_MENU_MODEL (b));
  g_object_unref (a);
  g_object_unref (b);
}

 /* Epilogue {{{1 */
int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_type_init ();

  g_test_add_func ("/gmenu/equality", test_equality);
  g_test_add_func ("/gmenu/random", test_random);
  g_test_add_func ("/gmenu/dbus/roundtrip", test_dbus_roundtrip);
  g_test_add_func ("/gmenu/dbus/subscriptions", test_dbus_subscriptions);
  g_test_add_func ("/gmenu/dbus/threaded", test_dbus_threaded);
  g_test_add_func ("/gmenu/markup/roundtrip", test_markup_roundtrip);
  g_test_add_func ("/gmenu/markup/objects", test_markup_objects);
  g_test_add_func ("/gmenu/markup/ids", test_markup_ids);
  g_test_add_func ("/gmenu/attributes", test_attributes);
  g_test_add_func ("/gmenu/links", test_links);
  g_test_add_func ("/gmenu/mutable", test_mutable);
  g_test_add_func ("/gmenu/misc", test_misc);

  return g_test_run ();
}
/* vim:set foldmethod=marker: */
