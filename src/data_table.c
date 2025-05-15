/**
 * File: data_table.h
 * 
 * Data Table formatter
 * All data layer can use to store and report a table of dataset
 * Reentrant, non-blocking codes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define DATA_TABLE_LINKED
#include "data_table.h"

void table_results_free(/* const TableDescr *td, */ TableResults* results){
    if (results->fields){
        free(results->fields);
        results->fields = NULL;
    }
    results->rows_count =0;
}

int table_results_alloc(const TableDescr* td, TableResults* results, size_t rows){
    if (results->fields){
        free(results->fields);
    }
    results->rows_count = rows;
    results->fields = malloc(sizeof(FieldValue) * td->fields_count * rows);
    return (results->fields)?1:0;
}

size_t table_head_dump(const TableDescr *td, char *buf, size_t len){
    size_t o=0;
    const int fields_count = td->fields_count;
    for (int i=0; i < fields_count; i++){
        const FieldDescr *fd = (FieldDescr *)&td->fields[i];
        const char *name = fd->name;
        const int width = fd->width;
        int pad = width - (int)strlen(name);
        if (pad < 0) pad = 0;
        if (i) o += snprintf(buf + o, len - o, "|");
        if (fd->align_right)
            o += snprintf(buf + o, len - o, "%*s", width, name );
        else
            o += snprintf(buf + o, len - o, "%-*s", width, name );
    }
    o += snprintf(buf + o, len - o, "\r\n");
    for (int i = 0; i < fields_count; i++) {
        if (i) {
            if (o < len - 1) {
                buf[o++] = '+';
            }
        }
        int rep = td->fields[i].width;
        if (o + rep >= len) rep = (int)(len - o - 1);
        if (rep > 0) {
            memset(buf + o, '-', rep);
            o += rep;
        }
        if (o < len) buf[o] = '\0';
    }
    o += snprintf(buf + o, len - o, "\r\n");
    return o;
}

size_t table_field_set_str(FieldValue *fv, const char *str){
    size_t len = strlen(str);
    if (len >= MAX_FIELD_STRING_LEN - 1) len = MAX_FIELD_STRING_LEN - 1;
    strncpy(fv->s, str, len);
    fv->s[len] = '\0';
    return len;
}

// Function to format a field entry into buffer
size_t table_field_format(const FieldDescr *fd, FieldValue *fv, char *buf, size_t buflen) {
    size_t o = 0;
    char tmp[MAX_FIELD_STRING_LEN] = {0};
    switch (fd->type) {
        case FIELD_TYPE_INT:
            snprintf(tmp, sizeof(tmp), fd->fmt, fv->i);
            break;
        case FIELD_TYPE_DOUBLE:
            if (fd->precision >= 0) {
                snprintf(tmp, sizeof(tmp), "%*.*f", fd->width, fd->precision, fv->d);
            } else {
                snprintf(tmp, sizeof(tmp), fd->fmt, fv->d);
            }
            break;
        case FIELD_TYPE_STRING:
            snprintf(tmp, sizeof(tmp), fd->fmt, fv->s);
            break;
        case FIELD_TYPE_CHAR:
            snprintf(tmp, sizeof(tmp), fd->fmt, fv->c);
            break;
    }
    int pad = fd->width - (int)strlen(tmp);
    if (pad < 0) pad = 0;

    if (fd->align_right) {
        o += snprintf(buf, buflen, "%*s%s", pad, "", tmp);
    } else {
        o += snprintf(buf, buflen, "%s%*s", tmp, pad, "");
    }
    return o;
}

/**
 * Dump a row to an ascii text buffer.
*/
size_t table_row_dump(const TableDescr *td, FieldValue *fv, char *buf, size_t len){
    size_t o =0;
    if (!fv)  return o;
    for (int i=0; i < td->fields_count; i++){
        if (i) o += snprintf(buf + o, len - o, "|");
        FieldDescr *fd = (FieldDescr *)&td->fields[i];
        o += table_field_format(fd, fv, buf + o, len - o);
        fv++;
    }
    o += snprintf(buf + o, len - o, "\r\n");
    return o;
}

/**
 * get an indexed row ptr from results
 */
FieldValue* table_row_get(const TableDescr *td, TableResults *res, size_t row_index){
    if (row_index >= res->rows_count) return NULL;
    return &res->fields[ row_index * td->fields_count];
}

