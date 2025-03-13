#include <stdio.h>
#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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
                 "AND t.category_id NOT IN (%s)", exclude_categories);
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

void report_spend(const char *date_start, const char *date_end, const char *agg, const char *exclude_categories) {
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
                 "GROUP BY c.label;", date_start, date_end);
    } else if (strcmp(agg, "yearly") == 0) {
        snprintf(sql, sizeof(sql),
                 "SELECT strftime('%%Y', t.date) AS year, SUM(t.charge) FROM transactions t "
                 "WHERE t.date BETWEEN '%s' AND '%s' %s "
                 "GROUP BY year;", date_start, date_end);
    } else if (strcmp(agg, "monthly") == 0) {
        snprintf(sql, sizeof(sql),
                 "SELECT strftime('%%Y-%%m', t.date) AS month, SUM(t.charge) FROM transactions t "
                 "WHERE t.date BETWEEN '%s' AND '%s' %s "
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
