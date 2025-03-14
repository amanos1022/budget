#include <stdio.h>
#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <json-c/json.h>

/**
 * @brief Generate a budget report for a specific year.
 *
 * This function generates and prints a budget report for the specified year,
 * including total spend and remaining budget. It can exclude certain categories from the calculations.
 *
 * @param year The year for which to generate the budget report.
 * @param exclude_categories A comma-separated list of category IDs to exclude from the report.
 */
void report_budget(int year, const char *exclude_categories) {
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

    char exclude_clause[512] = "";
    if (exclude_categories) {
        snprintf(exclude_clause, sizeof(exclude_clause),
                 "AND category_id NOT IN (%s)", exclude_categories);
    }

    snprintf(sql, sizeof(sql),
             "SELECT SUM(charge) FROM transactions WHERE strftime('%%Y', date) = '%d' %s;", year, exclude_clause);

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

/**
 * @brief Generate a budget report for a specific month.
 *
 * This function generates and prints a budget report for the specified month,
 * including total spend and remaining budget. It calculates the monthly budget based on the yearly budget.
 *
 * @param month The month for which to generate the budget report in YYYY-MM format.
 */
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

/**
 * @brief Generate a spend report within a specified date range.
 *
 * This function generates and prints a spend report for transactions within the specified date range,
 * optionally aggregating by year or month, excluding certain categories, and outputting in different formats (JSON or plain text).
 *
 * @param date_start The start date of the transaction period (inclusive) in YYYY-MM-DD format.
 * @param date_end The end date of the transaction period (inclusive) in YYYY-MM-DD format.
 * @param agg The aggregation level ("yearly" or "monthly"), or NULL for no aggregation.
 * @param exclude_categories A comma-separated list of category IDs to exclude from the report.
 * @param output_format The format in which to output the report ("json" or plain text).
 */
void report_spend(const char *date_start, const char *date_end, const char *agg, const char *exclude_categories, const char *output_format) {
    printf("Reporting spend from %s to %s\n", date_start, date_end);
    sqlite3 *db;
    char *err_msg = 0;
    int rc = sqlite3_open("budget.db", &db);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }

    char sql[512] = {0};
    char exclude_clause[512] = "";
    if (exclude_categories) {
        snprintf(exclude_clause, sizeof(exclude_clause),
                 "AND t.category_id NOT IN (%s)", exclude_categories);
    }

    if (agg == NULL) {
        snprintf(sql, sizeof(sql),
                 "SELECT c.label, SUM(t.charge) FROM transactions t "
                 "JOIN categories c ON t.category_id = c.id "
                 "WHERE t.date BETWEEN '%s' AND '%s' %s "
                 "GROUP BY c.label;", date_start, date_end, exclude_clause);
    } else if (strcmp(agg, "yearly") == 0) {
        snprintf(sql, sizeof(sql),
                 "SELECT strftime('%%Y', t.date) AS year, c.label, SUM(t.charge) FROM transactions t "
                 "JOIN categories c ON t.category_id = c.id "
                 "WHERE t.date BETWEEN '%s' AND '%s' %s "
                 "GROUP BY year, c.label;", date_start, date_end, exclude_clause);
    } else if (strcmp(agg, "monthly") == 0) {
        snprintf(sql, sizeof(sql),
                 "SELECT strftime('%%Y-%%m', t.date) AS month, c.label, SUM(t.charge) FROM transactions t "
                 "JOIN categories c ON t.category_id = c.id "
                 "WHERE t.date BETWEEN '%s' AND '%s' %s "
                 "GROUP BY month, c.label;", date_start, date_end, exclude_clause);
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

    if (output_format && strcmp(output_format, "json") == 0) {
        struct json_object *jarray = json_object_new_array();
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            struct json_object *jobj = json_object_new_object();
            if (agg == NULL) {
                json_object_object_add(jobj, "category", json_object_new_string((const char *)sqlite3_column_text(stmt, 0)));
                json_object_object_add(jobj, "spend", json_object_new_double(sqlite3_column_double(stmt, 1)));
            } else {
                json_object_object_add(jobj, agg ? (strcmp(agg, "yearly") == 0 ? "year" : "month") : "category", json_object_new_string((const char *)sqlite3_column_text(stmt, 0)));
                json_object_object_add(jobj, "category", json_object_new_string((const char *)sqlite3_column_text(stmt, 1)));
                json_object_object_add(jobj, "spend", json_object_new_double(sqlite3_column_double(stmt, 2)));
            }
            json_object_array_add(jarray, jobj);
        }
        printf("%s\n", json_object_to_json_string(jarray));
        json_object_put(jarray);
    } else {
        double total_spend = 0.0;
        double spend = 0.0;
        if (agg == NULL) {
            printf("%-20s | %s\n", "Category", "Spend");
            printf("-------------------------------\n");
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const char *label = (const char *)sqlite3_column_text(stmt, 0);
                spend = sqlite3_column_double(stmt, 1);
                printf("%-20s | %.2f\n", label, spend);
            }
        } else {
            printf("%-10s | %-20s | %s\n", agg ? (strcmp(agg, "yearly") == 0 ? "Year" : "Month") : "Category", "Category", "Spend");
            printf("---------------------------------------------\n");
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const char *period = (const char *)sqlite3_column_text(stmt, 0);
                const char *label = (const char *)sqlite3_column_text(stmt, 1);
                spend = sqlite3_column_double(stmt, 2);
                printf("%-10s | %-20s | %.2f\n", period, label, spend);
            }
            total_spend += spend;
        }
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
}
