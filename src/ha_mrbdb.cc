#include "storage/mrbdb/ha_mrbdb.h"

#include "my_dbug.h"
#include "mysql/plugin.h"
#include "sql/sql_class.h"
#include "sql/sql_plugin.h"
#include "sql/field.h"
#include "sql/table.h"
#include "typelib.h"

static handler *mrbdb_create_handler(handlerton *hton, TABLE_SHARE *table,
                                     bool partitioned, MEM_ROOT *mem_root);
handlerton *mrbdb_hton;

static bool mrbdb_is_supported_system_table(const char *db,
                                            const char *table_name,
                                            bool is_sql_layer_system_table);

MRBDB_share::MRBDB_share() {
  thr_lock_init(&lock);
}

static int mrbdb_init_func(void *p) {
  mrbdb_hton = (handlerton *)p;
  mrbdb_hton->state = SHOW_OPTION_YES;
  mrbdb_hton->create = mrbdb_create_handler;
  mrbdb_hton->flags = HTON_CAN_RECREATE;
  mrbdb_hton->is_supported_system_table = mrbdb_is_supported_system_table;

  return 0;
}

MRBDB_share *ha_mrbdb::get_share() {
  MRBDB_share *tmp_share;

  lock_shared_ha_data();
  if (!(tmp_share = static_cast<MRBDB_share *>(get_ha_share_ptr()))) {
    tmp_share = new MRBDB_share;
    if (!tmp_share) goto err;

    set_ha_share_ptr(static_cast<MRBDB_share *>(tmp_share));
  }
err:
  unlock_shared_ha_data();
  return tmp_share;
}

static handler *mrbdb_create_handler(handlerton *hton, TABLE_SHARE *table,
                                     bool, MEM_ROOT *mem_root) {
  return new (mem_root) ha_mrbdb(hton, table);
}

static void mrb_mysql_table_free(mrb_state *mrb, void *ptr) {
  struct Table *t = (struct Table *)ptr;
  mrb_free(mrb, t);
}
const static struct mrb_data_type mrb_mysql_table_type = {"MySQLTable", mrb_mysql_table_free};

static struct mrb_data_type mrb_table_type = { "Table", mrb_free };
static struct mrb_data_type mrb_field_type = { "Field", mrb_free };

static mrb_value mrb_field_class_get_string(mrb_state *mrb, mrb_value self) {
  struct mrb_field_t* f = (struct mrb_field_t*)DATA_PTR(self);
  char buffer[1024];
  String attribute(buffer, sizeof(buffer), &my_charset_bin);

  f->field->val_str(&attribute, &attribute);

  return mrb_str_new(mrb, attribute.ptr(), attribute.length());
}

static mrb_value mrb_field_class_store(mrb_state *mrb, mrb_value self) {
  struct mrb_field_t* f = (struct mrb_field_t*)DATA_PTR(self);

  mrb_value val;
  mrb_get_args(mrb, "S", &val);
  char* s = RSTRING_PTR(val);
  f->field->store(s, strlen(s), &my_charset_bin);

  return self;
}

static mrb_value mrb_table_class_each_field(mrb_state *mrb, mrb_value self) {
  mrb_value block;
  mrb_get_args(mrb, "&", &block);

  struct mrb_table_t* t = (struct mrb_table_t*)DATA_PTR(self);

  if (t->table) {
    for (Field **f = t->table->field; *f; ++f) {
      struct mrb_field_t* mrb_field = (struct mrb_field_t*)mrb_malloc(mrb, sizeof(struct mrb_field_t));
      mrb_field->field = *f;
      mrb_value mrb_field_value = mrb_obj_value(Data_Wrap_Struct(mrb, mrb_class_get(mrb, "Field"), &mrb_field_type, mrb_field));
      mrb_yield(mrb, block, mrb_field_value);
    }
  }

  return self;
}

ha_mrbdb::ha_mrbdb(handlerton *hton, TABLE_SHARE *table_arg)
  : handler(hton, table_arg)
{
  mrb = mrb_open();

  mrb_define_const(mrb, mrb->kernel_module, "HA_ERR_END_OF_FILE", mrb_fixnum_value(HA_ERR_END_OF_FILE));

  // define table
  auto table_class = mrb_define_class(mrb, "Table", mrb->object_class);
  MRB_SET_INSTANCE_TT(table_class, MRB_TT_DATA);
  mrb_define_method(mrb, table_class, "each_field", mrb_table_class_each_field, MRB_ARGS_REQ(1));
  mrb_table = (struct mrb_table_t*)mrb_malloc(mrb, sizeof(struct mrb_table_t));
  mrb_table->table = table;
  mrb_value mrb_table_value = mrb_obj_value(Data_Wrap_Struct(mrb, mrb_class_get(mrb, "Table"), &mrb_table_type, mrb_table));

  // define field
  auto field_class = mrb_define_class(mrb, "Field", mrb->object_class);
  MRB_SET_INSTANCE_TT(table_class, MRB_TT_DATA);
  mrb_define_method(mrb, field_class, "str", mrb_field_class_get_string, MRB_ARGS_NONE());
  mrb_define_method(mrb, field_class, "store", mrb_field_class_store, MRB_ARGS_REQ(1));

  struct RClass *mrbHandler = mrb_class_get(mrb, "Handler");
  mrb_value klazz = mrb_obj_value(mrbHandler);
  mrb_handler = mrb_funcall(mrb, klazz, "new", 1, mrb_table_value);
}

