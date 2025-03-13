import argparse

from transformers import (
    pipeline,
    logging as hf_logging,
    AutoModelForSequenceClassification,
    AutoTokenizer,
)
from src.datalayer import connect_to_db, get_categories

hf_logging.set_verbosity_error()


def classify_expense(description, conn):
    model = AutoModelForSequenceClassification.from_pretrained(
        "mgrella/autonlp-bank-transaction-classification-5521155", use_auth_token=True
    )

    tokenizer = AutoTokenizer.from_pretrained(
        "mgrella/autonlp-bank-transaction-classification-5521155", use_auth_token=True
    )

    inputs = tokenizer("I love AutoNLP", return_tensors="pt")

    outputs = model(**inputs)
    return outputs
    # Get categories
    categories = get_categories(conn)
    conn.close()

    # Unpack data properly
    candidate_labels = [
        f"{row[1]}, {row[2]}" for row in categories
    ]  # "label, description"
    category_id_map = {
        f"{row[1]}, {row[2]}": row[0] for row in categories
    }  # Map label to id

    # Initialize the zero-shot classification pipeline
    classifier = pipeline("zero-shot-classification", model="facebook/bart-large-mnli")

    # Perform classification
    result = classifier(description, candidate_labels)

    # Ensure IDs match reordered labels
    formatted_result = {
        "labels": [
            {"label": label.split(",")[0], "id": category_id_map[label]}
            for label in result["labels"]  # Uses reordered labels from classifier
        ],
        "scores": result["scores"],
        "sequence": description,
    }

    return formatted_result


def infer_category(description, db_file):
    """Infer category for a given description using zero-shot classification."""

    conn = connect_to_db(db_file)
    categories = get_categories(conn)
    conn.close()

    # Unpack data properly
    candidate_labels = [
        f"{row[1]}, {row[2]}" for row in categories
    ]  # "label, description"
    category_id_map = {
        f"{row[1]}, {row[2]}": row[0] for row in categories
    }  # Map label to id

    # Initialize the zero-shot classification pipeline
    classifier = pipeline("zero-shot-classification", model="facebook/bart-large-mnli")

    # Perform classification
    result = classifier(description, candidate_labels)
    return result

    # Ensure IDs match reordered labels
    formatted_result = {
        "labels": [
            {"label": label.split(",")[0], "id": category_id_map[label]}
            for label in result["labels"]  # Uses reordered labels from classifier
        ],
        "scores": result["scores"],
        "sequence": description,
    }

    return formatted_result

    # Connect to the database and get categories
    # conn = connect_to_db()
    # if not conn:
    #     return None

    # categories = get_categories(conn)
    # conn.close()

    # # Unpack data properly
    # category_ids = [row[0] for row in categories]  # Extract 'id'
    # candidate_labels = [
    #     f"{row[1]}, {row[2]}" for row in categories
    # ]  # Format "label, description"

    # # Initialize the zero-shot classification pipeline
    # classifier = pipeline("zero-shot-classification", model="facebook/bart-large-mnli")

    # # Perform classification
    # result = classifier(description, candidate_labels)

    # # Format the result
    # formatted_result = {
    #     "labels": [
    #         {"label": label.split(",")[0], "id": category_ids[idx]}
    #         for idx, label in enumerate(result["labels"])
    #     ],
    #     "scores": result["scores"],
    #     "sequence": description,
    # }

    # return formatted_result


def main():
    parser = argparse.ArgumentParser(description="Expense Categorizer")
    parser.add_argument("command", help="Command to run")
    parser.add_argument("--description", help="Description of the item")
    parser.add_argument("--db-file", default="budget.db", help="Path to the SQLite database file")

    args = parser.parse_args()

    if args.command == "category-infer" and args.description:
        category_id = infer_category(args.description, args.db_file)
        print(f"{category_id}")
    else:
        print("Invalid command or missing description.")


if __name__ == "__main__":
    main()