/**
 * Dump a table into an ascii string
 */
size_t table_dump(TextContext *tc, char *buf, size_t len){
    const TableDescr *td = tc->td;
    TableResults *res= tc->res;
    size_t o =0;
    if (tc->title)o+= snprintf(buf+o, len-o, "%s\r\n", tc->title);
    o += table_head_dump(td, buf + o, len - o);
    for (size_t r=0; r < res->rows_count; r++){
        FieldValue *row= table_row_get(td, res, r);
        o += table_row_dump(td,row, buf + o, len - o);
    }
    return o;
}



/**
 * html head part
 */
size_t table_head_html(TextContext *tc, char *buf, size_t len){
    size_t o = 0;
    o += snprintf(buf + o, len - o, "<table cellspacing=0 cellpadding=0");
    if (tc->id) o += snprintf(buf + o, len - o, " id='%s'", tc->id);
    o += snprintf(buf + o, len - o, ">");
    if (tc->title) o += snprintf(buf + o, len - o, "<tr class='title' colspan=%d>%s</tr>", tc->td->fields_count, tc->title);
    o += snprintf(buf + o, len - o, "<tr class='head'>");
    for (int i=0; i< tc->td->fields_count; i++){ 
         o += snprintf(buf + o, len - o, "<th>%s</th>", tc->td->fields[i].name); 
    }
    o += snprintf(buf + o, len - o, "</tr>");
    return o;
}

/**
 *  html tail part
 */
size_t table_tail_html(TextContext *tc, char *buf, size_t len){
    (void)tc;
    size_t o = 0;      
    o += snprintf(buf + o, len - o, "</table>");
    return o;
}

/**
 * html row part
 */
size_t table_row_html(TextContext *tc, char *buf, size_t len){
    size_t o = 0;
    const TableDescr *td = tc->td;
    const FieldValue *fv = tc->row;
    const char* sclass[2]={"roweven", "rowodd"};
    o += snprintf(buf + o, len - o, "<tr class='%s'>", sclass[tc->row_count %2]);
    for (int i=0; i< td->fields_count; i++, fv++){ 
        const FieldDescr *fd= &td->fields[i];
        char tmp[MAX_FIELD_STRING_LEN] = {0};
        switch (fd->type) {
            case FIELD_TYPE_INT:
                snprintf(tmp, sizeof(tmp), fd->fmt, fv->i);
                break;
            case FIELD_TYPE_DOUBLE:
                if (fd->precision >= 0) {
                    snprintf(tmp, sizeof(tmp), "%*.*f", fd->width, fd->precision, fv->d);
                } else {
                    snprintf(tmp, sizeof(tmp), fd->fmt, fv->d);
                }
                break;
            case FIELD_TYPE_STRING:
                snprintf(tmp, sizeof(tmp), fd->fmt, fv->s);
                break;
            case FIELD_TYPE_CHAR:
                snprintf(tmp, sizeof(tmp), fd->fmt, fv->c);
                break;
        }
        char * cclass;
        if (fd->align_right){
            cclass="cellright";
        }else{
            cclass="cellleft";
        }
        o += snprintf(buf + o, len - o, "<td class='%s'>%s</td>",cclass, tmp); 
    }
    o += snprintf(buf + o, len - o, "</tr>");
    return o;
}

/**
 * generate the table into an html string
 */
size_t table_gen_html(TextContext *tc, char *buf, size_t len){
    size_t o =0;
    tc->row_count = 0;
    o += table_head_html( tc, buf + o, len - o);
    for (tc->row_count = 0; tc->row_count < tc->res->rows_count; tc->row_count++){
        tc->row= table_row_get(tc->td, tc->res, tc->row_count);
        o += table_row_html(tc, buf + o, len - o);
    }
    o += table_tail_html(tc, buf + o, len - o);
    return o;
}

/**
 * json dict format
 */
