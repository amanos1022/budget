# Budget Tracker

## Project Overview

The Budget Tracker is a command-line application that helps users manage their finances by tracking expenses, setting budgets, and generating reports. It uses an SQLite database to store transaction data, categories, and budget information.

## Setup Instructions

1. **Clone the Repository:**
   ```bash
   git clone <repository-url>
   cd <repository-directory>
   ```

2. **Database Migration:**
   Run the migration script to set up the database schema.
   ```bash
   ./migrate_db.sh
   ```

3. **Compile the Application:**
   Compile the C source code to create the executable.
   ```bash
   gcc -o budget_tracker budget_tracker.c -lsqlite3 -lm
   ```

## Usage

- **Import Transactions from CSV:**
  ```bash
  ./budget_tracker import --csv=<path-to-csv-file>
  ```

- **Set Budget:**
  ```bash
  ./budget_tracker set-budget --year=<year> --amount=<amount>
  ```

- **Create Category:**
  ```bash
  ./budget_tracker create-category --label=<category-label> --regex=<regex-pattern>
  ```

- **Report Spend:**
  ```bash
  ./budget_tracker report spend --date-start=<YYYY-MM-DD> --date-end=<YYYY-MM-DD> [--agg=<yearly|monthly>]
  ```

- **Report Budget:**
  ```bash
  ./budget_tracker report budget --year=<year>
  ./budget_tracker report budget --month=<YYYY-MM>
  ```

## License

This project is licensed under the MIT License.
