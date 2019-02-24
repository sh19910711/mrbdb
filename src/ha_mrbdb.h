#include <mruby.h>
#include <mruby/compile.h>
#include <mruby/data.h>
#include <mruby/class.h>
#include <mruby/string.h>

#include <sys/types.h>

#include "my_base.h"
#include "my_compiler.h"
#include "my_inttypes.h"
#include "sql/handler.h"
#include "thr_lock.h"

struct mrb_table_t {
  TABLE* table;
};

struct mrb_field_t {
  Field* field;
};

class MRBDB_share : public Handler_share {
  public:
    THR_LOCK lock;
    MRBDB_share();
    ~MRBDB_share() { thr_lock_delete(&lock); }
};

class ha_mrbdb : public handler {
  THR_LOCK_DATA lock;
  MRBDB_share *share;
  MRBDB_share *get_share();

  mrb_state *mrb;
  mrb_value mrb_handler;
  struct mrb_table_t* mrb_table;

  public:
    ha_mrbdb(handlerton *hton, TABLE_SHARE *table_Arg);
    ~ha_mrbdb() { mrb_close(mrb); };

    const char *table_type() const { return "MRBDB"; };

    virtual enum ha_key_alg get_default_index_algorithm() const {
      return HA_KEY_ALG_HASH;
    }

    virtual bool is_index_algorithm_supported(enum ha_key_alg key_alg) const {
      return key_alg == HA_KEY_ALG_HASH;
    }

    ulonglong table_flags() const {
      return HA_BINLOG_ROW_CAPABLE | HA_BINLOG_STMT_CAPABLE;
    }

    ulong index_flags(uint inx MY_ATTRIBUTE((unused)),
        uint part MY_ATTRIBUTE((unused)),
        bool all_parts MY_ATTRIBUTE((unused))) const {
      return 0;
    }

    uint max_supported_record_length() const {
      return HA_MAX_REC_LENGTH;
    }

    uint max_supported_keys() const {
      return 0;
    }

    uint max_supported_key_parts() const {
      return 0;
    }

    uint max_supported_key_length() const {
      return 0;
    }

    virtual double scan_time() {
      return (double)(stats.records + stats.deleted) / 20.0 + 10;
    }

    virtual double read_time(uint, uint, ha_rows rows) {
      return (double)rows / 20.0 + 1;
    }

    int open(const char *name, int mode, uint test_if_locked, const dd::Table *table_def);
    int close(void);
    int write_row(uchar *buf);
    int update_row(const uchar *old_data, uchar *new_data);
    int delete_row(const uchar *buf);
    int index_read_map(uchar *, const uchar *, key_part_map, enum ha_rkey_function);
    int index_next(uchar *);
    int index_prev(uchar *);
    int index_first(uchar *);
    int index_last(uchar *);

    int rnd_init(bool);
    int rnd_end();
    int rnd_next(uchar *);
    int rnd_pos(uchar *, uchar *);
    void position(const uchar *);
    int info(uint);
    int extra(enum ha_extra_function);
    int external_lock(THD *, int);
    int delete_all_rows(void);
    ha_rows records_in_range(uint, key_range *, key_range *);
    int delete_table(const char *, const dd::Table *);
    int rename_table(const char *, const char *, const dd::Table *, dd::Table *);
    int create(const char *, TABLE *, HA_CREATE_INFO *, dd::Table *);
    THR_LOCK_DATA **store_lock(THD *, THR_LOCK_DATA **, enum thr_lock_type);
};