static size_t table_gen_json_dict(TextContext *tc, char *buf, size_t len){
    size_t o = 0;
    const TableDescr *td= tc->td;
    TableResults *res= tc->res;
    if (tc->flags & 1) o += snprintf(buf + o, len - o, "{");
    if (tc->flags & 2) o += snprintf(buf + o, len - o, ",\n");
    if (tc->id) o += snprintf(buf + o, len - o, "\"%s\":", tc->id);
    o += snprintf(buf + o, len - o, "[");
    for (size_t r = 0; r < res->rows_count; r++) {
        if (r) o += snprintf(buf + o, len - o, ",\n");
        o += snprintf(buf + o, len - o, "{");
        FieldValue *row = table_row_get(td, res, r);
        for (int i = 0; i < td->fields_count; i++) {
            if (i) o += snprintf(buf + o, len - o, ",");
            const FieldDescr *fd = &td->fields[i];
            FieldValue *fv = &row[i];
            o += snprintf(buf + o, len - o, "\"%s\":", fd->name);
            switch (fd->type) {
                case FIELD_TYPE_INT:
                    o += snprintf(buf + o, len - o, "%d", fv->i);
                    break;
                case FIELD_TYPE_DOUBLE:
                    o += snprintf(buf + o, len - o, "%.*f", fd->precision, fv->d);
                    break;
                case FIELD_TYPE_STRING:
                    o += snprintf(buf + o, len - o, "\"%s\"", fv->s);
                    break;
                case FIELD_TYPE_CHAR:
                    o += snprintf(buf + o, len - o, "\"%c\"", fv->c);
                    break;
            }
        }
        o += snprintf(buf + o, len - o, "}");
    }
    o += snprintf(buf + o, len - o, "]");
    if (tc->flags & 4) o += snprintf(buf + o, len - o, "\n}\n");
    return o;
}
/**
 * json array format
 */
static size_t table_gen_json_array(TextContext *tc, char *buf, size_t len) {
    size_t o = 0;
    const TableDescr *td= tc->td;
    TableResults *res= tc->res;
    if (tc->flags & 1) o += snprintf(buf + o, len - o, "{");
    if (tc->flags & 2) o += snprintf(buf + o, len - o, ",\n");
    if (tc->id) o += snprintf(buf + o, len - o, "\"%s\":", tc->id);
    o += snprintf(buf + o, len - o, "{");
    o += snprintf(buf + o, len - o, "\"columns\":[");
    for (int i = 0; i < td->fields_count; i++) {
        if (i) o += snprintf(buf + o, len - o, ",");
        o += snprintf(buf + o, len - o, "\"%s\"", td->fields[i].name);
    }
    o += snprintf(buf + o, len - o, "],\n\"rows\":[");
    for (size_t r = 0; r < res->rows_count; r++) {
        if (r) o += snprintf(buf + o, len - o, ",");
        o += snprintf(buf + o, len - o, "[");
        FieldValue *row = table_row_get(td, res, r);
        for (int i = 0; i < td->fields_count; i++) {
            if (i) o += snprintf(buf + o, len - o, ",\n");
            const FieldDescr *fd = &td->fields[i];
            FieldValue *fv = &row[i];
            switch (fd->type) {
                case FIELD_TYPE_INT:
                    o += snprintf(buf + o, len - o, "%d", fv->i);
                    break;
                case FIELD_TYPE_DOUBLE:
                    o += snprintf(buf + o, len - o, "%.*f", fd->precision, fv->d);
                    break;
                case FIELD_TYPE_STRING:
                    o += snprintf(buf + o, len - o, "\"%s\"", fv->s);
                    break;
                case FIELD_TYPE_CHAR:
                    o += snprintf(buf + o, len - o, "\"%c\"", fv->c);
                    break;
            }
        }
        o += snprintf(buf + o, len - o, "]");
    }
    o += snprintf(buf + o, len - o, "]}");
    if (tc->flags & 1) o += snprintf(buf + o, len - o, "\n}\n");
    return o;
}
/**
 * generate the table into a textual string in the spedicied format
 */
size_t table_gen_text(const TableDescr *td, TableResults *res, char *buf, size_t len, TextContext *tc) {
    if (!td || !res || !buf || !len || !tc) return 0;

    tc->td = td;
    tc->res = res;

    switch (tc->format) {
        case TEXT_FMT_TEXT:
            return table_dump(tc, buf, len);
        case TEXT_FMT_HTML:
            return table_gen_html(tc, buf, len);
        case TEXT_FMT_JSON_OBJECTS:
            return table_gen_json_dict(tc, buf, len);
        case TEXT_FMT_JSON_ARRAY:
            return table_gen_json_array(tc, buf, len);
        default:
            return 0;
    }
}