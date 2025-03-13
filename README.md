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

- **Import Transactions from CSV with Overwrite Option:**

  ```bash
  ./budget_tracker import --csv=<path-to-csv-file> [--overwrite]
  ```

- **Set Budget:**

  ```bash
  ./budget_tracker set-budget --year=<year> --amount=<amount>
  ```

- **Create Category:**

  ```bash
  ./budget_tracker create-category --label=<category-label> --description=<category-description>
  ```

- **Create Category Examples:**

  ```bash
  ./budget_tracker create-category-examples --examples=<example1,example2> --category-id=<category-id>
  ```

- **List Categories:**

  ```bash
  ./budget_tracker category-list
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

## How Category Examples and Import Work with OpenAI Few-Shot Encoding

The Budget Tracker uses OpenAI's few-shot encoding to classify transactions during import. By adding category examples using the `create-category-examples` command, you provide the model with context and examples for each category. This enhances the model's ability to accurately classify transactions based on their descriptions.

When you import transactions using the `import` command, the application queries the database for categories and their examples. It constructs a prompt dynamically, which includes these categories and examples, and sends it to OpenAI's API. The API then returns the most likely category for each transaction, which is used to update the transaction's category in the database.

This approach leverages the power of AI to automate and improve the accuracy of transaction categorization, making it easier for users to manage their finances.

This project is licensed under the MIT License.
