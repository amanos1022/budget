#ifndef CATEGORY_H
#define CATEGORY_H

int get_category_id(sqlite3 *db, const char *description);
void create_category(const char *label, const char *description);
void category_list();
void create_category_examples(const char *examples, int category_id);

#endif