#!/bin/zsh

sqlite3 budget.db <<EOF
CREATE TABLE IF NOT EXISTS categories(
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    label TEXT UNIQUE
);

CREATE TABLE IF NOT EXISTS transactions(
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    date TEXT,
    charge REAL,
    description TEXT,
    category_id INTEGER,
    FOREIGN KEY(category_id) REFERENCES categories(id)
);

CREATE TABLE IF NOT EXISTS budgets(
    year INTEGER PRIMARY KEY,
    amount REAL
);
INSERT INTO categories (label) VALUES ('Other');

EOF
