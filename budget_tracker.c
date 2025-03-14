#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <string.h> 
#include <sqlite3.h> 
#include <stdlib.h>
#include <time.h>
#include <stdlib.h>
#include <math.h>
#include <regex.h>
#include <json-c/json.h>

#include <curl/curl.h>
#include "report.h"
#include "import.h"
#include "category.h"

void set_budget(int year, double amount) {
    printf("Setting budget for year %d with amount %.2f\n", year, amount);
    sqlite3 *db;
    char *err_msg = 0;
    int rc = sqlite3_open("budget.db", &db);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }

    char *sql = sqlite3_mprintf("INSERT INTO budgets (year, amount) VALUES (%d, %f) "
                                "ON CONFLICT(year) DO UPDATE SET amount=excluded.amount;", year, amount);

    rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
    sqlite3_free(sql);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
    }

    sqlite3_close(db);
}

void transaction_list(const char *start_date, const char *end_date, const char *excluded_categories, const char *output_format) {
    sqlite3 *db;
    char *err_msg = 0;
    int rc = sqlite3_open("budget.db", &db);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        return;
    }

    char exclude_clause[512] = "";
    if (excluded_categories) {
        snprintf(exclude_clause, sizeof(exclude_clause),
                 "AND t.category_id NOT IN (%s)", excluded_categories);
    }

    char sql[512];
    snprintf(sql, sizeof(sql),
             "SELECT t.date, t.charge, t.description, c.label, t.category_id FROM transactions t "
             "JOIN categories c ON t.category_id = c.id "
             "WHERE t.date BETWEEN '%s' AND '%s' %s "
             "ORDER BY t.date;", start_date, end_date, exclude_clause);

    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to fetch transactions: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }

    if (output_format && strcmp(output_format, "json") == 0) {
        struct json_object *jarray = json_object_new_array();
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            struct json_object *jobj = json_object_new_object();
            json_object_object_add(jobj, "date", json_object_new_string((const char *)sqlite3_column_text(stmt, 0)));
            json_object_object_add(jobj, "charge", json_object_new_double(sqlite3_column_double(stmt, 1)));
            json_object_object_add(jobj, "description", json_object_new_string((const char *)sqlite3_column_text(stmt, 2)));
            json_object_object_add(jobj, "category", json_object_new_string((const char *)sqlite3_column_text(stmt, 3)));
            json_object_object_add(jobj, "category_id", json_object_new_int(sqlite3_column_int(stmt, 4)));
            json_object_array_add(jarray, jobj);
        }
        printf("%s\n", json_object_to_json_string(jarray));
        json_object_put(jarray);
    } else if (output_format && strcmp(output_format, "yaml") == 0) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            printf("- date: %s\n  charge: %.2f\n  description: %s\n  category: %s\n",
                   sqlite3_column_text(stmt, 0),
                   sqlite3_column_double(stmt, 1),
                   sqlite3_column_text(stmt, 2),
                   sqlite3_column_text(stmt, 3));
        }
    } else {
        printf("%-12s | %-10s | %-30s | %-15s | %-12s\n", "Date", "Charge", "Description", "Category", "Category ID");
        printf("-------------------------------------------------------------------------------------------\n");
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *date = (const char *)sqlite3_column_text(stmt, 0);
            double charge = sqlite3_column_double(stmt, 1);
            const char *description = (const char *)sqlite3_column_text(stmt, 2);
            const char *category = (const char *)sqlite3_column_text(stmt, 3);
            int category_id = sqlite3_column_int(stmt, 4);
            printf("%-12s | %-10.2f | %-30s | %-15s | %-12d\n", date, charge, description, category, category_id);
        }
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <command> [options]\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "import") == 0) {
        int overwrite = 0;
        const char *filename = NULL;

        for (int i = 2; i < argc; i++) {
            if (strncmp(argv[i], "--csv=", 6) == 0) {
                filename = argv[i] + 6; // Skip "--csv=" part
            } else if (strcmp(argv[i], "--overwrite") == 0) {
                overwrite = 1;
            }
        }

        if (filename) {
            import_csv(filename, overwrite);
        } else {
            printf("CSV file not specified.\n");
        }
    } else if (strcmp(argv[1], "set-budget") == 0 && argc == 4) {
        int year = atoi(argv[2] + 7); // Skip "--year=" part
        double amount = atof(argv[3] + 9); // Skip "--amount=" part
        set_budget(year, amount);
    } else if (strcmp(argv[1], "category-list") == 0) {
        category_list();
    } else if (strcmp(argv[1], "create-category-examples") == 0 && argc == 4) {
        const char *examples = argv[2] + 11; // Skip "--examples=" part
        int category_id = atoi(argv[3] + 14); // Skip "--category-id=" part

        create_category_examples(examples, category_id);
    } else if (strcmp(argv[1], "create-category") == 0 && argc == 4) {
        const char *label = argv[2] + 8; // Skip "--label=" part
        const char *description = argv[3] + 14; // Skip "--description=" part
        create_category(label, description);
    } else if (strcmp(argv[1], "report") == 0) {
        if (strcmp(argv[2], "spend") == 0 && argc >= 5) {
            const char *date_start = argv[3] + 13; // Skip "--date-start=" part
            const char *date_end = argv[4] + 11;   // Skip "--date-end=" part
            const char *agg = (argc >= 5) ? argv[5] + 6 : NULL; // Skip "--agg=" part if present
            const char *exclude_categories = NULL;
            const char *output_format = NULL;
            for (int i = 6; i < argc; i++) {
                if (strcmp(argv[i], "-ojson") == 0) {
                    output_format = "json";
                }
                if (strncmp(argv[i], "--exclude-categories=", 21) == 0) {
                    exclude_categories = argv[i] + 21; // Skip "--exclude-categories=" part
                }
            }
            report_spend(date_start, date_end, agg, exclude_categories, output_format);
        } else if (strcmp(argv[2], "budget") == 0 && argc >= 4) {
            if (strncmp(argv[3], "--year=", 7) == 0) {
                int year = atoi(argv[3] + 7); // Skip "--year=" part
                const char *exclude_categories = NULL;
                for (int i = 4; i < argc; i++) {
                    if (strncmp(argv[i], "--exclude-categories=", 21) == 0) {
                        exclude_categories = argv[i] + 21; // Skip "--exclude-categories=" part
                    }
                }
                report_budget(year, exclude_categories);
            } else if (strncmp(argv[3], "--month=", 8) == 0) {
                const char *month = argv[3] + 8; // Skip "--month=" part
                report_budget_month(month);
            } else {
                printf("Invalid budget report option\n");
            }
        } else {
            printf("Invalid report command or options\n");
        }
    } else if (strcmp(argv[1], "transaction") == 0 && strcmp(argv[2], "list") == 0 && argc >= 5) {
        const char *start_date = argv[3] + 13; // Skip "--start-date=" part
        const char *end_date = argv[4] + 11;   // Skip "--end-date=" part
        const char *excluded_categories = NULL;
        const char *output_format = NULL;
        for (int i = 5; i < argc; i++) {
            if (strcmp(argv[i], "-ojson") == 0) {
                output_format = "json";
            } else if (strcmp(argv[i], "-oyaml") == 0) {
                output_format = "yaml";
            }
            if (strncmp(argv[i], "--excluded-categories=", 22) == 0) {
                excluded_categories = argv[i] + 22; // Skip "--excluded-categories=" part
            }
        }
        transaction_list(start_date, end_date, excluded_categories, output_format);
    }

    return 0;
}
