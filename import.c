#define _XOPEN_SOURCE 700 // needed for stptotime
#include <stdio.h> 
#include <string.h> 
#include <sqlite3.h> 
#include <stdlib.h>
#include <time.h>
#include <stdlib.h>
#include "category.h"


void import_csv(const char *filename, int overwrite) {
    printf("Importing data from %s\n", filename);
    sqlite3 *db;
    char *err_msg = 0;
    int rc = sqlite3_open("budget.db", &db);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }

    char *sql = "CREATE TABLE IF NOT EXISTS transactions("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "date TEXT, "
                "charge REAL, "
                "description TEXT);";

    rc = sqlite3_exec(db, sql, 0, 0, &err_msg);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return;
    }

    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Could not open file: %s\n", filename);
        sqlite3_close(db);
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        char date[11], charge[20], description[256];
        sscanf(line, "\"%10[^\"]\",\"%19[^\"]\",%*[^,],%*[^,],\"%255[^\"]\"", date, charge, description);

        // Convert date from MM/DD/YYYY to YYYY-MM-DD
        struct tm tm;
        strptime(date, "%m/%d/%Y", &tm);
        char formatted_date[11];
        strftime(formatted_date, sizeof(formatted_date), "%Y-%m-%d", &tm);

        // Check if the charge is positive (credit), skip if it is
        if (atof(charge) > 0) {
            printf("Skipping credit transaction: %s, %s, %s\n", formatted_date, charge, description);
            continue;
        }

        // Check if the transaction already exists
        char *check_sql = sqlite3_mprintf("SELECT COUNT(*) FROM transactions WHERE date = '%q' AND charge = %q AND description = '%q';", formatted_date, charge, description);
        sqlite3_stmt *check_stmt;
        rc = sqlite3_prepare_v2(db, check_sql, -1, &check_stmt, 0);
        sqlite3_free(check_sql);

        if (rc != SQLITE_OK) {
            fprintf(stderr, "Failed to check existing transaction: %s\n", sqlite3_errmsg(db));
            sqlite3_finalize(check_stmt);
            continue;
        }

        int exists = 0;
        if (sqlite3_step(check_stmt) == SQLITE_ROW) {
            exists = sqlite3_column_int(check_stmt, 0);
        }
        sqlite3_finalize(check_stmt);

        if (!exists) {
            int category_id = 1; // Default to "Other" category
            if (atof(charge) < 0) { // Check if the transaction is a debit
                category_id = get_category_id(db, description);
            }

            char *insert_sql = sqlite3_mprintf("INSERT INTO transactions (date, charge, description, category_id) VALUES ('%q', %q, '%q', %d);", formatted_date, charge, description, category_id);
            rc = sqlite3_exec(db, insert_sql, 0, 0, &err_msg);
            sqlite3_free(insert_sql);

            if (rc != SQLITE_OK) {
                fprintf(stderr, "SQL error: %s\n", err_msg);
                sqlite3_free(err_msg);
                break;
            }
        } else if (overwrite) {
            printf("Transaction exists, updating category_id: %s, %s, %s\n", formatted_date, charge, description);
            int category_id = get_category_id(db, description);
            char *update_sql = sqlite3_mprintf("UPDATE transactions SET category_id = %d WHERE date = '%q' AND charge = %q AND description = '%q';", category_id, formatted_date, charge, description);
            rc = sqlite3_exec(db, update_sql, 0, 0, &err_msg);
            sqlite3_free(update_sql);

            if (rc != SQLITE_OK) {
                fprintf(stderr, "SQL error: %s\n", err_msg);
                sqlite3_free(err_msg);
                break;
            }
        }
    }

    fclose(file);
    sqlite3_close(db);
}
