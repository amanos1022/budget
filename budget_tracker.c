#include <stdio.h>
#include <string.h>
#include <sqlite3.h>
#include <stdlib.h>
#include <time.h>
#include <stdlib.h>

int get_category_id(sqlite3 *db, const char *description) {
    printf("Choose category for this item: \"%s\"\n", description);
    char *err_msg = 0;
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, "SELECT id, label FROM categories", -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to fetch categories: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    int category_id = 1; // Default to "Other" category
    int option = 1;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const unsigned char *label = sqlite3_column_text(stmt, 1);
        printf("[%d] %s\n", option++, label);
    }
    printf("[%d] Enter new category\n", option++);
    printf("[%d] Skip Item\n", option);

    int choice;
    scanf("%d", &choice);

    if (choice == option - 1) {
        char new_label[256];
        printf("Enter new category label: ");
        fgets(new_label, sizeof(new_label), stdin);
        new_label[strcspn(new_label, "\n")] = 0; // Remove newline character

        char *sql = sqlite3_mprintf("INSERT INTO categories (label) VALUES ('%q');", new_label);
        rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
        sqlite3_free(sql);

        if (rc != SQLITE_OK) {
            fprintf(stderr, "SQL error: %s\n", err_msg);
            sqlite3_free(err_msg);
        } else {
            category_id = (int)sqlite3_last_insert_rowid(db);
        }
    } else if (choice != option) {
        // Skip the item
        return -1;
    } else {
        option = 1;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            if (option == choice) {
                category_id = sqlite3_column_int(stmt, 0);
                break;
            }
            option++;
        }
    }

    sqlite3_finalize(stmt);
    return category_id;
}

void import_csv(const char *filename) {
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

        int category_id = 1; // Default to "Other" category
        if (atof(charge) < 0) { // Check if the transaction is a debit
            category_id = get_category_id(db, description);
        }
        char *insert_sql = sqlite3_mprintf("INSERT INTO transactions (date, charge, description, category_id) VALUES ('%q', %q, '%q', %d);", date, charge, description, category_id);
        rc = sqlite3_exec(db, insert_sql, 0, 0, &err_msg);
        sqlite3_free(insert_sql);

        if (rc != SQLITE_OK) {
            fprintf(stderr, "SQL error: %s\n", err_msg);
            sqlite3_free(err_msg);
            break;
        }
    }

    fclose(file);
    sqlite3_close(db);
}


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

void report_spend(const char *date_start, const char *date_end) {
    printf("Reporting spend from %s to %s\n", date_start, date_end);
    sqlite3 *db;
    char *err_msg = 0;
    int rc = sqlite3_open("budget.db", &db);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }

    char sql[512];
    snprintf(sql, sizeof(sql),
             "SELECT c.label, SUM(t.charge) FROM transactions t "
             "JOIN categories c ON t.category_id = c.id "
             "WHERE t.date BETWEEN '%s' AND '%s' "
             "GROUP BY c.label;", date_start, date_end);

    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to fetch report: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        printf("%s: %.2f\n", sqlite3_column_text(stmt, 0), sqlite3_column_double(stmt, 1));
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

void report_budget(int year) {
    printf("Reporting budget for year %d\n", year);
    // TODO: Implement budget reporting logic
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <command> [options]\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "import") == 0 && argc == 3) {
        import_csv(argv[2] + 6); // Skip "--csv=" part
    } else if (strcmp(argv[1], "set-budget") == 0 && argc == 4) {
        int year = atoi(argv[2] + 7); // Skip "--year=" part
        double amount = atof(argv[3] + 9); // Skip "--amount=" part
        set_budget(year, amount);
    } else if (strcmp(argv[1], "report") == 0) {
        if (strcmp(argv[2], "spend") == 0 && argc >= 5) {
            const char *date_start = argv[3] + 12; // Skip "--date-start=" part
            const char *date_end = argv[4] + 10;   // Skip "--date-end=" part
            report_spend(date_start, date_end);
        } else if (strcmp(argv[2], "budget") == 0 && argc >= 4) {
            if (strncmp(argv[3], "--year=", 7) == 0) {
                int year = atoi(argv[3] + 7); // Skip "--year=" part
                // Call report_budget function (to be implemented)
                printf("Report budget for year %d\n", year);
            } else if (strncmp(argv[3], "--month=", 8) == 0) {
                const char *month = argv[3] + 8; // Skip "--month=" part
                // Call report_budget_month function (to be implemented)
                printf("Report budget for month %s\n", month);
            } else {
                printf("Invalid budget report option\n");
            }
        } else {
            printf("Invalid report command or options\n");
        }
    } else {
        printf("Invalid command or options\n");
    }

    return 0;
}