static st_handler_tablename ha_mrbdb_system_tables[] = {
  { (const char *)NULL, (const char *)NULL }
};

static bool mrbdb_is_supported_system_table(const char *db, const char* table_name, bool is_sql_layer_system_table) {
  st_handler_tablename *systab;

  if (is_sql_layer_system_table) return false;

  systab = ha_mrbdb_system_tables;
  while (systab && systab->db) {
    if (systab->db == db && strcmp(systab->tablename, table_name) == 0) {
      return true;
    }
    systab++;
  }

  return false;
}

int ha_mrbdb::open(const char *, int, uint, const dd::Table *) {
  if (!(share = get_share())) return 1;
  thr_lock_data_init(&share->lock, &lock, NULL);
  return 0;
}

int ha_mrbdb::close(void) {
  return 0;
}

int ha_mrbdb::write_row(uchar *) {
  mrb_table->table = table;
  return mrb_funcall(mrb, mrb_handler, "write_row", 0).value.i;
}

int ha_mrbdb::update_row(const uchar *, uchar *) {
  mrb_table->table = table;
  return mrb_funcall(mrb, mrb_handler, "update_row", 0).value.i;
}

int ha_mrbdb::delete_row(const uchar *) {
  mrb_table->table = table;
  return mrb_funcall(mrb, mrb_handler, "delete_row", 0).value.i;
}

int ha_mrbdb::index_read_map(uchar *, const uchar *, key_part_map, enum ha_rkey_function) {
  return HA_ERR_WRONG_COMMAND;
}

int ha_mrbdb::index_next(uchar *) {
  return HA_ERR_WRONG_COMMAND;
}

int ha_mrbdb::index_prev(uchar *) {
  return HA_ERR_WRONG_COMMAND;
}

int ha_mrbdb::index_first(uchar *) {
  return HA_ERR_WRONG_COMMAND;
}

int ha_mrbdb::index_last(uchar *) {
  return HA_ERR_WRONG_COMMAND;
}

int ha_mrbdb::rnd_init(bool) {
  mrb_table->table = table;
  return mrb_funcall(mrb, mrb_handler, "rnd_init", 0).value.i;
}

int ha_mrbdb::rnd_end() {
  return mrb_funcall(mrb, mrb_handler, "rnd_end", 0).value.i;
}

int ha_mrbdb::rnd_next(uchar *buf) {
  memset(buf, 0, table->s->null_bytes);
  return mrb_funcall(mrb, mrb_handler, "rnd_next", 0).value.i;
}

void ha_mrbdb::position(const uchar *) {
}

int ha_mrbdb::rnd_pos(uchar *, uchar *) {
  return HA_ERR_WRONG_COMMAND;
}

int ha_mrbdb::info(uint) {
  return 0;
}

int ha_mrbdb::extra(enum ha_extra_function) {
  return 0;
}

int ha_mrbdb::delete_all_rows() {
  return HA_ERR_WRONG_COMMAND;
}

int ha_mrbdb::external_lock(THD *, int) {
  return 0;
}

THR_LOCK_DATA **ha_mrbdb::store_lock(THD *, THR_LOCK_DATA **to, enum thr_lock_type lock_type) {
  if (lock_type != TL_IGNORE && lock.type == TL_UNLOCK) lock.type = lock_type;
  *to++ = &lock;
  return to;
}

int ha_mrbdb::delete_table(const char *, const dd::Table *) {
  return 0;
}

int ha_mrbdb::rename_table(const char *, const char *, const dd::Table *, dd::Table *) {
  return HA_ERR_WRONG_COMMAND;
}

ha_rows ha_mrbdb::records_in_range(uint, key_range *, key_range *) {
  return 10;
}

static MYSQL_THDVAR_STR(last_create_thdvar, PLUGIN_VAR_MEMALLOC, NULL, NULL, NULL, NULL);

static MYSQL_THDVAR_UINT(create_count_thdvar, 0, NULL, NULL, NULL, 0, 0, 1000, 0);

