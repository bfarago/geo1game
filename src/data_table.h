/**
 * File: data_table.h
 */
#ifndef DATA_TABLE_H_
#define DATA_TABLE_H_
#ifdef __cplusplus
extern "C" {
#endif

#define MAX_FIELD_STRING_LEN (64u)

typedef enum {
    TEXT_FMT_TEXT,
    TEXT_FMT_HTML,
    TEXT_FMT_JSON_OBJECTS,
    TEXT_FMT_JSON_ARRAY
} TableTextFormat;

typedef enum {
    FIELD_TYPE_INT,
    FIELD_TYPE_DOUBLE,
    FIELD_TYPE_STRING,
    FIELD_TYPE_CHAR
} FieldType;

typedef enum {
    TABLE_JSON_OBJECTS,  // {"col1":val1,...}
    TABLE_JSON_ARRAY     // {columns:[], rows:[[]]}
} TableJsonFormat;

typedef struct {
    const char *name;
    const char *fmt;       // format string e.g. "%6.2f"
    int width;
    int align_right;       // 1 = right, 0 = left
    FieldType type;
    int precision;         // number of decimal places for double
} FieldDescr;

typedef union {
    int i;
    double d;
    char s[MAX_FIELD_STRING_LEN];
    char c;
} FieldValue;

typedef struct {
    int fields_count;
    const FieldDescr *fields;
} TableDescr;

typedef struct{
    size_t rows_count;
    FieldValue *fields;
} TableResults;

typedef struct{
    TableTextFormat format;
    const TableDescr *td;
    TableResults * res;
    FieldValue *row;
    size_t row_count;
    const char* title;
    const char* id;
    unsigned char flags;
} TextContext;

// API typedefs

typedef int (*table_results_alloc_fn)(const TableDescr* td, TableResults* results, size_t rows);
typedef void (*table_results_free_fn)( TableResults* results);
typedef size_t (*table_field_set_str_fn)(FieldValue *fv, const char *str);
typedef FieldValue* (*table_row_get_fn)(const TableDescr *td, TableResults *res, size_t row_index);
typedef size_t (*table_gen_text_fn)(const TableDescr *td, TableResults *res, char *buf, size_t len, TextContext *tc);

#ifdef DATA_TABLE_LINKED
    int table_results_alloc(const TableDescr* td, TableResults* results, size_t rows);
    void table_results_free(/* const TableDescr *td, */ TableResults* results);
    FieldValue* table_row_get(const TableDescr *td, TableResults *res, size_t row_index);
    size_t table_field_set_str(FieldValue *fv, const char *str);
    size_t table_gen_text(const TableDescr *td, TableResults *res, char *buf, size_t len, TextContext *tc);
#else
    #define table_results_free g_host->data.results_free
    #define table_results_alloc g_host->data.results_alloc
    #define table_row_get g_host->data.row_get
    #define table_field_set_str g_host->data.field_set_str
    #define table_gen_text g_host->data.gen_text
#endif

#ifdef __cplusplus
}
#endif

#endif // DATA_TABLE_H_
