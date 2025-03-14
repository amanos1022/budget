#!/bin/zsh

sqlite3 budget.db <<EOF
CREATE TABLE IF NOT EXISTS categories(
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    label TEXT UNIQUE,
    description TEXT
);

CREATE TABLE IF NOT EXISTS transactions(
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    date DATE,
    charge REAL,
    description TEXT,
    category_id INTEGER,
    FOREIGN KEY(category_id) REFERENCES categories(id)
);

CREATE TABLE IF NOT EXISTS budgets(
    year INTEGER PRIMARY KEY,
    amount REAL
);
CREATE TABLE IF NOT EXISTS category_examples(
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    category_id INTEGER,
    example TEXT,
    FOREIGN KEY(category_id) REFERENCES categories(id)
);

INSERT OR IGNORE INTO categories (label) VALUES ('Other');

EOF
