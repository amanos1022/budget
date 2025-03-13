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

size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t totalSize = size * nmemb;
    strncat((char *)userp, (char *)contents, totalSize);
    return totalSize;
}

int get_category_id(sqlite3 *db, const char *description) {
    int category_id = -1; // Default to -1 indicating no match found
    CURL *curl;
    CURLcode res;
    struct curl_slist *headers = NULL;
    char *api_key = getenv("OPENAI_API_KEY");
    if (!api_key) {
        fprintf(stderr, "OpenAI API key not set in environment\n");
        return -1;
    }

    curl = curl_easy_init();
    if(curl) {
        const char *MODEL = "gpt-4o-mini";
        char post_data[4096];

        // Retrieve categories and examples from the database
        sqlite3_stmt *stmt;
        char categories_query[] = "SELECT c.label, c.description, e.example FROM categories c LEFT JOIN category_examples e ON c.id = e.category_id";
        int rc = sqlite3_prepare_v2(db, categories_query, -1, &stmt, 0);

        if (rc != SQLITE_OK) {
            fprintf(stderr, "Failed to fetch categories and examples: %s\n", sqlite3_errmsg(db));
            return -1;
        }

        // Construct the prompt dynamically
        char categories_part[2048] = "Categories:\\n";
        char examples_part[2048] = "Examples:\\n";
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *label = (const char *)sqlite3_column_text(stmt, 0);
            const char *description = (const char *)sqlite3_column_text(stmt, 1);
            const char *example = (const char *)sqlite3_column_text(stmt, 2);

            if (label && description) {
                strcat(categories_part, "- ");
                strcat(categories_part, label);
                strcat(categories_part, ": ");
                strcat(categories_part, description);
                strcat(categories_part, "\\n");
            }

            if (example) {
                strcat(examples_part, example);
                strcat(examples_part, "\\n");
            }
        }
        sqlite3_finalize(stmt);

        // Format the JSON request properly
        int written = snprintf(post_data, sizeof(post_data),
            "{"
            "\"model\": \"%s\","
            "\"messages\": [{\"role\": \"user\", \"content\": \""
            "You are a financial assistant that categorizes transactions.\\n"
            "%s"
            "%s"
            "Now classify this transaction:\\n"
            "\\\"%s\\\"\\n"
            "Return only the category name as a string.\"}]}",
            MODEL, categories_part, examples_part, description);

        // Ensure snprintf didn't truncate output
        if (written < 0 || written >= sizeof(post_data)) {
            fprintf(stderr, "Error: JSON request is too long or snprintf failed.\n");
            return 0;
        }

        headers = curl_slist_append(headers, "Content-Type: application/json");
        char auth_header[256];
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
        headers = curl_slist_append(headers, auth_header);

        curl_easy_setopt(curl, CURLOPT_URL, "https://api.openai.com/v1/chat/completions");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);

        res = curl_easy_perform(curl);
        if(res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        } else {
            // Parse the response to extract the category name
            // char buffer[4096];
            char buffer[4096] = {0}; // Initialize buffer to store response
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, buffer);
            res = curl_easy_perform(curl);
            if (res == CURLE_OK) {
                struct json_object *parsed_json;
                struct json_object *choices;
                struct json_object *choice;
                struct json_object *message;
                struct json_object *content;

                parsed_json = json_tokener_parse(buffer);
                json_object_object_get_ex(parsed_json, "choices", &choices);
                choice = json_object_array_get_idx(choices, 0);
                json_object_object_get_ex(choice, "message", &message);
                json_object_object_get_ex(message, "content", &content);
                const char *category_name = json_object_get_string(content);

                // Query the database to get the category_id
                sqlite3_stmt *stmt;
                char *sql = sqlite3_mprintf("SELECT id FROM categories WHERE label = '%q';", category_name);
                int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
                sqlite3_free(sql);

                if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
                    category_id = sqlite3_column_int(stmt, 0);
                }
                sqlite3_finalize(stmt);
                json_object_put(parsed_json); // Free memory
            }
        }

        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }

    return category_id;
}

void create_category(const char *label, const char *description) {
    sqlite3 *db;
    char *err_msg = 0;
    int rc = sqlite3_open("budget.db", &db);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        return;
    }

    // Insert new category or update if it exists
    char *sql = sqlite3_mprintf("INSERT INTO categories (label, description) VALUES ('%q', '%q') "
                                "ON CONFLICT(label) DO UPDATE SET description=excluded.description;", label, description);
    rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
    sqlite3_free(sql);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
    } else {
        printf("Category created or updated: %s with description: %s\n", label, description);
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

        int category_id = 1; // Default to "Other" category
        if (atof(charge) < 0) { // Check if the transaction is a debit
            category_id = get_category_id(db, description);
        }

        if (exists) {
            if (overwrite) {
                printf("Transaction exists, updating category_id: %s, %s, %s\n", formatted_date, charge, description);
                char *update_sql = sqlite3_mprintf("UPDATE transactions SET category_id = %d WHERE date = '%q' AND charge = %q AND description = '%q';", category_id, formatted_date, charge, description);
                rc = sqlite3_exec(db, update_sql, 0, 0, &err_msg);
                sqlite3_free(update_sql);

                if (rc != SQLITE_OK) {
                    fprintf(stderr, "SQL error: %s\n", err_msg);
                    sqlite3_free(err_msg);
                    break;
                }
            } else {
                printf("Transaction already exists, skipping: %s, %s, %s\n", formatted_date, charge, description);
                continue;
            }
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
        sqlite3 *db;
        char *err_msg = 0;
        int rc = sqlite3_open("budget.db", &db);

        if (rc != SQLITE_OK) {
            fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
            return 1;
        }

        sqlite3_stmt *stmt;
        rc = sqlite3_prepare_v2(db, "SELECT id, label FROM categories", -1, &stmt, 0);

        if (rc != SQLITE_OK) {
            fprintf(stderr, "Failed to fetch categories: %s\n", sqlite3_errmsg(db));
            sqlite3_close(db);
            return 1;
        }

        printf("Category ID | Category Label\n");
        printf("---------------------------\n");
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int id = sqlite3_column_int(stmt, 0);
            const unsigned char *label = sqlite3_column_text(stmt, 1);
            printf("%11d | %s\n", id, label);
        }

        sqlite3_finalize(stmt);
        sqlite3_close(db);
    } else if (strcmp(argv[1], "create-category-examples") == 0 && argc == 4) {
        const char *examples = argv[2] + 11; // Skip "--examples=" part
        int category_id = atoi(argv[3] + 14); // Skip "--category-id=" part

        sqlite3 *db;
        char *err_msg = 0;
        int rc = sqlite3_open("budget.db", &db);

        if (rc != SQLITE_OK) {
            fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
            return 1;
        }

        char *example = strtok((char *)examples, ",");
        while (example != NULL) {
            char *sql = sqlite3_mprintf("INSERT INTO category_examples (category_id, example) VALUES (%d, '%q');", category_id, example);
            rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
            sqlite3_free(sql);

            if (rc != SQLITE_OK) {
                fprintf(stderr, "SQL error: %s\n", err_msg);
                sqlite3_free(err_msg);
                sqlite3_close(db);
                return 1;
            }

            example = strtok(NULL, ",");
        }

        printf("Examples added to category ID %d\n", category_id);
        sqlite3_close(db);
    } else if (strcmp(argv[1], "create-category") == 0 && argc == 4) {
        const char *label = argv[2] + 8; // Skip "--label=" part
        const char *description = argv[3] + 14; // Skip "--description=" part
        create_category(label, description);
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
