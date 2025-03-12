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
    printf("[%d] Enter new category\n", option);

    int choice;
    scanf("%d", &choice);

    if (choice == option) {
        char new_label[256];
        printf("Enter new category label: ");
        scanf("%s", new_label);

        char *sql = sqlite3_mprintf("INSERT INTO categories (label) VALUES ('%q');", new_label);
        rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
        sqlite3_free(sql);

        if (rc != SQLITE_OK) {
            fprintf(stderr, "SQL error: %s\n", err_msg);
            sqlite3_free(err_msg);
        } else {
            category_id = (int)sqlite3_last_insert_rowid(db);
        }
    } else {
        sqlite3_reset(stmt);
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
    } else if (strcmp(argv[1], "report") == 0 && argc == 4 && strcmp(argv[2], "budget") == 0) {
        report_budget(atoi(argv[3] + 7)); // Skip "--year=" part
    } else {
        printf("Invalid command or options\n");
    }

    return 0;
}
