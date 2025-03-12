#!/bin/zsh

sqlite3 budget.db <<EOF
CREATE TABLE IF NOT EXISTS transactions(
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    date TEXT,
    charge REAL,
    description TEXT
);

CREATE TABLE IF NOT EXISTS budgets(
    year INTEGER PRIMARY KEY,
    amount REAL
);
EOF
