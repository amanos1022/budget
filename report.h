#ifndef REPORT_H
#define REPORT_H

void report_budget(int year, const char *exclude_categories);

void report_budget_month(const char *month);

void report_spend(const char *date_start, const char *date_end, const char *agg, const char *exclude_categories, const char *output_format);

#endif 
