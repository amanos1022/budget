import sqlite3


def connect_to_db(db_name="budget.db"):
    """Connect to the SQLite database."""
    try:
        conn = sqlite3.connect(db_name)
        return conn
    except sqlite3.Error as e:
        print(f"Error connecting to database: {e}")
        return None


def get_categories(conn):
    """Retrieve all categories from the database."""
    try:
        cursor = conn.cursor()
        cursor.execute("SELECT label, description FROM categories WHERE id 1")
        categories = cursor.fetchall()
        return categories
    except sqlite3.Error as e:
        print(f"Error retrieving categories: {e}")
        return []
