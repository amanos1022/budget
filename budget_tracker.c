#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <string.h>
#include <sqlite3.h>
#include <stdlib.h>
#include <time.h>
#include <stdlib.h>
#include <math.h>
#include <regex.h>

int get_category_id(sqlite3 *db, const char *description) {
    printf("Choose category for this item: \"%s\"\n", description);
    char *err_msg = 0;
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, "SELECT id, label, regex_pattern FROM categories", -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to fetch categories: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    int category_id = -1; // Default to -1 indicating no match found
    regex_t regex;
    int match_found = 0;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const unsigned char *label = sqlite3_column_text(stmt, 1);
        const unsigned char *pattern = sqlite3_column_text(stmt, 2);

        if (pattern == NULL) {
            continue;  // Skip this row if the regex pattern is NULL
        }

        // Compile the regex pattern
        if (regcomp(&regex, (const char *)pattern, REG_EXTENDED) == 0) {
            // Check if the description matches the regex pattern
            if (regexec(&regex, description, 0, NULL, 0) == 0) {
                category_id = id;
                match_found = 1;
                printf("Matched category: %s\n", label);
                break;
            }
            regfree(&regex);
        }
    }

    sqlite3_finalize(stmt);

    if (!match_found) {
        // If no match found, prompt for category
        int option = 1;
        printf("No matching category found. Please select a category:\n");
        rc = sqlite3_prepare_v2(db, "SELECT id, label FROM categories", -1, &stmt, 0);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "Failed to fetch categories: %s\n", sqlite3_errmsg(db));
            return -1;
        }

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int id = sqlite3_column_int(stmt, 0);
            const unsigned char *label = sqlite3_column_text(stmt, 1);
            printf("[%d] %s\n", option++, label);
        }
        printf("[%d] Enter new category\n", option++);
        printf("[%d] Skip Item\n", option);

        int choice;
        scanf("%d", &choice);

        if (choice == option) {
            // Skip the item
            return -1;
        } else if (choice == option - 1) {
            char new_label[256];
            printf("Enter new category label: ");
            while (getchar() != '\n'); // Clear the input buffer
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
    }

    return category_id;
}

void create_category(const char *label, const char *regex_pattern) {
    sqlite3 *db;
    char *err_msg = 0;
    int rc = sqlite3_open("budget.db", &db);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        return;
    }

    char *sql = sqlite3_mprintf("INSERT INTO categories (label, regex_pattern) VALUES ('%q', '%q');", label, regex_pattern);
    rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
    sqlite3_free(sql);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
    } else {
        printf("Category created: %s with regex: %s\n", label, regex_pattern);
    }

    sqlite3_close(db);
}

void report_budget_month(const char *month) {
    sqlite3 *db;
    char *err_msg = 0;
    int rc = sqlite3_open("budget.db", &db);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }

    int year;
    sscanf(month, "%d", &year);

    char sql[256];
    snprintf(sql, sizeof(sql),
             "SELECT amount FROM budgets WHERE year = %d;", year);

    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to fetch budget: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }

    double yearly_budget = 0.0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        yearly_budget = sqlite3_column_double(stmt, 0);
        double monthly_budget = yearly_budget / 12;
        printf("Monthly budget for %s: %.2f\n", month, monthly_budget);
    } else {
        printf("No budget set for %d\n", year);
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return;
    }
    sqlite3_finalize(stmt);

    snprintf(sql, sizeof(sql),
             "SELECT SUM(charge) FROM transactions WHERE strftime('%%Y-%%m', date) = '%s';", month);

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to fetch total spend: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }

    double total_spend = 0.0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        total_spend = sqlite3_column_double(stmt, 0);
        printf("Total spend for %s: %.2f\n", month, total_spend);
    } else {
        printf("No transactions found for %s\n", month);
    }
    sqlite3_finalize(stmt);

    printf("Remaining budget for %s: %.2f\n", month, (yearly_budget / 12) - fabs(total_spend));

    sqlite3_close(db);
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

        if (exists) {
            printf("Transaction already exists, skipping: %s, %s, %s\n", formatted_date, charge, description);
            continue;
        }

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