int ha_mrbdb::create(const char *name, TABLE *, HA_CREATE_INFO *, dd::Table *) {
  THD *thd = ha_thd();
  char *buf = (char *)my_malloc(PSI_NOT_INSTRUMENTED, SHOW_VAR_FUNC_BUFF_SIZE, MYF(MY_FAE));
  snprintf(buf, SHOW_VAR_FUNC_BUFF_SIZE, "Last creation '%s'", name);
  THDVAR_SET(thd, last_create_thdvar, buf);
  my_free(buf);

  uint count = THDVAR(thd, create_count_thdvar) + 1;
  THDVAR_SET(thd, create_count_thdvar, &count);

  return 0;
}

struct st_mysql_storage_engine mrbdb_storage_engine = {
  MYSQL_HANDLERTON_INTERFACE_VERSION};

static ulong srv_enum_var = 0;
static ulong srv_ulong_var = 0;
static double srv_double_var = 0;

const char *enum_var_names[] = {"e1", "e2", NullS};

TYPELIB enum_var_typelib = {array_elements(enum_var_names) - 1,
  "enum_var_typelib", enum_var_names, NULL};

static MYSQL_SYSVAR_ENUM(enum_var, srv_enum_var, PLUGIN_VAR_RQCMDARG, "enum system variable.", NULL, NULL, 0, &enum_var_typelib);


static MYSQL_SYSVAR_ULONG(ulong_var, srv_ulong_var, PLUGIN_VAR_RQCMDARG, "0..1000", NULL, NULL, 8, 0, 1000, 0);

static MYSQL_SYSVAR_DOUBLE(double_var, srv_double_var, PLUGIN_VAR_RQCMDARG, "0.500000..1000.500000", NULL, NULL, 8.5, 0.5, 100.5, 0);

static MYSQL_THDVAR_DOUBLE(double_thdvar, PLUGIN_VAR_RQCMDARG, "0.500000..1000.500000", NULL, NULL, 8.5, 0.5, 1000.5, 0);

static SYS_VAR *mrbdb_system_variables[] = {
  MYSQL_SYSVAR(enum_var),
  MYSQL_SYSVAR(ulong_var),
  MYSQL_SYSVAR(double_var),
  MYSQL_SYSVAR(double_thdvar),
  MYSQL_SYSVAR(last_create_thdvar),
  MYSQL_SYSVAR(create_count_thdvar),
  NULL
};

static int show_func_mrbdb(MYSQL_THD, SHOW_VAR *var, char *buf) {
  var->type = SHOW_CHAR;
  var->value = buf;
  snprintf(buf, SHOW_VAR_FUNC_BUFF_SIZE, "enum_var is %lu, ulong_var is %lu, double_var is %f", srv_enum_var, srv_ulong_var, srv_double_var);
  return 0;
}

struct mrbdb_vars_t {
  ulong var1;
  double var2;
  char var3[64];
  bool var4;
  bool var5;
  ulong var6;
};

mrbdb_vars_t mrbdb_vars = {100, 20.01, "three hundred", true, 0, 8250};

static SHOW_VAR show_status_mrbdb[] = {
  {"var1", (char *)&mrbdb_vars.var1, SHOW_LONG, SHOW_SCOPE_GLOBAL},
  {"var2", (char *)&mrbdb_vars.var2, SHOW_DOUBLE, SHOW_SCOPE_GLOBAL},
  {0, 0, SHOW_UNDEF, SHOW_SCOPE_UNDEF}
};

static SHOW_VAR show_array_mrbdb[] = {
  {"array", (char *)show_status_mrbdb, SHOW_ARRAY, SHOW_SCOPE_GLOBAL},
  {"var3", (char *)&mrbdb_vars.var3, SHOW_CHAR, SHOW_SCOPE_GLOBAL},
  {"var4", (char *)&mrbdb_vars.var4, SHOW_BOOL, SHOW_SCOPE_GLOBAL},
  {0, 0, SHOW_UNDEF, SHOW_SCOPE_UNDEF}
};

static SHOW_VAR func_status[] = {
  {"mrbdb_func_mrbdb", (char *)show_func_mrbdb, SHOW_FUNC, SHOW_SCOPE_GLOBAL},
  {"mrbdb_status_var5", (char *)&mrbdb_vars.var5, SHOW_BOOL, SHOW_SCOPE_GLOBAL},
  {"mrbdb_status_var6", (char *)&mrbdb_vars.var6, SHOW_LONG, SHOW_SCOPE_GLOBAL},
  {"mrbdb_status", (char *)show_array_mrbdb, SHOW_ARRAY, SHOW_SCOPE_GLOBAL},
  {0, 0, SHOW_UNDEF, SHOW_SCOPE_UNDEF}
};

mysql_declare_plugin(hello) {
  MYSQL_STORAGE_ENGINE_PLUGIN,
  &mrbdb_storage_engine,
  "MRBDB",
  "Hiroyuki Sano",
  "Storage engine written in mruby",
  PLUGIN_LICENSE_GPL,
  mrbdb_init_func,
  NULL,
  NULL,
  0x0001,
  func_status,
  mrbdb_system_variables,
  NULL,
  0,
} mysql_declare_plugin_end;
