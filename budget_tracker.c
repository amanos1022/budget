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
            const char *agg = (argc == 6) ? argv[5] + 6 : NULL; // Skip "--agg=" part if present
            const char *exclude_categories = NULL;
            for (int i = 5; i < argc; i++) {
                if (strncmp(argv[i], "--exclude-categories=", 21) == 0) {
                    exclude_categories = argv[i] + 21; // Skip "--exclude-categories=" part
                }
            }
            report_spend(date_start, date_end, agg, exclude_categories);
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
        for (int i = 5; i < argc; i++) {
            if (strncmp(argv[i], "--excluded-categories=", 22) == 0) {
                excluded_categories = argv[i] + 22; // Skip "--excluded-categories=" part
            }
        }
        transaction_list(start_date, end_date, excluded_categories);
        printf("Invalid command or options\n");
    }

    return 0;
}