void report_spend(const char *date_start, const char *date_end, const char *agg) {
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
    if (agg == NULL) {
        snprintf(sql, sizeof(sql),
                 "SELECT c.label, SUM(t.charge) FROM transactions t "
                 "JOIN categories c ON t.category_id = c.id "
                 "WHERE t.date BETWEEN '%s' AND '%s' "
                 "GROUP BY c.label;", date_start, date_end);
    } else if (strcmp(agg, "yearly") == 0) {
        snprintf(sql, sizeof(sql),
                 "SELECT strftime('%%Y', t.date) AS year, SUM(t.charge) FROM transactions t "
                 "WHERE t.date BETWEEN '%s' AND '%s' "
                 "GROUP BY year;", date_start, date_end);
    } else if (strcmp(agg, "monthly") == 0) {
        snprintf(sql, sizeof(sql),
                 "SELECT strftime('%%Y-%%m', t.date) AS month, SUM(t.charge) FROM transactions t "
                 "WHERE t.date BETWEEN '%s' AND '%s' "
                 "GROUP BY month;", date_start, date_end);
    } else {
        fprintf(stderr, "Invalid aggregation option\n");
        sqlite3_close(db);
        return;
    }

    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to fetch report: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }

    double total_spend = 0.0;
    printf("%-15s | %s\n", agg ? (strcmp(agg, "yearly") == 0 ? "Year" : "Month") : "Category", "Spend");
    printf("-------------------------------\n");
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *label = (const char *)sqlite3_column_text(stmt, 0);
        double spend = sqlite3_column_double(stmt, 1);
        printf("%-15s | %.2f\n", label, spend);
        total_spend += spend;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

void report_budget(int year) {
    sqlite3 *db;
    char *err_msg = 0;
    int rc = sqlite3_open("budget.db", &db);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }

    char sql[256];
    snprintf(sql, sizeof(sql),
             "SELECT amount FROM budgets WHERE year = %d;", year);

    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to fetch budget: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }

    double budget = 0.0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        budget = sqlite3_column_double(stmt, 0);
        printf("Budget for %d: %.2f\n", year, budget);
    } else {
        printf("No budget set for %d\n", year);
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return;
    }
    sqlite3_finalize(stmt);

    snprintf(sql, sizeof(sql),
             "SELECT SUM(charge) FROM transactions WHERE strftime('%%Y', date) = '%d';", year);

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to fetch total spend: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }

    double total_spend = 0.0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        total_spend = sqlite3_column_double(stmt, 0);
        printf("Total spend for %d: %.2f\n", year, total_spend);
    } else {
        printf("No transactions found for %d\n", year);
    }
    sqlite3_finalize(stmt);

    printf("Remaining budget for %d: %.2f\n", year, budget - fabsf(total_spend));

    sqlite3_close(db);
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
    } else if (strcmp(argv[1], "create-category") == 0 && argc == 5) {
        const char *label = argv[2] + 8; // Skip "--label=" part
        const char *regex_pattern = argv[3] + 9; // Skip "--regex=" part
        create_category(label, regex_pattern);
    } else if (strcmp(argv[1], "report") == 0) {
        if (strcmp(argv[2], "spend") == 0 && argc >= 5) {
            const char *date_start = argv[3] + 13; // Skip "--date-start=" part
            const char *date_end = argv[4] + 11;   // Skip "--date-end=" part
            const char *agg = (argc == 6) ? argv[5] + 6 : NULL; // Skip "--agg=" part if present
            report_spend(date_start, date_end, agg);
        } else if (strcmp(argv[2], "budget") == 0 && argc >= 4) {
            if (strncmp(argv[3], "--year=", 7) == 0) {
                int year = atoi(argv[3] + 7); // Skip "--year=" part
                report_budget(year);
            } else if (strncmp(argv[3], "--month=", 8) == 0) {
                const char *month = argv[3] + 8; // Skip "--month=" part
                report_budget_month(month);
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
