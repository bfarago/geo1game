#ifndef DATA_SQL_H
#define DATA_SQL_H

#include "data.h"
#include "plugin.h"

typedef int (*sql_execute_fn)(data_handle_t *dh, DbQuery *db_query);
typedef struct {
    sql_execute_fn execute;
} data_api_sql_t;

#endif // DATA_SQL_H